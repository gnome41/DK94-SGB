DK94 Recomp @VERSION@
====================

An unofficial static recompilation of Donkey Kong (Game Boy, 1994), with
Super Game Boy colours, border and sound, widescreen views and controller
support. Not affiliated with, endorsed by or sponsored by Nintendo. Donkey
Kong, Game Boy and Super Game Boy are trademarks of Nintendo.

This package contains NO game code or data. You need your own copy of:

  * the game ROM (required):
      Donkey Kong (World) (Rev 1) (SGB Enhanced).gb
      CRC32 F777A5D8, SHA-1 397ad2ff25627b83e02c71b54c72bb4deb39e0c0
  * a Super Game Boy BIOS (optional, adds the SGB's SNES sound effects):
      Super Game Boy (World).sfc or another revision

dumped from hardware you own.

How it works: when you press PLAY, the player recompiles YOUR ROM's code to
C and compiles it in memory with TinyCC (about a third of a second). Nothing
derived from the game is shipped with this program or written to disk.

Running
-------
Windows: run dk94.exe. Keep libtcc.dll and SDL2.dll next to it.
Linux:   run ./dk94. Keep libtcc.so next to it. Needs SDL2 from your
         distribution (e.g. Debian/Ubuntu: libsdl2-2.0-0) and glibc 2.34+.
         ./install-desktop-entry.sh adds it (with its icon) to your
         application menu.

The launcher opens. Put the ROM (and BIOS) next to the program or choose
them with Browse... / drag and drop; the launcher checks them, then press
PLAY. Settings are remembered.

Controls
--------
  Game Boy    Keyboard                    Controller
  D-pad       arrow keys                  D-pad or left stick
  A           X or K                      A / Y
  B           Z or J                      B / X
  Start       Enter                       Start
  Select      Right Shift / Backspace     Back
  fast-fwd    Tab (hold)                  RB or RT (hold)
  view        F10                         LB
  fullscreen  F11                         Guide
  mods        F9
  quit        Esc

Battery saves are written next to the ROM (.sav).

Mods: put mod packs in the mods/ folder and enable them in the launcher
(MOD PACKS > MANAGE...). F9 toggles them in game. See MODDING.md.
mods/DK94-Example is an example pack (HD border, HD timer digits, a new
first-stage tune) - enable it to try mods, or copy it to start your own.

Command line: dk94 --help (any option skips the launcher).

Licenses
--------
  DK94 Recomp   MIT                  LICENSE.txt
  TinyCC        LGPL-2.1             licenses/TinyCC-COPYING.txt,
                (libtcc.dll / libtcc.so, unmodified 0.9.27; source in
                 source/tcc-0.9.27.tar.bz2 - you may replace the library
                 with your own build)
  SDL2          zlib                 licenses/SDL2-LICENSE.txt (Windows)

Source code: the DK94-SGB repository.
