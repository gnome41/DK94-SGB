/* Launcher: pick the ROM and SGB BIOS (with validation), choose options,
 * then start the game.  Drawn into a 640x360 software canvas with a
 * built-in pixel font; driven by mouse, keyboard or controller.  Files
 * can be chosen with Browse (native dialog on Windows, built-in browser
 * elsewhere) or dropped onto the window.  Settings persist in the
 * per-user config directory. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>    /* before gb.h, whose R macro clashes with Windows headers */
#include <commdlg.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <SDL.h>
#include "gb.h"
#include "files.h"
#include "pad.h"
#include "launcher.h"
#include "font5x7.h"

#define W 640
#define H 360
#define PATH_MAX_LEN 1024

/* colours */
#define C_BG     0xFF15131Fu
#define C_PANEL  0xFF221F33u
#define C_FOCUS  0xFF3A3456u
#define C_TEXT   0xFFF0EEF8u
#define C_DIM    0xFF9A96B0u
#define C_TITLE  0xFFF2C14Eu
#define C_RED    0xFFE04848u
#define C_GREEN  0xFF5AD45Au
#define C_YELLOW 0xFFF2C14Eu
#define C_BUTTON 0xFF3D3858u
#define C_PLAY   0xFFC8342Cu

static uint32_t canvas[W * H];
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;

/* ------------------------------------------------------------ settings */

static struct {
    char rom[PATH_MAX_LEN], bios[PATH_MAX_LEN];
    int hw;          /* 0 Super Game Boy, 1 Game Boy */
    int view;        /* VIEW_* */
    int scale;       /* 0 auto, 1..8 */
    int fullscreen;
    int swap_ab;
    int save;        /* battery saves on */
    int interp;
    int uncapped;
} cfg = { .save = 1 };

static RomStatus rom_status;
static uint32_t rom_crc;
static BiosStatus bios_status;
static char message[160];
static uint32_t message_color = C_DIM;

static const char *config_path(void)
{
    static char path[PATH_MAX_LEN];
    if (getenv("DK94_LAUNCHER_CONFIG"))
        return getenv("DK94_LAUNCHER_CONFIG");
    if (!path[0]) {
        char *pref = SDL_GetPrefPath("DK94", "DK94-SGB");
        if (!pref)
            return NULL;
        snprintf(path, sizeof path, "%slauncher.ini", pref);
        SDL_free(pref);
    }
    return path;
}

static void load_settings(void)
{
    const char *p = config_path();
    size_t n;
    char *d = p ? (char *)file_read(p, &n, 65536) : NULL;
    if (!d)
        return;
    char *text = realloc(d, n + 1);
    if (!text) {
        free(d);
        return;
    }
    text[n] = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        int iv = atoi(v);
        if (!strcmp(k, "rom")) snprintf(cfg.rom, sizeof cfg.rom, "%s", v);
        else if (!strcmp(k, "bios")) snprintf(cfg.bios, sizeof cfg.bios, "%s", v);
        else if (!strcmp(k, "hardware")) cfg.hw = iv == 1;
        else if (!strcmp(k, "view")) cfg.view = iv >= 0 && iv < VIEW_COUNT ? iv : 0;
        else if (!strcmp(k, "scale")) cfg.scale = iv >= 0 && iv <= 8 ? iv : 0;
        else if (!strcmp(k, "fullscreen")) cfg.fullscreen = iv != 0;
        else if (!strcmp(k, "swap_ab")) cfg.swap_ab = iv != 0;
        else if (!strcmp(k, "save")) cfg.save = iv != 0;
        else if (!strcmp(k, "interpreter")) cfg.interp = iv != 0;
        else if (!strcmp(k, "uncapped")) cfg.uncapped = iv != 0;
    }
    free(text);
}

static void save_settings(void)
{
    const char *p = config_path();
    if (!p)
        return;
    char text[4096];
    int n = snprintf(text, sizeof text,
                     "rom=%s\nbios=%s\nhardware=%d\nview=%d\nscale=%d\nfullscreen=%d\n"
                     "swap_ab=%d\nsave=%d\ninterpreter=%d\nuncapped=%d\n",
                     cfg.rom, cfg.bios, cfg.hw, cfg.view, cfg.scale, cfg.fullscreen,
                     cfg.swap_ab, cfg.save, cfg.interp, cfg.uncapped);
    if (n > 0 && !file_write(p, text, (size_t)n))
        fprintf(stderr, "warning: cannot save launcher settings to %s\n", p);
}

