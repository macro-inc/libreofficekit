#!/bin/bash
BASE=$(dirname "$(dirname "$0")")
LO="$BASE/libreoffice-core"
W="$LO/workdir"
I="$LO/instdir"
D="$W/debug"
echo "Preparing debug symbols for Linux..."
mkdir -p "$D"
rm -rf "${D:?}/*"
for i in "$I"/program/*; do
  echo -n "$i"
  debug_name="$(basename "$i").debug" 
  objcopy --only-keep-debug --compress-debug-sections "$i" "$D/$debug_name"
  echo -n "."
  strip --discard-all --strip-debug --preserve-dates "$i"
  echo -n "."
  debug_file="$(realpath "$i")"
  (cd "$D" && objcopy --add-gnu-debuglink="$debug_name" "$debug_file")
  echo "."
done
