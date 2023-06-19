#!/bin/bash
BASE=$(dirname "$(dirname "$0")")
LO="$BASE/libreoffice-core"
W="$LO/workdir"
I="$LO/instdir"
D="$W/debug"
echo "Preparing debug symbols for macOS..."
mkdir -p "$D"
rm -rf "${D:?}/*"
[ "$(uname -m)" == 'arm64' ]; then
  I="$I/aarch64"
else
  I="$I/x64"
fi
for i in "$I"/program/*; do
  echo -n "$i"
  debug_name="$(basename "$i").dSYM" 
  dsymutil "$i" -o "$D/$debug_name"
  echo -n "."
  strip -S "$i"
  echo "."
done