static void set_message(uint32_t color, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    message_color = color;
}

static void set_rom(const char *path)
{
    if (path != cfg.rom)
        snprintf(cfg.rom, sizeof cfg.rom, "%s", path ? path : "");
    rom_status = rom_check(cfg.rom, &rom_crc);
}

static void set_bios(const char *path)
{
    if (path != cfg.bios)
        snprintf(cfg.bios, sizeof cfg.bios, "%s", path ? path : "");
    bios_status = bios_check(cfg.bios);
}

/* A dropped or browsed file: work out what it is. */
static void accept_file(const char *path, int want)   /* want: 0 any, 1 rom, 2 bios */
{
    if (want != 2 && rom_check(path, NULL) == ROM_OK) {
        set_rom(path);
        set_message(C_GREEN, "ROM OK - CHECKSUM MATCHES");
    } else if (want != 1 && bios_check(path) == BIOS_OK) {
        set_bios(path);
        set_message(C_GREEN, "SGB BIOS OK - SOUND DRIVER FOUND");
    } else if (want == 1) {
        set_rom(path);
        set_message(C_RED, "THAT FILE IS NOT THE SUPPORTED DONKEY KONG ROM");
    } else if (want == 2) {
        set_bios(path);
        set_message(C_RED, "THAT FILE IS NOT A SUPER GAME BOY BIOS");
    } else {
        set_message(C_RED, "NOT RECOGNISED AS THE DONKEY KONG ROM OR AN SGB BIOS");
    }
}

/* ------------------------------------------------------------ drawing */

static void fill(int x, int y, int w, int h, uint32_t c)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < H; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < W; i++)
            canvas[j * W + i] = c;
}

static const Glyph *glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 32;
    for (size_t i = 0; i < sizeof font5x7 / sizeof *font5x7; i++)
        if (font5x7[i].c == c)
            return &font5x7[i];
    return glyph('?');
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void text(int x, int y, const char *s, uint32_t c, int scale)
{
    for (; *s; s++, x += 6 * scale) {
        const Glyph *g = glyph(*s);
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g->rows[r][k] == '#')
                    fill(x + k * scale, y + r * scale, scale, scale, c);
    }
}

/* Draws s in at most max_chars, eliding the start of long paths. */
static void text_fit(int x, int y, const char *s, uint32_t c, int max_chars)
{
    size_t n = strlen(s);
    if ((int)n <= max_chars) {
        text(x, y, s, c, 1);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof buf, "...%s", s + n - (max_chars - 3));
    text(x, y, buf, c, 1);
}

/* ------------------------------------------------------------ layout */

enum {
    IT_ROM, IT_BIOS,
    IT_HW, IT_VIEW, IT_SCALE, IT_FULL, IT_LAYOUT, IT_SAVE, IT_CORE, IT_SPEED,
    IT_PLAY, IT_QUIT, IT_COUNT
};
enum { ACT_FOCUS, ACT_BROWSE, ACT_CLEAR, ACT_PREV, ACT_NEXT, ACT_PLAY, ACT_QUIT };

typedef struct { int x, y, w, h, item, act; } Hit;
static Hit hits[64];
static int nhits;
static int focus = IT_PLAY;

static void hit(int x, int y, int w, int h, int item, int act)
{
    if (nhits < (int)(sizeof hits / sizeof *hits))
        hits[nhits++] = (Hit){ x, y, w, h, item, act };
}

typedef struct {
    const char *label;
    int *value;
    int count;
    const char *names[9];
    const char *help;
} OptRow;

