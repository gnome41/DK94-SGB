#!/bin/sh
# Adds DK94 Recomp to your desktop's application menu (current user only).
# Run it from the folder you unpacked; run it again if you move the folder.
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
APPS=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$APPS"
cat > "$APPS/dk94-recomp.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=DK94 Recomp
Comment=Unofficial static recompilation of Donkey Kong (Game Boy, 1994)
Exec="$DIR/dk94"
Path=$DIR
Icon=$DIR/dk94.png
Terminal=false
Categories=Game;
DESKTOP
echo "Added $APPS/dk94-recomp.desktop"
