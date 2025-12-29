#ifndef TERMINAL_H
#define TERMINAL_H

#include "midi.h"
#include <stdint.h>

#define COLOR_RESET   "\x1b[0m"
#define COLOR_GREEN   "\x1b[1;32m"
#define COLOR_RED     "\x1b[1;31m"
#define COLOR_YELLOW  "\x1b[1;33m"
#define COLOR_BLUE    "\x1b[1;34m"
#define COLOR_ORANGE  "\x1b[1;38;5;208m"

typedef struct {
  int score;
  int streak;
  int hit;
  int miss;
} Stats;

typedef struct {
  double time_left;
  int lane;
  int type;
} Effect;

typedef struct {
  double time_left;
  double time_total;
  int x;
  int y;
  int type;
  int width;
  int height;
} MultilineEffect;

const char* lane_color(int lane);
void term_raw_on(void);
void term_raw_off(void);
void get_term_size(int *rows, int *cols);
double now_sec(void);
void clear_screen_hide_cursor(void);
void show_cursor(void);

void add_effect(int lane, int type, double duration);
void update_effects(double dt);
void add_multiline_effect(int x, int y, int type, double duration, int width, int height);
void update_multiline_effects(double dt);
void set_sustain_flames(uint8_t lane_mask);
void draw_frame(const ChordVec *chords, size_t cursor, double t,
                double lookahead, uint8_t held_mask, const Stats *st,
                double song_offset_ms, double global_offset_ms, 
                int selected_track, const TrackNameVec *track_names,
                const char *timing_feedback, int inverted_mode);

#endif