static OptRow opts[] = {
    { "HARDWARE", &cfg.hw, 2, { "SUPER GAME BOY", "GAME BOY" },
      "SGB: COLOURS, ARCADE BORDER AND SNES SOUND. GAME BOY: ORIGINAL 4 SHADES." },
    { "DISPLAY VIEW", &cfg.view, 3, { "ORIGINAL", "16:9 FRAME", "WIDESCREEN (EXPERIMENTAL)" },
      "16:9 FRAME KEEPS THE IMAGE EXACT. WIDESCREEN IS EXPERIMENTAL: SHOWS MORE OF WIDE LEVELS ONLY." },
    { "WINDOW SCALE", &cfg.scale, 9, { "AUTO", "1X", "2X", "3X", "4X", "5X", "6X", "7X", "8X" },
      "INTEGER SCALING ONLY - PIXELS ARE NEVER STRETCHED." },
    { "FULLSCREEN", &cfg.fullscreen, 2, { "OFF", "ON" }, "F11 / GUIDE BUTTON TOGGLES IN GAME." },
    { "CONTROLLER A/B", &cfg.swap_ab, 2, { "XBOX STYLE (A = A)", "NINTENDO STYLE (B = A)" },
      "WHICH FACE BUTTONS ACT AS THE GAME BOY'S A AND B." },
    { "BATTERY SAVES", &cfg.save, 2, { "OFF", "ON" }, "SAVES PROGRESS NEXT TO THE ROM (.SAV FILE)." },
    { "CPU CORE", &cfg.interp, 2, { "RECOMPILED (NATIVE)", "INTERPRETER (REFERENCE)" },
      "THE INTERPRETER IS THE SLOWER REFERENCE IMPLEMENTATION, FOR TESTING." },
    { "SPEED", &cfg.uncapped, 2, { "NORMAL", "UNCAPPED" },
      "UNCAPPED RUNS AS FAST AS POSSIBLE WITHOUT SOUND. TAB / RB FAST-FORWARDS IN GAME." },
};

static OptRow *opt_row(int item) { return item >= IT_HW && item <= IT_SPEED ? &opts[item - IT_HW] : NULL; }

static bool can_play(void) { return rom_status == ROM_OK; }

static void button(int x, int y, int w, int h, const char *label, int item, int act, uint32_t col, bool focused)
{
    fill(x, y, w, h, focused ? C_TEXT : col);
    fill(x + 1, y + 1, w - 2, h - 2, col);
    text(x + (w - text_w(label, 1)) / 2, y + (h - 7) / 2, label, C_TEXT, 1);
    hit(x, y, w, h, item, act);
}

static void badge(int x, int y, const char *s, uint32_t c)
{
    int w = text_w(s, 1) + 8;
    fill(x, y, w, 11, c);
    text(x + 4, y + 2, s, C_BG, 1);
}

