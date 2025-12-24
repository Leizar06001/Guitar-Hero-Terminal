#ifndef SETTINGS_H
#define SETTINGS_H

#include <SDL2/SDL.h>

typedef struct {
  SDL_Keycode key_fret_green;
  SDL_Keycode key_fret_red;
  SDL_Keycode key_fret_yellow;
  SDL_Keycode key_fret_blue;
  SDL_Keycode key_fret_orange;
  SDL_Keycode key_strum;
  double global_offset_ms;  // Global offset for all songs
  int inverted_mode;  // Invert high/low notes and keys
  double lookahead_sec;  // How far ahead notes are visible (in seconds)
} Settings;

void settings_load(Settings *s);
void settings_save(const Settings *s);
void settings_init_defaults(Settings *s);

// Song-specific offset (saved in song.ini)
double song_offset_load(const char *song_dir);
void song_offset_save(const char *song_dir, double offset_ms);

#endif
