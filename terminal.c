#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "terminal.h"
#include "config.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Helper to get single-char representation of a key
static char key_char(SDL_Keycode key) {
  if (key >= SDLK_a && key <= SDLK_z) {
    return 'a' + (char)(key - SDLK_a);
  }
  if (key >= SDLK_0 && key <= SDLK_9) {
    return '0' + (char)(key - SDLK_0);
  }
  return '?';
}

const char *lane_color(int lane) {
  switch (lane) {
  case 0:
    return COLOR_GREEN;
  case 1:
    return COLOR_RED;
  case 2:
    return COLOR_YELLOW;
  case 3:
    return COLOR_BLUE;
  case 4:
    return COLOR_ORANGE;
  default:
    return COLOR_RESET;
  }
}

static struct termios g_old_term;

void term_raw_on(void) {
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &g_old_term) != 0)
    return;
  t = g_old_term;
  t.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void term_raw_off(void) { tcsetattr(STDIN_FILENO, TCSANOW, &g_old_term); }

void get_term_size(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 &&
      ws.ws_col > 0) {
    *rows = (int)ws.ws_row;
    *cols = (int)ws.ws_col;
  } else {
    *rows = DEFAULT_TERM_ROWS;
    *cols = DEFAULT_TERM_COLS;
  }
}

double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void clear_screen_hide_cursor(void) {
  printf("\x1b[2J\x1b[1;1H\x1b[?25l"); // Clear, position at row 1, hide cursor
}

void show_cursor(void) { printf("\x1b[?25h\x1b[0m\n"); }

static Effect g_effects[MAX_EFFECTS];
static int g_effect_count = 0;

void add_effect(int lane, int type, double duration) {
  for (int i = 0; i < g_effect_count; i++) {
    if (g_effects[i].lane == lane) {
      g_effects[i].type = type;
      g_effects[i].time_left = duration;
      return;
    }
  }

  if (g_effect_count < MAX_EFFECTS) {
    g_effects[g_effect_count].lane = lane;
    g_effects[g_effect_count].type = type;
    g_effects[g_effect_count].time_left = duration;
    g_effect_count++;
  }
}