static void draw(void)
{
    nhits = 0;
    fill(0, 0, W, H, C_BG);

    /* header */
    fill(0, 0, W, 44, C_PANEL);
    text(16, 9, "DK94 RECOMP", C_TITLE, 3);
    text(16 + text_w("DK94 RECOMP", 3) + 14, 12, "FOR DONKEY KONG (GAME BOY / SUPER GAME BOY, 1994)", C_TEXT, 1);
    text(16 + text_w("DK94 RECOMP", 3) + 14, 24, "UNOFFICIAL - NOT AFFILIATED WITH NINTENDO", C_DIM, 1);

    /* files */
    text(16, 54, "FILES", C_DIM, 1);
    for (int i = 0; i < 2; i++) {
        int item = i ? IT_BIOS : IT_ROM, y = 66 + i * 26;
        if (focus == item)
            fill(8, y - 4, W - 16, 22, C_FOCUS);
        hit(8, y - 4, W - 16, 22, item, ACT_FOCUS);
        text(16, y + 3, i ? "SGB BIOS" : "GAME ROM", C_TEXT, 1);
        const char *path = i ? cfg.bios : cfg.rom;
        if (!i) {
            badge(96, y + 1, rom_status == ROM_OK ? "OK" : rom_status == ROM_WRONG ? "WRONG ROM" : "MISSING",
                  rom_status == ROM_OK ? C_GREEN : C_RED);
        } else if (cfg.hw == 1) {
            badge(96, y + 1, "NOT USED", C_DIM);
        } else {
            badge(96, y + 1, bios_status == BIOS_OK ? "OK" : bios_status == BIOS_INVALID ? "INVALID" : "NOT SET",
                  bios_status == BIOS_OK ? C_GREEN : bios_status == BIOS_INVALID ? C_RED : C_YELLOW);
        }
        text_fit(172, y + 3, path[0] ? path : "(NONE - BROWSE OR DROP A FILE HERE)", path[0] ? C_TEXT : C_DIM,
                 i ? 47 : 58);
        if (i)
            button(470, y - 1, 70, 15, "CLEAR", item, ACT_CLEAR, C_BUTTON, false);
        button(548, y - 1, 80, 15, "BROWSE...", item, ACT_BROWSE, C_BUTTON, focus == item);
    }
    const char *hint;
    char hintbuf[160];
    if (rom_status == ROM_WRONG) {
        snprintf(hintbuf, sizeof hintbuf, "ROM CRC32 %08X - THIS BUILD NEEDS %08X: DONKEY KONG (WORLD) (REV 1) (SGB ENHANCED)",
                 (unsigned)rom_crc, (unsigned)expected_rom_crc32());
        hint = hintbuf;
    } else if (rom_status == ROM_MISSING) {
        hint = "REQUIRED: DONKEY KONG (WORLD) (REV 1) (SGB ENHANCED).GB";
    } else if (cfg.hw == 0 && bios_status != BIOS_OK) {
        hint = "OPTIONAL: A SUPER GAME BOY BIOS (.SFC) ADDS THE SGB'S SNES SOUND EFFECTS";
    } else {
        hint = "ALL FILES READY";
    }
    text(16, 117, hint, rom_status != ROM_OK ? C_RED : C_DIM, 1);

    /* options */
    text(16, 136, "OPTIONS", C_DIM, 1);
    for (int i = IT_HW; i <= IT_SPEED; i++) {
        OptRow *o = opt_row(i);
        int y = 148 + (i - IT_HW) * 15;
        bool f = focus == i;
        if (f)
            fill(8, y - 3, W - 16, 14, C_FOCUS);
        hit(8, y - 3, W - 16, 14, i, ACT_FOCUS);
        text(16, y, o->label, C_TEXT, 1);
        const char *name = o->names[*o->value];
        int cx = 300;
        text(cx - 64, y, "\x01", f ? C_TITLE : C_DIM, 1);
        text(cx + 160, y, "\x02", f ? C_TITLE : C_DIM, 1);
        text(cx + 50 - text_w(name, 1) / 2, y, name, f ? C_TITLE : C_TEXT, 1);
        hit(cx - 70, y - 3, 30, 14, i, ACT_PREV);
        hit(cx - 40, y - 3, 220, 14, i, ACT_NEXT);
    }
    OptRow *fo = opt_row(focus);
    if (fo)
        text(16, 272, fo->help, C_DIM, 1);

    /* buttons */
    button(196, 290, 120, 26, can_play() ? "PLAY" : "PLAY (NEED ROM)", IT_PLAY, ACT_PLAY,
           can_play() ? C_PLAY : C_BUTTON, focus == IT_PLAY);
    button(332, 290, 112, 26, "QUIT", IT_QUIT, ACT_QUIT, C_BUTTON, focus == IT_QUIT);

    /* footer */
    fill(0, H - 30, W, 30, C_PANEL);
    text(16, H - 25, message[0] ? message : "READY", message_color, 1);
    text(16, H - 13, "\x03\x04 SELECT   \x01\x02 CHANGE   ENTER / A: OK   START: PLAY   ESC: QUIT   "
                     "DROP FILES ONTO THIS WINDOW", C_DIM, 1);
}

/* ------------------------------------------------------------ browse */

#ifdef _WIN32
static bool browse(int want, char *out, size_t out_size)
{
    wchar_t file[PATH_MAX_LEN] = L"";
    const char *cur = want == 1 ? cfg.rom : cfg.bios;
    if (cur[0])
        MultiByteToWideChar(CP_UTF8, 0, cur, -1, file, PATH_MAX_LEN);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = file;
    ofn.nMaxFile = PATH_MAX_LEN;
    ofn.lpstrFilter = want == 1 ? L"Game Boy ROM (*.gb;*.gbc)\0*.gb;*.gbc\0All files\0*.*\0"
                                : L"Super Game Boy BIOS (*.sfc;*.smc;*.bin)\0*.sfc;*.smc;*.bin\0All files\0*.*\0";
    ofn.lpstrTitle = want == 1 ? L"Select the Donkey Kong ROM" : L"Select the Super Game Boy BIOS";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)out_size, NULL, NULL) > 0;
}
#else
/* Built-in file browser. */
typedef struct { char name[256]; bool dir; } Entry;

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static bool wanted_ext(const char *name, int want)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return false;
    if (want == 1)
        return !strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc");
    return !strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc") || !strcasecmp(dot, ".bin");
}

