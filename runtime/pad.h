#ifndef PAD_H
#define PAD_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

typedef struct {
    uint8_t buttons;        /* Game Boy buttons, bit order as gb.buttons */
    bool fast_forward;      /* held */
    bool cycle_view;        /* pressed this frame */
    bool toggle_fullscreen; /* pressed this frame */
} PadInput;

void pad_init(const char *exe_dir);
void pad_event(const SDL_Event *e);
PadInput pad_poll(void);
int pad_count(void);
int pad_selftest(void);

void set_window_icon(SDL_Window *window);   /* icon.c */

#endif
