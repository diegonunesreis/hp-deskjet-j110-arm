#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
QUEUE="HP_Deskjet_1000_J110_ARM"
DIR="/Library/Printers/HP/Deskjet1000J110ARM"
PPD="$HERE/build/ppd/hp-deskjet_1000_j110_series.ppd"
DEVICE=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --device) [ "$#" -ge 2 ] || { echo "Falta o valor de --device." >&2; exit 2; }; DEVICE="$2"; shift 2 ;;
    -h|--help) echo "Uso: sudo $0 [--device URI_USB_DA_J110]"; exit 0 ;;
    *) echo "Opção desconhecida: $1" >&2; exit 2 ;;
  esac
done
[ "$(id -u)" -eq 0 ] || { echo "Execute com sudo: sudo $0" >&2; exit 1; }
[ -x "$HERE/build/hpcups" ] && [ -f "$HERE/build/libjpeg.8.dylib" ] && [ -f "$PPD" ] || { echo "Artefatos ausentes; rode ./build.sh primeiro." >&2; exit 1; }
[ "$(grep -c '^\*cupsFilter:' "$PPD")" -eq 1 ] || { echo "PPD inesperado: cupsFilter ausente ou duplicado." >&2; exit 1; }

if [ -z "$DEVICE" ]; then
  DEVICE="$(python3 - <<'PY'
import subprocess, sys
try:
    p = subprocess.run(["lpinfo", "-v"], capture_output=True, text=True, timeout=20, check=True)
except Exception:
    print("Falha ao descobrir a URI; informe --device URI.", file=sys.stderr); sys.exit(1)
matches = []
for line in p.stdout.splitlines():
    f = line.split(None, 1)
    if len(f) == 2 and f[0] == "direct" and f[1].startswith("usb://HP/Deskjet%201000%20J110%20series?serial="):
        matches.append(f[1])
if len(matches) != 1:
    print("Não encontrei exatamente uma Deskjet 1000 J110 USB; informe --device URI.", file=sys.stderr); sys.exit(1)
print(matches[0])
PY
)"
fi
case "$DEVICE" in "usb://HP/Deskjet%201000%20J110%20series?serial="*) ;; *) echo "URI não reconhecida como Deskjet 1000 J110 USB." >&2; exit 1 ;; esac

MARKER="$DIR/.rea-j110-arm"
QUEUE_PPD="/etc/cups/ppd/$QUEUE.ppd"
if [ -e "$DIR" ] && [ ! -f "$MARKER" ]; then echo "Diretório de destino não pertence ao instalador: $DIR" >&2; exit 1; fi
if lpstat -v "$QUEUE" >/dev/null 2>&1; then
  [ -f "$MARKER" ] && [ -f "$QUEUE_PPD" ] && grep -Fq "$DIR/hpcups" "$QUEUE_PPD" || { echo "A fila $QUEUE já existe e não pertence a este instalador." >&2; exit 1; }
  CURRENT="$(lpstat -v "$QUEUE" | sed 's/^[^:]*: //')"
  [ "$CURRENT" = "$DEVICE" ] || { echo "A fila existente usa outro dispositivo; não será alterada." >&2; exit 1; }
  ACTION="Atualizar"
else ACTION="Criar"
fi
echo "$ACTION a fila CUPS $QUEUE para a Deskjet 1000 J110."
echo "Instalar filtro/PPD em $DIR; não alterar fila padrão nem imprimir."
printf 'Digite INSTALL J110 para continuar: '
IFS= read -r CONFIRM
[ "$CONFIRM" = "INSTALL J110" ] || { echo "Cancelado; nenhuma alteração foi feita."; exit 0; }

install -d -o root -g wheel -m 755 "$DIR"
install -o root -g wheel -m 755 "$HERE/build/hpcups" "$DIR/hpcups"
install -o root -g wheel -m 644 "$HERE/build/libjpeg.8.dylib" "$DIR/libjpeg.8.dylib"
sed "s|^\*cupsFilter:.*|*cupsFilter: \"application/vnd.cups-raster 0 $DIR/hpcups\"|" "$PPD" > "$DIR/hp-deskjet_1000_j110_series.ppd"
grep -Fqx "*cupsFilter: \"application/vnd.cups-raster 0 $DIR/hpcups\"" "$DIR/hp-deskjet_1000_j110_series.ppd" || { echo "Falha ao preparar PPD." >&2; exit 1; }
printf '%s\n' 'rea-j110-arm-v1' > "$MARKER"
chown root:wheel "$MARKER"; chmod 644 "$MARKER"
lpadmin -p "$QUEUE" -v "$DEVICE" -P "$DIR/hp-deskjet_1000_j110_series.ppd" -o printer-is-shared=false
cupsenable "$QUEUE"; cupsaccept "$QUEUE"
echo "Fila instalada: $QUEUE. Nenhum trabalho foi enviado; a impressora padrão não mudou."