static int list_dir(const char *dir, Entry **out, int want)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int n = 0, cap = 64;
    Entry *e = malloc(cap * sizeof *e);
    strcpy(e[n].name, "..");
    e[n++].dir = true;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char full[PATH_MAX_LEN];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        bool isdir = S_ISDIR(st.st_mode);
        if (!isdir && !wanted_ext(de->d_name, want))
            continue;
        if (n == cap)
            e = realloc(e, (cap *= 2) * sizeof *e);
        snprintf(e[n].name, sizeof e[n].name, "%s", de->d_name);
        e[n++].dir = isdir;
    }
    closedir(d);
    qsort(e + 1, n - 1, sizeof *e, entry_cmp);
    *out = e;
    return n;
}

static void present(void);

static bool browse(int want, char *out, size_t out_size)
{
    char dir[PATH_MAX_LEN];
    const char *cur = want == 1 ? cfg.rom : cfg.bios;
    if (cur[0] && cur[0] == '/') {
        snprintf(dir, sizeof dir, "%s", cur);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir)
            *slash = 0;
    } else if (!getcwd(dir, sizeof dir)) {
        strcpy(dir, "/");
    }
    Entry *e = NULL;
    int n = list_dir(dir, &e, want), sel = 0, top = 0;
    const int rows = 20;
    bool done = false, chosen = false;
    uint8_t prev_pad = 0;
    while (!done) {
        if (n < 0) {
            strcpy(dir, "/");
            n = list_dir(dir, &e, want);
        }
        int activate = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                done = true;
            } else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_UP: sel--; break;
                case SDLK_DOWN: sel++; break;
                case SDLK_PAGEUP: sel -= rows; break;
                case SDLK_PAGEDOWN: sel += rows; break;
                case SDLK_HOME: sel = 0; break;
                case SDLK_END: sel = n - 1; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: activate = 1; break;
                case SDLK_BACKSPACE: sel = 0; activate = 1; break;
                case SDLK_ESCAPE: done = true; break;
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                sel -= ev.wheel.y * 3;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int r = (ev.button.y - 70) / 11;
                if (ev.button.x >= 40 && ev.button.x < W - 40 && r >= 0 && r < rows && top + r < n) {
                    sel = top + r;
                    activate = 1;
                } else if (ev.button.y > H - 60) {
                    done = true;   /* click on the footer cancels */
                }
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                pad_event(&ev);
            }
        }
        uint8_t p = pad_poll().buttons, pressed = p & ~prev_pad;
        prev_pad = p;
        if (pressed & 0x04) sel--;
        if (pressed & 0x08) sel++;
        if (pressed & 0x10) activate = 1;
        if (pressed & 0x20) { sel = 0; activate = 1; }   /* B: parent directory */
        if (pressed & 0x40) done = true;                 /* Select: cancel */
        if (sel < 0) sel = 0;
        if (sel >= n) sel = n - 1;
        if (activate && n > 0) {
            Entry *x = &e[sel];
            char next[PATH_MAX_LEN];
            if (!strcmp(x->name, "..")) {
                snprintf(next, sizeof next, "%s", dir);
                char *slash = strrchr(next, '/');
                if (slash && slash != next) *slash = 0;
                else strcpy(next, "/");
            } else {
                snprintf(next, sizeof next, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", x->name);
            }
            if (x->dir) {
                Entry *ne;
                int nn = list_dir(next, &ne, want);
                if (nn >= 0) {
                    free(e);
                    e = ne;
                    n = nn;
                    snprintf(dir, sizeof dir, "%s", next);
                    sel = top = 0;
                }
            } else {
                snprintf(out, out_size, "%s", next);
                chosen = done = true;
            }
        }
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;

        draw();
        fill(30, 30, W - 60, H - 60, C_PANEL);
        text(40, 38, want == 1 ? "SELECT THE DONKEY KONG ROM (.GB)" : "SELECT THE SUPER GAME BOY BIOS (.SFC)", C_TITLE, 1);
        text_fit(40, 52, dir, C_DIM, 92);
        for (int r = 0; r < rows && top + r < n; r++) {
            int i = top + r, y = 70 + r * 11;
            if (i == sel)
                fill(36, y - 2, W - 72, 11, C_FOCUS);
            char label[300];
            snprintf(label, sizeof label, "%s%s", e[i].name, e[i].dir ? "/" : "");
            text_fit(44, y, label, e[i].dir ? C_TITLE : C_TEXT, 88);
        }
        text(40, H - 50, "ENTER / A: OPEN   BACKSPACE / B: UP   ESC / SELECT: CANCEL", C_DIM, 1);
        present();
        SDL_Delay(16);
    }
    free(e);
    return chosen;
}
#endif

