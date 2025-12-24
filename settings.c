#include "settings.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SETTINGS_FILE ".midifall_settings"

void settings_init_defaults(Settings *s) {
  s->key_fret_green = KEY_FRET_GREEN;
  s->key_fret_red = KEY_FRET_RED;
  s->key_fret_yellow = KEY_FRET_YELLOW;
  s->key_fret_blue = KEY_FRET_BLUE;
  s->key_fret_orange = KEY_FRET_ORANGE;
  s->key_strum = KEY_STRUM_DOWN;
  s->global_offset_ms = DEFAULT_OFFSET;
}

static const char* get_settings_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s/%s", home, SETTINGS_FILE);
  } else {
    snprintf(path, sizeof(path), "%s", SETTINGS_FILE);
  }
  return path;
}

void settings_load(Settings *s) {
  settings_init_defaults(s);
  
  const char *path = get_settings_path();
  FILE *f = fopen(path, "r");
  if (!f) return;
  
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    int value;
    double dvalue;
    
    if (sscanf(line, "key_fret_green=%d", &value) == 1) {
      s->key_fret_green = (SDL_Keycode)value;
    } else if (sscanf(line, "key_fret_red=%d", &value) == 1) {
      s->key_fret_red = (SDL_Keycode)value;
    } else if (sscanf(line, "key_fret_yellow=%d", &value) == 1) {
      s->key_fret_yellow = (SDL_Keycode)value;
    } else if (sscanf(line, "key_fret_blue=%d", &value) == 1) {
      s->key_fret_blue = (SDL_Keycode)value;
    } else if (sscanf(line, "key_fret_orange=%d", &value) == 1) {
      s->key_fret_orange = (SDL_Keycode)value;
    } else if (sscanf(line, "key_strum=%d", &value) == 1) {
      s->key_strum = (SDL_Keycode)value;
    } else if (sscanf(line, "global_offset_ms=%lf", &dvalue) == 1) {
      s->global_offset_ms = dvalue;
    } else if (sscanf(line, "offset_ms=%lf", &dvalue) == 1) {
      // Legacy compatibility
      s->global_offset_ms = dvalue;
    }
  }
  
  fclose(f);
}

void settings_save(const Settings *s) {
  const char *path = get_settings_path();
  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Warning: Could not save settings to %s\n", path);
    return;
  }
  
  fprintf(f, "key_fret_green=%d\n", s->key_fret_green);
  fprintf(f, "key_fret_red=%d\n", s->key_fret_red);
  fprintf(f, "key_fret_yellow=%d\n", s->key_fret_yellow);
  fprintf(f, "key_fret_blue=%d\n", s->key_fret_blue);
  fprintf(f, "key_fret_orange=%d\n", s->key_fret_orange);
  fprintf(f, "key_strum=%d\n", s->key_strum);
  fprintf(f, "global_offset_ms=%.1f\n", s->global_offset_ms);
  
  fclose(f);
}

double song_offset_load(const char *song_dir) {
  char ini_path[512];
  snprintf(ini_path, sizeof(ini_path), "%s/song.ini", song_dir);
  
  FILE *f = fopen(ini_path, "r");
  if (!f) return 0.0;
  
  char line[256];
  double offset = 0.0;
  while (fgets(line, sizeof(line), f)) {
    double value;
    if (sscanf(line, "offset=%lf", &value) == 1) {
      offset = value;
      break;
    }
  }
  
  fclose(f);
  return offset;
}

void song_offset_save(const char *song_dir, double offset_ms) {
  char ini_path[512];
  snprintf(ini_path, sizeof(ini_path), "%s/song.ini", song_dir);
  
  // Read existing ini file
  char content[4096] = {0};
  int has_offset = 0;
  
  FILE *f = fopen(ini_path, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "offset=", 7) == 0) {
        // Replace offset line
        int len = strlen(content);
        snprintf(content + len, sizeof(content) - len, "offset=%.1f\n", offset_ms);
        has_offset = 1;
      } else {
        // Keep other lines
        strncat(content, line, sizeof(content) - strlen(content) - 1);
      }
    }
    fclose(f);
  }
  
  // If no offset line found, add it
  if (!has_offset) {
    int len = strlen(content);
    snprintf(content + len, sizeof(content) - len, "offset=%.1f\n", offset_ms);
  }
  
  // Write back
  f = fopen(ini_path, "w");
  if (f) {
    fputs(content, f);
    fclose(f);
  }
}