void update_effects(double dt) {
  int write_idx = 0;
  for (int read_idx = 0; read_idx < g_effect_count; read_idx++) {
    g_effects[read_idx].time_left -= dt;
    if (g_effects[read_idx].time_left > 0) {
      if (write_idx != read_idx) {
        g_effects[write_idx] = g_effects[read_idx];
      }
      write_idx++;
    }
  }
  g_effect_count = write_idx;
}
void draw_frame(const ChordVec *chords, size_t cursor, double t,
                double lookahead, uint8_t held_mask, const Stats *st,
                double song_offset_ms, double global_offset_ms,
                int selected_track,
                const TrackNameVec *track_names,
                const char *timing_feedback) {
  int rows, cols;
  get_term_size(&rows, &cols);
  int h = rows - 3; // Account for: 1 stats line, 1 empty line, bottom margin
  if (h < 10)
    h = 10;

  const int lanes = 5;
  const int lane_w = 3; // [#]
  int grid_w = lanes * lane_w;
  int x0 = (cols - grid_w) / 2;
  if (x0 < 0)
    x0 = 0;

  // buffer
  char *screen = (char *)malloc((size_t)rows * (size_t)(cols + 1));
  if (!screen)
    return;
  for (int r = 0; r < rows; r++) {
    memset(screen + (size_t)r * (size_t)(cols + 1), ' ', (size_t)cols);
    screen[(size_t)r * (size_t)(cols + 1) + (size_t)cols] = '\0';
  }

  // Row 1: Stats line with both offsets
  int total_notes = st->hit + st->miss;
  char statsline[512];
  snprintf(statsline, sizeof(statsline),
           "t=%.3fs  GlobalOffset: %.1fms  SongOffset: %.1fms  Score: \x1b[93m%d\x1b[0m  Streak: %d  Hit: %d/%d",
           t, global_offset_ms, song_offset_ms, st->score, st->streak, st->hit, total_notes);
  int hl = (int)strlen(statsline);
  if (hl > cols)
    hl = cols;
  memcpy(screen + 1 * (cols + 1), statsline, (size_t)hl); // Row 1

  int top_y = 3; // Leave row 2 empty (between stats and lanes)
  int hit_y = top_y + h;
  if (hit_y >= rows - 1) // Just leave bottom row empty
    hit_y = rows - 2;

  // Streak multiplier bar on the left side (vertical bar)
  int multiplier = 1 + st->streak / STREAK_DIVISOR;
  if (multiplier > MAX_MULTIPLIER)
    multiplier = MAX_MULTIPLIER;
  
  // Calculate bar height based on streak progress (not just multiplier)
  int bar_height = hit_y - top_y;
  int max_streak_for_max_mult = (MAX_MULTIPLIER - 1) * STREAK_DIVISOR;
  int current_streak = st->streak;
  if (current_streak > max_streak_for_max_mult)
    current_streak = max_streak_for_max_mult;
  
  int filled_height = (bar_height * current_streak) / max_streak_for_max_mult;
  if (filled_height < 1 && current_streak > 0) filled_height = 1;  // Show at least 1 row if there's any streak
  
  // Draw vertical bar at x=0 and x=1
  for (int y = top_y; y <= hit_y && y < rows; y++) {
    int dy = hit_y - y; // distance from bottom
    char bar_char;
    
    if (dy < filled_height) {
      bar_char = '#';  // Filled portion
    } else {
      bar_char = '.';  // Empty portion
    }
    
    screen[(size_t)y * (size_t)(cols + 1) + 0] = bar_char;
    screen[(size_t)y * (size_t)(cols + 1) + 1] = bar_char;
  }
  
  // Add multiplier text at bottom of bar
  if (hit_y + 1 < rows) {
    char mult_text[8];
    snprintf(mult_text, sizeof(mult_text), "%dx", multiplier);
    size_t mult_pos = (size_t)(hit_y + 1) * (size_t)(cols + 1);
    for (size_t i = 0; i < strlen(mult_text) && i < 3; i++) {
      screen[mult_pos + i] = mult_text[i];
    }
  }

  // hit line
  for (int x = x0; x < x0 + grid_w && x < cols; x++) {
    screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)x] = '-';
  }

  // Colored character tracking
  typedef struct {
    int lane;
    size_t pos;
  } ColoredChar;
  ColoredChar
      colored[8192]; // arbitrary max colored chars (increased for guides)
  int colored_count = 0;

  // Color the streak bar - entire bar matches current multiplier color
  for (int y = top_y; y <= hit_y && y < rows; y++) {
    int dy = hit_y - y;  // Distance from bottom
    int bar_lane_code;
    
    if (dy < filled_height) {
      // Entire filled bar uses current multiplier color
      if (multiplier >= 4) {
        bar_lane_code = -23;  // 4x - yellow
      } else if (multiplier == 3) {
        bar_lane_code = -22;  // 3x - magenta
      } else if (multiplier == 2) {
        bar_lane_code = -21;  // 2x - green
      } else {
        bar_lane_code = -24;  // 1x - blue
      }
    } else {
      bar_lane_code = -20;  // Empty - gray
    }
    
    // Add both x=0 and x=1 positions
    if (colored_count < 8192) {
      colored[colored_count].lane = bar_lane_code;
      colored[colored_count].pos = (size_t)y * (size_t)(cols + 1) + 0;
      colored_count++;
    }
    if (colored_count < 8192) {
      colored[colored_count].lane = bar_lane_code;
      colored[colored_count].pos = (size_t)y * (size_t)(cols + 1) + 1;
      colored_count++;
    }
  }

  // Check for any active effects (to display graphical feedback on sides)
  int active_effects[5] = {-1, -1, -1, -1, -1};
  for (int e = 0; e < g_effect_count; e++) {
    if (g_effects[e].lane >= 0 && g_effects[e].lane < 5) {
      active_effects[g_effects[e].lane] = g_effects[e].type;
    }
  }
  
  // Draw graphical feedback on LEFT side of lanes
  int left_x = x0 - 6;  // 6 chars to the left
  if (left_x >= 0 && hit_y >= 0 && hit_y < rows) {
    for (int e = 0; e < g_effect_count; e++) {
      int effect_type = g_effects[e].type;
      
      if (effect_type == 0) {
        // Miss - X marks and lines
        if (left_x + 4 < cols) {
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 0)] = 'X';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 1)] = 'X';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 2)] = 'X';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 3)] = 'X';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 4)] = 'X';
          
          for (int i = 0; i < 5; i++) {
            if (colored_count < 8192) {
              colored[colored_count].lane = -10;  // Miss color
              colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + i);
              colored_count++;
            }
          }
        }
      } else if (effect_type == 3) {
        // Perfect - burst animation
        if (left_x + 4 < cols) {
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 0)] = '=';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 1)] = '=';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 2)] = '*';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 3)] = '=';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 4)] = '=';
          
          for (int i = 0; i < 5; i++) {
            if (colored_count < 8192) {
              colored[colored_count].lane = -13;  // Perfect color
              colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + i);
              colored_count++;
            }
          }
        }
      } else if (effect_type == 2) {
        // Good - star burst
        if (left_x + 4 < cols) {
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 0)] = '-';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 1)] = '-';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 2)] = '*';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 3)] = '-';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 4)] = '-';
          
          for (int i = 0; i < 5; i++) {
            if (colored_count < 8192) {
              colored[colored_count].lane = -12;  // Good color
              colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + i);
              colored_count++;
            }
          }
        }
      } else if (effect_type == 1) {
        // OK - small burst
        if (left_x + 4 < cols) {
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 0)] = '.';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 1)] = '.';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 2)] = 'o';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 3)] = '.';
          screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + 4)] = '.';
          
          for (int i = 0; i < 5; i++) {
            if (colored_count < 8192) {
              colored[colored_count].lane = -11;  // OK color
              colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(left_x + i);
              colored_count++;
            }
          }
        }
      }
      break;  // Only show one effect at a time
    }
  }
  
  // Draw graphical feedback on RIGHT side of lanes
  int right_x = x0 + grid_w + 2;  // 2 char after lanes
  if (right_x + 4 < cols && hit_y >= 0 && hit_y < rows) {
    for (int e = 0; e < g_effect_count; e++) {
      int effect_type = g_effects[e].type;
      
      if (effect_type == 0) {
        // Miss - X marks
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 0)] = 'X';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 1)] = 'X';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 2)] = 'X';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 3)] = 'X';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 4)] = 'X';
        
        for (int i = 0; i < 5; i++) {
          if (colored_count < 8192) {
            colored[colored_count].lane = -10;  // Miss color
            colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + i);
            colored_count++;
          }
        }
      } else if (effect_type == 3) {
        // Perfect - burst animation
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 0)] = '=';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 1)] = '=';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 2)] = '*';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 3)] = '=';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 4)] = '=';
        
        for (int i = 0; i < 5; i++) {
          if (colored_count < 8192) {
            colored[colored_count].lane = -13;  // Perfect color
            colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + i);
            colored_count++;
          }
        }
      } else if (effect_type == 2) {
        // Good - star burst
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 0)] = '-';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 1)] = '-';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 2)] = '*';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 3)] = '-';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 4)] = '-';
        
        for (int i = 0; i < 5; i++) {
          if (colored_count < 8192) {
            colored[colored_count].lane = -12;  // Good color
            colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + i);
            colored_count++;
          }
        }
      } else if (effect_type == 1) {
        // OK - small burst
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 0)] = '.';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 1)] = '.';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 2)] = 'o';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 3)] = '.';
        screen[(size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + 4)] = '.';
        
        for (int i = 0; i < 5; i++) {
          if (colored_count < 8192) {
            colored[colored_count].lane = -11;  // OK color
            colored[colored_count].pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(right_x + i);
            colored_count++;
          }
        }
      }
      break;  // Only show one effect at a time
    }
  }

  // Fret buttons at hit line - show when frets are pressed (no effects here)
  for (int l = 0; l < lanes; l++) {
    int x = x0 + l * lane_w;
    if (x + 2 < cols && hit_y >= 0 && hit_y < rows) {
      size_t pos0 = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(x + 0);
      size_t pos1 = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(x + 1);
      size_t pos2 = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(x + 2);

      if (held_mask & (1u << l)) {
        // Fret is pressed - show filled button with bright indicator
        screen[pos0] = '<';
        screen[pos1] = 'O'; // Capital O to indicate pressed
        screen[pos2] = '>';
      } else {
        // Fret not pressed - show empty button
        screen[pos0] = '[';
        screen[pos1] = ' ';
        screen[pos2] = ']';
      }

      // Store positions for coloring
      if (colored_count < 8192 - 3) {
        colored[colored_count].lane = l;
        colored[colored_count].pos = pos0;
        colored_count++;

        colored[colored_count].lane = l;
        colored[colored_count].pos = pos1;
        colored_count++;

        colored[colored_count].lane = l;
        colored[colored_count].pos = pos2;
        colored_count++;
      }
    }
  }

  // Add timing feedback next to fret line
  if (timing_feedback && timing_feedback[0] != '\0') {
    int feedback_x = x0 + grid_w + 3;  // 3 spaces after fret buttons
    const char *feedback_str = timing_feedback;
    int feedback_len = (int)strlen(feedback_str);
    
    for (int i = 0; i < feedback_len && feedback_x + i < cols; i++) {
      size_t pos = (size_t)hit_y * (size_t)(cols + 1) + (size_t)(feedback_x + i);
      screen[pos] = feedback_str[i];
      
      // Color the feedback (red for late, yellow for early)
      if (colored_count < 8192) {
        if (strstr(timing_feedback, "LATE")) {
          colored[colored_count].lane = -10;  // Red (miss color)
        } else {
          colored[colored_count].lane = -12;  // Yellow (good color) 
        }
        colored[colored_count].pos = pos;
        colored_count++;
      }
    }
  }

  // Add a line below the fret buttons for better visual separation
  if (hit_y + 1 < rows - 1) { // Just need room for bottom edge
    for (int x = x0; x < x0 + grid_w && x < cols; x++) {
      screen[(size_t)(hit_y + 1) * (size_t)(cols + 1) + (size_t)x] = '=';
    }
  }

  // lane guides (with colors) - store positions for coloring

  for (int y = top_y; y < hit_y; y++) {
    for (int l = 0; l < lanes; l++) {
      int x = x0 + l * lane_w;
      if (x >= 0 && x < cols) {
        size_t pos = (size_t)y * (size_t)(cols + 1) + (size_t)x;
        screen[pos] = '|';
        // Store position and lane for coloring the guide
        if (colored_count < 8192) {
          colored[colored_count].lane = l;
          colored[colored_count].pos = pos;
          colored_count++;
        }
      }
    }
  }

  // notes within lookahead - mark them and store for coloring
  for (size_t k = cursor; k < chords->n; k++) {
    double dt = chords->v[k].t_sec - t;
    double duration = chords->v[k].duration_sec;
    
    // Check if either the note head OR the sustain end is visible
    double sustain_end_time = chords->v[k].t_sec + duration;
    double sustain_dt = sustain_end_time - t;
    
    // Skip if note is too far past AND sustain has ended
    if (dt < -0.3 && sustain_dt < -0.3)
      continue;
    // Skip if BOTH note head AND sustain end haven't appeared yet
    if (dt > lookahead && sustain_dt > lookahead)
      break;

    double frac = 1.0 - (dt / lookahead);
    int y = top_y + (int)(frac * (double)(h - 1));
    // Allow note head to be positioned off-screen above
    if (y < top_y)
      y = top_y;
    if (y > hit_y - 1)
      y = hit_y - 1;

    uint8_t m = chords->v[k].mask;
    uint8_t is_hopo = chords->v[k].is_hopo;
    
    // Draw sustain trails first (if duration > 0)
    if (duration > 0.01) {  // Only draw trail if sustain is > 10ms
      // Draw trail if any part of the sustain is visible
      // (either note head is visible, or sustain hasn't ended yet)
      if (sustain_dt >= -0.3) {
        // Calculate sustain end position (may be off-screen above)
        double sustain_frac = 1.0 - (sustain_dt / lookahead);
        int sustain_y = top_y + (int)(sustain_frac * (double)(h - 1));
        
        // Clamp sustain end to top of screen if it's beyond lookahead
        if (sustain_y < top_y)
          sustain_y = top_y;
        if (sustain_y > hit_y)
          sustain_y = hit_y;
        
        // Trail goes FROM sustain end (top) TO note head (bottom)
        // sustain_y is closer to top (smaller), y is closer to bottom (larger)
        int trail_start = sustain_y;   // Start at sustain end (top)
        int trail_end = y;              // End at note head (bottom)
        
        // If note head hasn't appeared yet (still above screen), end at top
        if (dt > lookahead) {
          trail_end = top_y;
        }
        // If note head is below hit line, end at hit line
        else if (y > hit_y) {
          trail_end = hit_y;
        }
        
        // Draw trail from sustain end down to note head
        for (int l = 0; l < lanes; l++) {
          if (m & (1u << l)) {
            int x = x0 + l * lane_w + 1;  // Center of lane
            
            // Draw trail from sustain end (top) to note head (bottom)
            for (int trail_y = trail_start; trail_y <= trail_end; trail_y++) {
              if (trail_y >= top_y && trail_y <= hit_y && x >= 0 && x < cols) {
                size_t trail_pos = (size_t)trail_y * (size_t)(cols + 1) + (size_t)x;
                screen[trail_pos] = '|';  // Trail character
                
                // Color the trail
                if (colored_count < 8192) {
                  colored[colored_count].lane = l;
                  colored[colored_count].pos = trail_pos;
                  colored_count++;
                }
              }
            }
          }
        }
      }
    }
    
    // Draw note heads on top of trails
    for (int l = 0; l < lanes; l++) {
      if (m & (1u << l)) {
        int x = x0 + l * lane_w;
        if (x + 2 < cols && y >= 0 && y < rows) {
          size_t pos0 = (size_t)y * (size_t)(cols + 1) + (size_t)(x + 0);
          size_t pos1 = (size_t)y * (size_t)(cols + 1) + (size_t)(x + 1);
          size_t pos2 = (size_t)y * (size_t)(cols + 1) + (size_t)(x + 2);
          
          // HOPO notes use <*> instead of [#]
          if (is_hopo) {
            screen[pos0] = '<';
            screen[pos1] = '*';
            screen[pos2] = '>';
          } else {
            screen[pos0] = '[';
            screen[pos1] = '#';
            screen[pos2] = ']';
          }

          // Store positions and lane for coloring (notes are brighter/bolder)
          if (colored_count < 8192 - 3) {
            colored[colored_count].lane = l;
            colored[colored_count].pos = pos0;
            colored_count++;
            colored[colored_count].lane = l;
            colored[colored_count].pos = pos1;
            colored_count++;
            colored[colored_count].lane = l;
            colored[colored_count].pos = pos2;
            colored_count++;
          }
        }
      }
    }
  }

  // blit with colors
  // Position cursor at row 1
  printf("\x1b[1;1H"); // Position at row 1, column 1

  // Print all rows with color lookup (starting from row 1 to skip row 0)
  for (int r = 1; r < rows; r++) {
    size_t row_start = (size_t)r * (size_t)(cols + 1);
    int col = 0;
    while (col < cols) {
      // Check if this position needs coloring
      int lane = -1;
      for (int i = 0; i < colored_count; i++) {
        if (colored[i].pos == row_start + (size_t)col) {
          lane = colored[i].lane;
          break;
        }
      }

      if (lane >= -13 && lane <= -10) {
        // Effect coloring (-10 to -13)
        int effect_type = -10 - lane;
        const char *color;
        if (effect_type == 0) {
          color = "\x1b[1;31m"; // Miss - bright red
        } else if (effect_type == 3) {
          color = "\x1b[1;33m"; // Perfect - bright yellow
        } else if (effect_type == 2) {
          color = "\x1b[1;32m"; // Good - bright green
        } else {
          color = "\x1b[1;36m"; // OK - bright cyan
        }
        printf("%s%c" COLOR_RESET, color, screen[row_start + (size_t)col]);
      } else if (lane == -20) {
        // Empty streak bar - gray
        printf("\x1b[2;37m%c" COLOR_RESET, screen[row_start + (size_t)col]);
      } else if (lane == -24) {
        // 1x multiplier - blue
        printf("\x1b[1;34m%c" COLOR_RESET, screen[row_start + (size_t)col]);
      } else if (lane == -21) {
        // 2x multiplier - green
        printf("\x1b[1;32m%c" COLOR_RESET, screen[row_start + (size_t)col]);
      } else if (lane == -22) {
        // 3x multiplier - magenta
        printf("\x1b[1;35m%c" COLOR_RESET, screen[row_start + (size_t)col]);
      } else if (lane == -23) {
        // 4x multiplier - yellow
        printf("\x1b[1;33m%c" COLOR_RESET, screen[row_start + (size_t)col]);
      } else if (lane >= 0) {
        // Print colored character
        printf("%s%c" COLOR_RESET, lane_color(lane),
               screen[row_start + (size_t)col]);
      } else {
        // Print normal character
        putchar(screen[row_start + (size_t)col]);
      }
      col++;
    }
    putchar('\n');
  }
  fflush(stdout);
  free(screen);
}