/* ------------------------------------------------------------ main loop */

static void present(void)
{
    SDL_UpdateTexture(tex, NULL, canvas, W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint8_t rgb[3] = { canvas[i] >> 16, canvas[i] >> 8, canvas[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void do_browse(int item)
{
    char path[PATH_MAX_LEN];
    if (browse(item == IT_ROM ? 1 : 2, path, sizeof path))
        accept_file(path, item == IT_ROM ? 1 : 2);
}

/* Returns 1 = play, -1 = quit, 0 = continue. */
static int act(int item, int a)
{
    OptRow *o = opt_row(item);
    focus = item;
    switch (a) {
    case ACT_BROWSE: do_browse(item); break;
    case ACT_CLEAR:
        set_bios(NULL);
        set_message(C_DIM, "SGB BIOS CLEARED - THE SGB'S SNES SOUND WILL BE OFF");
        break;
    case ACT_PREV: if (o) *o->value = (*o->value + o->count - 1) % o->count; break;
    case ACT_NEXT: if (o) *o->value = (*o->value + 1) % o->count; break;
    case ACT_PLAY:
        if (can_play())
            return 1;
        set_message(C_RED, rom_status == ROM_WRONG ? "THIS ROM IS NOT THE ONE THE GAME WAS RECOMPILED FROM"
                                                    : "PLEASE SELECT THE DONKEY KONG ROM FIRST");
        focus = IT_ROM;
        break;
    case ACT_QUIT: return -1;
    }
    return 0;
}

/* Enter / A on the focused item. */
static int activate(void)
{
    if (focus == IT_ROM || focus == IT_BIOS)
        return act(focus, ACT_BROWSE);
    if (focus == IT_PLAY)
        return act(focus, ACT_PLAY);
    if (focus == IT_QUIT)
        return act(focus, ACT_QUIT);
    return act(focus, ACT_NEXT);
}

/* DK94_LAUNCHER_KEYS="down,right,enter,...": scripted key presses, one per
 * frame (testing aid).  Returns false when the script has finished. */
static bool scripted_key(int frame, int *step)
{
    const char *keys = getenv("DK94_LAUNCHER_KEYS");
    if (!keys || frame < 2)
        return keys != NULL && *step >= 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", keys);
    int i = 0;
    for (char *k = strtok(buf, ","); k; k = strtok(NULL, ","), i++) {
        if (i != *step)
            continue;
        SDL_Keycode code = !strcmp(k, "up") ? SDLK_UP : !strcmp(k, "down") ? SDLK_DOWN
                         : !strcmp(k, "left") ? SDLK_LEFT : !strcmp(k, "right") ? SDLK_RIGHT
                         : !strcmp(k, "enter") ? SDLK_RETURN : !strcmp(k, "delete") ? SDLK_DELETE
                         : !strcmp(k, "esc") ? SDLK_ESCAPE : SDLK_UNKNOWN;
        SDL_Event ev = { 0 };
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = code;
        SDL_PushEvent(&ev);
        (*step)++;
        return true;
    }
    *step = -1;
    return false;
}

bool launcher_run(char *rom, size_t rom_size, char *bios, size_t bios_size)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    load_settings();
    if (!cfg.rom[0] || rom_check(cfg.rom, NULL) == ROM_MISSING) {
        char *p = find_default_rom();
        if (p) {
            snprintf(cfg.rom, sizeof cfg.rom, "%s", p);
            free(p);
        }
    }
    if (!cfg.bios[0]) {
        char *p = find_default_bios();
        if (p) {
            snprintf(cfg.bios, sizeof cfg.bios, "%s", p);
            free(p);
        }
    }
    set_rom(cfg.rom);
    set_bios(cfg.bios);
    if (rom_status == ROM_OK)
        set_message(C_GREEN, "ROM FOUND AND VERIFIED - PRESS PLAY");
    else
        set_message(C_YELLOW, "SELECT THE GAME ROM WITH BROWSE, OR DROP IT ONTO THIS WINDOW");
    if (!can_play())
        focus = IT_ROM;

    win = SDL_CreateWindow("DK94 Recomp - Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           W * 2, H * 2, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    char *base = SDL_GetBasePath();
    pad_init(base);
    SDL_free(base);

    int result = 0, frame = 0, step = 0;
    uint8_t prev_pad = 0;
    bool scripted = getenv("DK94_LAUNCHER_KEYS") != NULL;
    while (!result) {
        bool script_running = scripted_key(frame++, &step);
        SDL_Event ev;
        while (!result && SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                result = -1;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_UP: focus = (focus + IT_COUNT - 1) % IT_COUNT; break;
                case SDLK_DOWN: case SDLK_TAB: focus = (focus + 1) % IT_COUNT; break;
                case SDLK_LEFT:
                    if (opt_row(focus)) act(focus, ACT_PREV);
                    else if (focus == IT_QUIT) focus = IT_PLAY;
                    break;
                case SDLK_RIGHT:
                    if (opt_row(focus)) act(focus, ACT_NEXT);
                    else if (focus == IT_PLAY) focus = IT_QUIT;
                    break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: result = activate(); break;
                case SDLK_DELETE: case SDLK_BACKSPACE:
                    if (focus == IT_BIOS) act(IT_BIOS, ACT_CLEAR);
                    break;
                case SDLK_ESCAPE: result = -1; break;
                }
                break;
            case SDL_MOUSEMOTION:
                for (int i = 0; i < nhits; i++)
                    if (hits[i].act == ACT_FOCUS && ev.motion.x >= hits[i].x && ev.motion.x < hits[i].x + hits[i].w &&
                        ev.motion.y >= hits[i].y && ev.motion.y < hits[i].y + hits[i].h)
                        focus = hits[i].item;
                break;
            case SDL_MOUSEBUTTONDOWN: {
                int best = -1;
                for (int i = 0; i < nhits; i++)   /* later (smaller) hit boxes win */
                    if (ev.button.x >= hits[i].x && ev.button.x < hits[i].x + hits[i].w &&
                        ev.button.y >= hits[i].y && ev.button.y < hits[i].y + hits[i].h)
                        best = i;
                if (best < 0)
                    break;
                Hit *h = &hits[best];
                int a = h->act;
                if (ev.button.button == SDL_BUTTON_RIGHT && a == ACT_NEXT)
                    a = ACT_PREV;
                if (a == ACT_FOCUS)
                    a = h->item == IT_ROM || h->item == IT_BIOS ? ACT_FOCUS : ACT_NEXT;
                result = a == ACT_FOCUS ? (focus = h->item, 0) : act(h->item, a);
                break;
            }
            case SDL_MOUSEWHEEL:
                if (opt_row(focus))
                    act(focus, ev.wheel.y > 0 ? ACT_PREV : ACT_NEXT);
                break;
            case SDL_DROPFILE:
                accept_file(ev.drop.file, 0);
                SDL_free(ev.drop.file);
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                pad_event(&ev);
                break;
            }
        }
        uint8_t p = pad_poll().buttons, pressed = p & ~prev_pad;
        prev_pad = p;
        if (!result) {
            if (pressed & 0x04) focus = (focus + IT_COUNT - 1) % IT_COUNT;
            if (pressed & 0x08) focus = (focus + 1) % IT_COUNT;
            if ((pressed & 0x02) && opt_row(focus)) act(focus, ACT_PREV);
            if ((pressed & 0x01) && opt_row(focus)) act(focus, ACT_NEXT);
            if (pressed & 0x10) result = activate();
            if (pressed & 0x80) result = act(IT_PLAY, ACT_PLAY);
        }
        draw();
        present();
        if (scripted && !script_running && !result) {
            if (getenv("DK94_LAUNCHER_SHOT"))
                write_ppm(getenv("DK94_LAUNCHER_SHOT"));
            if (!getenv("DK94_LAUNCHER_STAY"))
                result = -1;
        }
        SDL_Delay(16);
    }

    if (result == 1) {
        if (!cfg.interp) {
            /* the player recompiles the ROM before the game window opens */
            set_message(C_TITLE, "RECOMPILING YOUR ROM TO NATIVE CODE...");
            draw();
            present();
        }
        save_settings();
        opt.sgb = cfg.hw == 0;
        opt.view = cfg.view;
        opt.scale = cfg.scale;
        opt.fullscreen = cfg.fullscreen;
        opt.swap_ab = cfg.swap_ab;
        opt.no_save = !cfg.save;
        opt.interp_only = cfg.interp;
        opt.unthrottled = cfg.uncapped;
        snprintf(rom, rom_size, "%s", cfg.rom);
        snprintf(bios, bios_size, "%s", opt.sgb && bios_status == BIOS_OK ? cfg.bios : "");
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return result == 1;
}
