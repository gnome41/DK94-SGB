/* File helpers shared by the game and the launcher: whole-file reads
 * through SDL_RWops (UTF-8 paths on every platform), ROM / BIOS
 * validation and the default file search. */
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "files.h"
#include "gb.h"

static const char *rom_names[] = {
    "Donkey Kong (World) (Rev 1) (SGB Enhanced).gb",
    "Donkey Kong (World) (Rev A) (SGB Enhanced).gb",
    "dk94.gb",
    "DK94.gb",
};

static const char *bios_names[] = {
    "Super Game Boy (World).sfc",
    "Super Game Boy (World) (Rev 1).sfc",
    "Super Game Boy (World) (Rev 2).sfc",
    "Super Game Boy (Japan, USA).sfc",
    "sgb.sfc",
    "SGB.sfc",
};

uint8_t *file_read(const char *path, size_t *size, size_t max)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw)
        return NULL;
    Sint64 n = SDL_RWsize(rw);
    if (n <= 0 || (size_t)n > max) {
        SDL_RWclose(rw);
        return NULL;
    }
    uint8_t *data = malloc((size_t)n);
    if (data && SDL_RWread(rw, data, 1, (size_t)n) != (size_t)n) {
        free(data);
        data = NULL;
    }
    SDL_RWclose(rw);
    if (data)
        *size = (size_t)n;
    return data;
}

bool file_write(const char *path, const void *data, size_t size)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "wb");
    if (!rw)
        return false;
    bool ok = SDL_RWwrite(rw, data, 1, size) == size;
    return SDL_RWclose(rw) == 0 && ok;
}

bool file_exists(const char *path)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (rw)
        SDL_RWclose(rw);
    return rw != NULL;
}

uint32_t crc32_buf(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

RomStatus rom_check(const char *path, uint32_t *crc_out)
{
    if (!path || !*path)
        return ROM_MISSING;
    size_t n;
    uint8_t *d = file_read(path, &n, 8 * 1024 * 1024);
    if (!d)
        return ROM_MISSING;
    uint32_t crc = crc32_buf(d, n);
    free(d);
    if (crc_out)
        *crc_out = crc;
    return crc == expected_rom_crc32() ? ROM_OK : ROM_WRONG;
}

BiosStatus bios_check(const char *path)
{
    if (!path || !*path)
        return BIOS_MISSING;
    if (!file_exists(path))
        return BIOS_MISSING;
    return sgb_bios_valid(path) ? BIOS_OK : BIOS_INVALID;
}

static char *join(const char *dir, const char *name)
{
    size_t n = strlen(dir) + strlen(name) + 1;
    char *p = malloc(n);
    if (p) {
        strcpy(p, dir);
        strcat(p, name);
    }
    return p;
}

/* Looks for each candidate name next to the executable, then in the
 * current directory.  Returns a malloc'd path or NULL. */
static char *find_named(const char **names, size_t count, bool (*accept)(const char *))
{
    char *base = SDL_GetBasePath();
    char *found = NULL;
    for (size_t i = 0; i < count && !found; i++) {
        /* next to the executable first: gives an absolute path, which stays
         * valid when the launcher saves it */
        char *p = base ? join(base, names[i]) : NULL;
        if (p && accept(p))
            found = p;
        else {
            free(p);
            if (accept(names[i]))
                found = strdup(names[i]);
        }
    }
    SDL_free(base);
    return found;
}

static bool rom_ok(const char *p) { return rom_check(p, NULL) == ROM_OK; }
static bool bios_ok(const char *p) { return bios_check(p) == BIOS_OK; }
static bool exists(const char *p) { return file_exists(p); }

char *find_default_rom(void)
{
    char *p = find_named(rom_names, sizeof rom_names / sizeof *rom_names, rom_ok);
    return p ? p : find_named(rom_names, sizeof rom_names / sizeof *rom_names, exists);
}

char *find_default_bios(void)
{
    return find_named(bios_names, sizeof bios_names / sizeof *bios_names, bios_ok);
}

const char *default_rom_name(void) { return rom_names[0]; }
