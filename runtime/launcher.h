#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

/* Shows the launcher.  On "Play" fills opt and the chosen ROM / BIOS
 * paths (bios empty = none) and returns true; returns false on quit. */
bool launcher_run(char *rom, size_t rom_size, char *bios, size_t bios_size);

#endif
