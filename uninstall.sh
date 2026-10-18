#!/bin/bash
set -euo pipefail
QUEUE="HP_Deskjet_1000_J110_ARM"
DIR="/Library/Printers/HP/Deskjet1000J110ARM"
MARKER="$DIR/.rea-j110-arm"
[ "$(id -u)" -eq 0 ] || { echo "Execute com sudo: sudo $0" >&2; exit 1; }
[ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "rea-j110-arm-v1" ] || { echo "Marcador de propriedade ausente; nada será removido." >&2; exit 1; }
if lpstat -v "$QUEUE" >/dev/null 2>&1; then
  [ -f "/etc/cups/ppd/$QUEUE.ppd" ] && grep -Fq "$DIR/hpcups" "/etc/cups/ppd/$QUEUE.ppd" || { echo "A fila não aponta para este filtro; nada será removido." >&2; exit 1; }
fi
echo "Remover a fila $QUEUE e somente os arquivos marcados em $DIR."
printf 'Digite REMOVE J110 para continuar: '
IFS= read -r CONFIRM
[ "$CONFIRM" = "REMOVE J110" ] || { echo "Cancelado; nenhuma alteração foi feita."; exit 0; }
if lpstat -v "$QUEUE" >/dev/null 2>&1; then lpadmin -x "$QUEUE"; fi
rm -rf "$DIR"
echo "Fila removida; demais filas e a impressora padrão não foram alteradas."
