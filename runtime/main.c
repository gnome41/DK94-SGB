#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>   /* renames main() to SDL_main() where the platform needs it */
#include "gb.h"
#include "pad.h"
#include "files.h"
#include "launcher.h"

Options opt = { .sgb = true };

bool platform_init(void);
void platform_set_sram_path(const char *p);
void platform_error(const char *msg);

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [options] [rom.gb]\n"
            "  (no arguments: open the launcher)\n"
            "  --launcher          open the launcher even when other options are given\n"
            "  --fullscreen        start in fullscreen\n"
            "  --dmg               plain Game Boy (no Super Game Boy palettes/border)\n"
            "  --scale N           window scale\n"
            "  --view MODE         original | frame (16:9, image unscaled) | wide (experimental: more of the level)\n"
            "  --interp            run everything on the interpreter (reference mode)\n"
            "  --headless          no window/audio\n"
            "  --unthrottled       run as fast as possible\n"
            "  --no-save           do not load or write the .sav file\n"
            "  --swap-ab           controller: B/X = A, A/Y = B (Nintendo button positions)\n"
            "  --sgb-bios FILE     Super Game Boy SNES ROM, enables SGB (SNES-side) sound\n"
            "  --frames N          exit after N frames\n"
            "  --input random:SEED deterministic pseudo-random input\n"
            "  --hash-log FILE     write a per-frame state hash\n"
            "  --log-fallback FILE record ROM addresses run by the interpreter\n"
            "  --screenshot FILE   write the last frame as PPM on exit\n"
            "  --wav FILE          record audio to a WAV file (with --headless)\n"
            "controls: arrows, X = A, Z = B, Enter = Start, RShift/Backspace = Select,\n"
            "          Tab = fast-forward, F10 = cycle view, F11 = fullscreen, Esc = quit\n"
            "controller: D-pad/left stick, A/Y = A, B/X = B, Start, Back = Select,\n"
            "          RB/RT = fast-forward, LB = cycle view, Guide = fullscreen\n",
            argv0);
}

int main(int argc, char **argv)
{
    if (getenv("DK94_PAD_SELFTEST"))
        return pad_selftest();

    const char *rom_path = NULL;
    bool want_launcher = argc == 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has_val = i + 1 < argc;
        if (!strcmp(a, "--dmg")) opt.sgb = false;
        else if (!strcmp(a, "--interp")) opt.interp_only = true;
        else if (!strcmp(a, "--headless")) opt.headless = true;
        else if (!strcmp(a, "--unthrottled")) opt.unthrottled = true;
        else if (!strcmp(a, "--no-save")) opt.no_save = true;
        else if (!strcmp(a, "--swap-ab")) opt.swap_ab = true;
        else if (!strcmp(a, "--fullscreen")) opt.fullscreen = true;
        else if (!strcmp(a, "--launcher")) want_launcher = true;
        else if (!strcmp(a, "--sgb-bios") && has_val) opt.sgb_bios = argv[++i];
        else if (!strcmp(a, "--scale") && has_val) opt.scale = atoi(argv[++i]);
        else if (!strcmp(a, "--view") && has_val) {
            const char *v = argv[++i];
            if (!strcmp(v, "original")) opt.view = VIEW_ORIGINAL;
            else if (!strcmp(v, "frame")) opt.view = VIEW_FRAME;
            else if (!strcmp(v, "wide")) opt.view = VIEW_WIDE;
            else { usage(argv[0]); return 2; }
        }
        else if (!strcmp(a, "--frames") && has_val) opt.max_frames = atol(argv[++i]);
        else if (!strcmp(a, "--input") && has_val) opt.input_script = argv[++i];
        else if (!strcmp(a, "--hash-log") && has_val) opt.hash_log = argv[++i];
        else if (!strcmp(a, "--log-fallback") && has_val) opt.fallback_log = argv[++i];
        else if (!strcmp(a, "--screenshot") && has_val) opt.screenshot = argv[++i];
        else if (!strcmp(a, "--wav") && has_val) opt.wav = argv[++i];
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (a[0] == '-') { usage(argv[0]); return 2; }
        else rom_path = a;
    }

    char *found_rom = NULL;
    if (want_launcher && !opt.headless) {
        static char launch_rom[1024], launch_bios[1024];
        if (!launcher_run(launch_rom, sizeof launch_rom, launch_bios, sizeof launch_bios))
            return 0;
        rom_path = launch_rom;
        opt.sgb_bios = launch_bios[0] ? launch_bios : NULL;
        opt.no_bios_search = true;
    }
    if (!rom_path)
        rom_path = found_rom = find_default_rom();

    char msg[512];
    if (!rom_path || !cart_load(rom_path)) {
        snprintf(msg, sizeof msg, "Could not load the ROM.\nPut \"%s\" next to the executable, "
                                  "pass its path as an argument, or run without arguments to "
                                  "open the launcher.", default_rom_name());
        platform_error(msg);
        return 1;
    }

    uint32_t crc = crc32_buf(gb.rom, gb.rom_size);
    if (crc != gen_rom_crc32 && !opt.interp_only) {
        snprintf(msg, sizeof msg, "ROM CRC32 %08X does not match the recompiled ROM (%08X).\n"
                                  "Use the ROM this build was generated from, or --interp.",
                 (unsigned)crc, (unsigned)gen_rom_crc32);
        platform_error(msg);
        return 1;
    }

    size_t n = strlen(rom_path);
    char *sav = malloc(n + 5);
    strcpy(sav, rom_path);
    char *dot = strrchr(sav, '.');
    if (dot && !strchr(dot, '/') && !strchr(dot, '\\'))
        *dot = 0;
    strcat(sav, ".sav");
    if (!opt.no_save)
        platform_set_sram_path(sav);

    if (!platform_init())
        return 1;
    mem_reset();
    sgb_reset();
    if (opt.sgb) {
        bool ok = false;
        if (opt.sgb_bios) {
            ok = sgb_sound_init(opt.sgb_bios);
            if (!ok)
                fprintf(stderr, "cannot use SGB BIOS %s - SNES sound disabled\n", opt.sgb_bios);
        } else if (!opt.no_bios_search) {
            char *p = find_default_bios();
            ok = p && sgb_sound_init(p);
            free(p);
        }
        if (!ok && !opt.sgb_bios && !opt.headless)
            fprintf(stderr, "note: no Super Game Boy BIOS found - SGB sound effects disabled "
                            "(see --sgb-bios)\n");
    }
    if (!opt.no_save)
        cart_load_sram(sav);
    cpu_reset();
    run_forever();
    return 0;
}
