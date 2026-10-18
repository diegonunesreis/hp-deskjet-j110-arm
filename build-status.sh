#!/bin/bash
# Build read-only USB status and consumables utilities using HPLIP's transport.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${HPLIP_SRC:-$HERE/vendor-source/hplip-3.23.12}"
OUT="$HERE/build/status"
LIBUSB="$(brew --prefix libusb)"
SDK="$(xcrun --show-sdk-path)"

[ -f "$SRC/io/hpmud/hpmud.c" ] || { echo "Set HPLIP_SRC to an HPLIP source tree." >&2; exit 1; }
[ -f "$LIBUSB/include/libusb-1.0/libusb.h" ] || { echo "libusb headers not found at $LIBUSB." >&2; exit 1; }
mkdir -p "$OUT/etc" "$OUT/obj"
printf '[dirs]\nhome = %s\n' "$SRC" > "$OUT/etc/hplip.conf"

INCS=(-I"$SRC/io/hpmud" -I"$SRC/common" -I"$LIBUSB/include" -I"$LIBUSB/include/libusb-1.0")
DEFS=(-DHAVE_LIBUSB -DMUDNAME='"hp"' -DCONFDIR="\"$OUT/etc\"")
SOURCES=(hpmud mlc model pml dot4 musb)
for name in "${SOURCES[@]}"; do
  cc -arch arm64 -O2 "${INCS[@]}" "${DEFS[@]}" \
    -c "$SRC/io/hpmud/$name.c" -o "$OUT/obj/$name.o"
done
cc -arch arm64 -O2 "${INCS[@]}" "${DEFS[@]}" \
  -c "$SRC/common/utils.c" -o "$OUT/obj/utils.o"
cc -arch arm64 -O2 "${INCS[@]}" \
  -c "$HERE/src/j110-status.c" -o "$OUT/obj/j110-status.o"
cc -arch arm64 -o "$OUT/j110-status" \
  "$OUT"/obj/{hpmud,mlc,model,pml,dot4,musb,utils,j110-status}.o \
  -L"$LIBUSB/lib" -lusb-1.0 -lpthread -ldl

cc -arch arm64 -O2 "${INCS[@]}" -I"$SDK/usr/include/libxml2" \
  -c "$HERE/src/j110-supplies.c" -o "$OUT/obj/j110-supplies.o"
cc -arch arm64 -isysroot "$SDK" -o "$OUT/j110-supplies" \
  "$OUT"/obj/{hpmud,mlc,model,pml,dot4,musb,utils,j110-supplies}.o \
  -L"$LIBUSB/lib" -lusb-1.0 -L"$SDK/usr/lib" -lxml2 -lpthread -ldl

cc -arch arm64 -O2 -c "$HERE/src/j110-clean.c" -o "$OUT/obj/j110-clean.o"
cc -arch arm64 -o "$OUT/j110-clean" "$OUT/obj/j110-clean.o"

cc -arch arm64 -O2 "${INCS[@]}" -I"$SDK/usr/include/libxml2" \
  -c "$HERE/src/j110-align.c" -o "$OUT/obj/j110-align.o"
cc -arch arm64 -isysroot "$SDK" -o "$OUT/j110-align" \
  "$OUT"/obj/{hpmud,mlc,model,pml,dot4,musb,utils,j110-align}.o \
  -L"$LIBUSB/lib" -lusb-1.0 -L"$SDK/usr/lib" -lxml2 -lpthread -ldl

echo "Built: $OUT/j110-status (arm64, read-only)"
echo "Built: $OUT/j110-supplies (arm64, read-only)"
echo "Built: $OUT/j110-clean (arm64, disabled until the J110 DMF workflow is implemented)"
echo "Built: $OUT/j110-align (arm64, explicit confirmation required)"
