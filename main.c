#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "audio.h"
#include "midi.h"
#include "terminal.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char path[512];
  char title[128];
  char artist[128];
  char year[16];
} SongEntry;

// Parse song.ini file for metadata
static int parse_song_ini(const char *ini_path, char *title, char *artist, char *year) {
  FILE *f = fopen(ini_path, "r");
  if (!f) return 0;
  
  title[0] = '\0';
  artist[0] = '\0';
  year[0] = '\0';
  
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    // Trim whitespace
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    
    if (strncmp(p, "name", 4) == 0) {
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        strncpy(title, eq, 127);
        title[127] = '\0';
        // Remove newline
        char *nl = strchr(title, '\n');
        if (nl) *nl = '\0';
      }
    } else if (strncmp(p, "artist", 6) == 0) {
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        strncpy(artist, eq, 127);
        artist[127] = '\0';
        char *nl = strchr(artist, '\n');
        if (nl) *nl = '\0';
        
        // Remove "(WaveGroup)" and similar tags
        char *paren = strstr(artist, "(WaveGroup)");
        if (paren) {
          // Remove everything from opening paren to closing paren
          char *start = paren;
          while (start > artist && (*(start-1) == ' ' || *(start-1) == '\t')) {
            start--;  // Also remove leading spaces
          }
          *start = '\0';
        }
      }
    } else if (strncmp(p, "year", 4) == 0) {
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        strncpy(year, eq, 15);
        year[15] = '\0';
        char *nl = strchr(year, '\n');
        if (nl) *nl = '\0';
      }
    }
  }
  
  fclose(f);
  return (title[0] != '\0' || artist[0] != '\0');
}

// Parse hopo_frequency from song.ini
static int parse_hopo_from_ini(const char *song_dir) {
  char ini_path[512];
  snprintf(ini_path, sizeof(ini_path), "%s/song.ini", song_dir);
  
  FILE *f = fopen(ini_path, "r");
  if (!f) return 170;  // Default HOPO threshold
  
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    
    if (strncmp(p, "hopo_frequency", 14) == 0) {
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        int freq = atoi(eq);
        fclose(f);
        return (freq > 0) ? freq : 170;
      }
    }
  }
  
  fclose(f);
  return 170;  // Default
}

// Scan Songs directory for valid songs
static int scan_songs_directory(const char *songs_dir, SongEntry **out_songs) {
  DIR *dir = opendir(songs_dir);
  if (!dir) {
    fprintf(stderr, "Cannot open directory: %s\n", songs_dir);
    return 0;
  }
  
  SongEntry *songs = NULL;
  int count = 0;
  int capacity = 0;
  
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    
    char song_path[512];
    snprintf(song_path, sizeof(song_path), "%s/%s", songs_dir, ent->d_name);
    
    struct stat st;
    if (stat(song_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    
    // Check for required files
    char ini_path[512], mid_path[512];
    snprintf(ini_path, sizeof(ini_path), "%s/song.ini", song_path);
    snprintf(mid_path, sizeof(mid_path), "%s/notes.mid", song_path);
    
    int has_ini = (access(ini_path, R_OK) == 0);
    int has_mid = (access(mid_path, R_OK) == 0);
    
    // Check for at least one .opus file
    int has_opus = 0;
    DIR *song_dir = opendir(song_path);
    if (song_dir) {
      struct dirent *file;
      while ((file = readdir(song_dir)) != NULL) {
        size_t len = strlen(file->d_name);
        if (len > 5 && strcmp(file->d_name + len - 5, ".opus") == 0) {
          has_opus = 1;
          break;
        }
      }
      closedir(song_dir);
    }
    
    if (!has_ini || !has_mid || !has_opus) continue;
    
    // Allocate song entry
    if (count >= capacity) {
      capacity = capacity ? capacity * 2 : 16;
      SongEntry *new_songs = realloc(songs, capacity * sizeof(SongEntry));
      if (!new_songs) {
        free(songs);
        closedir(dir);
        return 0;
      }
      songs = new_songs;
    }
    
    strncpy(songs[count].path, song_path, 511);
    songs[count].path[511] = '\0';
    
    // Parse metadata
    if (!parse_song_ini(ini_path, songs[count].title, songs[count].artist, songs[count].year)) {
      // Use folder name as fallback
      strncpy(songs[count].title, ent->d_name, 127);
      songs[count].title[127] = '\0';
      songs[count].artist[0] = '\0';
      songs[count].year[0] = '\0';
    }
    
    count++;
  }
  
  closedir(dir);
  *out_songs = songs;
  return count;
}

// Display song selector and return selected index (-1 if quit)
static int show_song_selector(SongEntry *songs, int count) {
  int selected = 0;
  
  // Set terminal to raw mode for immediate key reading
  struct termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  new_term.c_cc[VMIN] = 1;
  new_term.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
  
  while (1) {
    // Clear screen and show menu
    printf("\x1b[2J\x1b[1;1H");
    printf("\x1b[1;36m╔════════════════════════════════════════════════════════════════════════════╗\x1b[0m\n");
    printf("\x1b[1;36m║                           SONG SELECTOR                                    ║\x1b[0m\n");
    printf("\x1b[1;36m╠════════════════════════════════════════════════════════════════════════════╣\x1b[0m\n");
    
    // Show songs (with scrolling if needed)
    int start = (selected > 10) ? selected - 10 : 0;
    int end = (start + 20 < count) ? start + 20 : count;
    
    for (int i = start; i < end; i++) {
      printf("\x1b[1;36m║\x1b[0m");
      if (i == selected) {
        printf("\x1b[1;33m► ");  // Yellow arrow for selected
      } else {
        printf("  ");
      }
      
      // Print content
      printf("\x1b[1;37m%-38s\x1b[0m  \x1b[36m%-24s\x1b[0m  \x1b[33m%-4s\x1b[0m", 
             songs[i].title, songs[i].artist, songs[i].year);
      // Position cursor at column 78 and print right wall
      printf("\x1b[78G\x1b[1;36m║\x1b[0m\n");
    }
    
    printf("\x1b[1;36m╚════════════════════════════════════════════════════════════════════════════╝\x1b[0m\n");
    printf("\x1b[37mUse \x1b[1;32m↑/↓\x1b[0;37m to select, \x1b[1;32mENTER\x1b[0;37m to play, \x1b[1;31mq/ESC\x1b[0;37m to quit\x1b[0m\n");
    fflush(stdout);
    
    // Read key
    char c = getchar();
    
    if (c == 27) {  // ESC or arrow key
      char next = getchar();
      if (next == '[') {  // Arrow key sequence
        char arrow = getchar();
        if (arrow == 'A') {  // Up arrow
          selected = (selected > 0) ? selected - 1 : count - 1;
        } else if (arrow == 'B') {  // Down arrow
          selected = (selected < count - 1) ? selected + 1 : 0;
        }
      } else {
        // Just ESC - quit
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        return -1;
      }
    } else if (c == '\n' || c == '\r') {  // Enter
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return selected;
    } else if (c == 'q' || c == 'Q') {  // q to quit
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return -1;
    }
  }
}

// Display difficulty selector and return selected difficulty (-1 if quit/back)
static int show_difficulty_selector(void) {
  const char *difficulties[] = {"Easy", "Medium", "Hard", "Expert"};
  const char *colors[] = {"\x1b[1;32m", "\x1b[1;33m", "\x1b[1;31m", "\x1b[1;35m"};  // Green, Yellow, Red, Magenta
  int selected = 3;  // Default to Expert
  
  // Set terminal to raw mode
  struct termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  new_term.c_cc[VMIN] = 1;
  new_term.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
  
  while (1) {
    // Clear screen and show menu
    printf("\x1b[2J\x1b[1;1H");
    printf("\x1b[1;36m╔════════════════════════════════════════════════════════════════════════════╗\x1b[0m\n");
    printf("\x1b[1;36m║                        DIFFICULTY SELECTOR                                 ║\x1b[0m\n");
    printf("\x1b[1;36m╠════════════════════════════════════════════════════════════════════════════╣\x1b[0m\n");
    printf("\x1b[1;36m║                                                                            ║\x1b[0m\n");
    
    for (int i = 0; i < 4; i++) {
      printf("\x1b[1;36m║\x1b[0m");
      if (i == selected) {
        printf("                             \x1b[1;33m► %s%-8s\x1b[0m ◄", colors[i], difficulties[i]);
      } else {
        printf("                               %s%-8s\x1b[0m", colors[i], difficulties[i]);
      }
      printf("\x1b[78G\x1b[1;36m║\x1b[0m\n");
    }
    
    printf("\x1b[1;36m║                                                                            ║\x1b[0m\n");
    printf("\x1b[1;36m╚════════════════════════════════════════════════════════════════════════════╝\x1b[0m\n");
    printf("\x1b[37mUse \x1b[1;32m↑/↓\x1b[0;37m to select, \x1b[1;32mENTER\x1b[0;37m to continue, \x1b[1;31mq/ESC\x1b[0;37m to go back\x1b[0m\n");
    fflush(stdout);
    
    // Read key
    char c = getchar();
    
    if (c == 27) {  // ESC or arrow key
      char next = getchar();
      if (next == '[') {  // Arrow key sequence
        char arrow = getchar();
        if (arrow == 'A') {  // Up arrow
          selected = (selected > 0) ? selected - 1 : 3;
        } else if (arrow == 'B') {  // Down arrow
          selected = (selected < 3) ? selected + 1 : 0;
        }
      } else {
        // Just ESC - go back
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        return -1;
      }
    } else if (c == '\n' || c == '\r') {  // Enter
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return selected;
    } else if (c == 'q' || c == 'Q') {  // q to go back
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return -1;
    }
  }
}

typedef struct {
  char name[32];
  float gain;
} GainEntry;

typedef struct {
  char mutes[16][32];
  int mute_count;
  GainEntry gains[16];
  int gain_count;
  
  char *midi_path;
  char *opus_paths[16];
  int opus_count;
  
  int hopo_frequency;  // HOPO threshold in ticks
  
  const char *diff_str;
  double offset_ms;
  double lookahead;
  int needs_free;
} Args;

static const char *diff_name(int d) {
  switch (d) {
  case 0: return "easy";
  case 1: return "medium";
  case 2: return "hard";
  case 3: return "expert";
  default: return "unknown";
  }
}

static int parse_diff(const char *s) {
  if (!s) return -1;
  if (strcmp(s, "easy") == 0) return 0;
  if (strcmp(s, "medium") == 0) return 1;
  if (strcmp(s, "hard") == 0) return 2;
  if (strcmp(s, "expert") == 0) return 3;
  return -1;
}

static int choose_best_diff_present(const NoteVec *notes) {
  int best = -1;
  for (size_t i = 0; i < notes->n; i++)
    if (notes->v[i].diff > best)
      best = notes->v[i].diff;
  return best;
}

static int find_max_track(const NoteVec *notes) {
  int max_track = 0;
  for (size_t i = 0; i < notes->n; i++) {
    if (notes->v[i].track > max_track)
      max_track = notes->v[i].track;
  }
  return max_track;
}

static float gain_for(const Args *a, const char *stem_name) {
  for (int i = 0; i < a->gain_count; i++) {
    if (strcmp(a->gains[i].name, stem_name) == 0)
      return a->gains[i].gain;
  }
  return 1.0f;
}

static int muted(const Args *a, const char *stem_name) {
  for (int i = 0; i < a->mute_count; i++) {
    if (strcmp(a->mutes[i], stem_name) == 0)
      return 1;
  }
  return 0;
}

static void scan_directory(const char *dir_path, char **out_midi, char **out_opus, int *out_opus_count) {
  DIR *dir = opendir(dir_path);
  if (!dir) {
    fprintf(stderr, "Cannot open directory: %s\n", dir_path);
    exit(1);
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;
    
    char *full_path = (char *)malloc(strlen(dir_path) + strlen(entry->d_name) + 2);
    sprintf(full_path, "%s/%s", dir_path, entry->d_name);
    
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
      size_t len = strlen(entry->d_name);
      if (len > 4 && strcmp(entry->d_name + len - 4, ".mid") == 0) {
        *out_midi = full_path;
      } else if (len > 5 && strcmp(entry->d_name + len - 5, ".opus") == 0) {
        if (*out_opus_count < 16) {
          out_opus[(*out_opus_count)++] = full_path;
        } else {
          fprintf(stderr, "Warning: Too many .opus files (max 16), skipping: %s\n", entry->d_name);
          free(full_path);
        }
      } else {
        free(full_path);
      }
    } else {
      free(full_path);
    }
  }
  closedir(dir);
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s <folder>  [options]\n", prog);
  fprintf(stderr, "  %s <file.mid> --opus <file1.opus> [--opus <file2.opus> ...] [options]\n\n", prog);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --difficulty <easy|medium|hard|expert>\n");
  fprintf(stderr, "  --offset-ms <N>\n");
  fprintf(stderr, "  --lookahead-sec <S>\n");
  fprintf(stderr, "  --gain <name=val>\n");
  fprintf(stderr, "  --mute <name>\n");
  exit(1);
}

static void parse_args(int argc, char **argv, Args *a) {
  memset(a, 0, sizeof(*a));
  a->diff_str = DEFAULT_DIFFICULTY;
  a->offset_ms = DEFAULT_OFFSET;
  a->lookahead = DEFAULT_LOOKAHEAD;

  if (argc < 2) usage(argv[0]);

  struct stat st;
  if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
    a->needs_free = 1;
    scan_directory(argv[1], &a->midi_path, a->opus_paths, &a->opus_count);
    
    if (!a->midi_path) {
      fprintf(stderr, "No .mid file found in directory: %s\n", argv[1]);
      exit(1);
    }
    if (a->opus_count == 0) {
      fprintf(stderr, "No .opus files found in directory: %s\n", argv[1]);
      exit(1);
    }
    
    // Parse HOPO frequency from song.ini
    a->hopo_frequency = parse_hopo_from_ini(argv[1]);

    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--difficulty") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->diff_str = argv[++i];
      } else if (strcmp(argv[i], "--offset-ms") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->offset_ms = atof(argv[++i]);
      } else if (strcmp(argv[i], "--lookahead-sec") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->lookahead = atof(argv[++i]);
        if (a->lookahead < MIN_LOOKAHEAD)
          a->lookahead = MIN_LOOKAHEAD;
      } else if (strcmp(argv[i], "--gain") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        if (a->gain_count >= 16) {
          fprintf(stderr, "Too many --gain\n");
          exit(1);
        }
        const char *spec = argv[++i];
        const char *eq = strchr(spec, '=');
        if (!eq) {
          fprintf(stderr, "--gain expects name=val\n");
          exit(1);
        }
        size_t n = (size_t)(eq - spec);
        if (n >= sizeof(a->gains[a->gain_count].name))
          n = sizeof(a->gains[a->gain_count].name) - 1;
        memcpy(a->gains[a->gain_count].name, spec, n);
        a->gains[a->gain_count].name[n] = '\0';
        a->gains[a->gain_count].gain = (float)atof(eq + 1);
        a->gain_count++;
      } else if (strcmp(argv[i], "--mute") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        if (a->mute_count >= 16) {
          fprintf(stderr, "Too many --mute\n");
          exit(1);
        }
        snprintf(a->mutes[a->mute_count++], 32, "%s", argv[++i]);
      } else {
        usage(argv[0]);
      }
    }
  } else {
    a->midi_path = argv[1];
    a->hopo_frequency = 170;  // Default for file mode

    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--opus") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        if (a->opus_count >= 16) {
          fprintf(stderr, "Too many --opus\n");
          exit(1);
        }
        a->opus_paths[a->opus_count++] = argv[++i];
      } else if (strcmp(argv[i], "--difficulty") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->diff_str = argv[++i];
      } else if (strcmp(argv[i], "--offset-ms") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->offset_ms = atof(argv[++i]);
      } else if (strcmp(argv[i], "--lookahead-sec") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        a->lookahead = atof(argv[++i]);
        if (a->lookahead < MIN_LOOKAHEAD)
          a->lookahead = MIN_LOOKAHEAD;
      } else if (strcmp(argv[i], "--gain") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        if (a->gain_count >= 16) {
          fprintf(stderr, "Too many --gain\n");
          exit(1);
        }
        const char *spec = argv[++i];
        const char *eq = strchr(spec, '=');
        if (!eq) {
          fprintf(stderr, "--gain expects name=val\n");
          exit(1);
        }
        size_t n = (size_t)(eq - spec);
        if (n >= sizeof(a->gains[a->gain_count].name))
          n = sizeof(a->gains[a->gain_count].name) - 1;
        memcpy(a->gains[a->gain_count].name, spec, n);
        a->gains[a->gain_count].name[n] = '\0';
        a->gains[a->gain_count].gain = (float)atof(eq + 1);
        a->gain_count++;
      } else if (strcmp(argv[i], "--mute") == 0) {
        if (i + 1 >= argc) usage(argv[0]);
        if (a->mute_count >= 16) {
          fprintf(stderr, "Too many --mute\n");
          exit(1);
        }
        snprintf(a->mutes[a->mute_count++], 32, "%s", argv[++i]);
      } else {
        usage(argv[0]);
      }
    }

    if (a->opus_count == 0) {
      fprintf(stderr, "Need at least one --opus file.\n");
      exit(1);
    }
  }
}

int main(int argc, char **argv) {
  static char *selected_difficulty = NULL;
  
  // Song selector mode if no arguments
  if (argc == 1) {
    SongEntry *songs = NULL;
    int song_count = scan_songs_directory("Songs", &songs);
    
    if (song_count == 0) {
      fprintf(stderr, "No valid songs found in Songs/ directory\n");
      fprintf(stderr, "Each song folder must contain: notes.mid, *.opus, and song.ini\n");
      return 1;
    }
    
    int selected = show_song_selector(songs, song_count);
    
    if (selected < 0) {
      free(songs);
      return 0;  // User quit
    }
    
    // Show difficulty selector
    int diff_choice = show_difficulty_selector();
    
    if (diff_choice < 0) {
      free(songs);
      return 0;  // User went back/quit
    }
    
    // Build persistent argv for selected song and difficulty
    static char prog_name[] = "midifall";
    static char diff_arg_flag[] = "--difficulty";
    static char diff_easy[] = "easy";
    static char diff_medium[] = "medium";
    static char diff_hard[] = "hard";
    static char diff_expert[] = "expert";
    static char *new_argv[5];
    
    const char *diff_strings[] = {diff_easy, diff_medium, diff_hard, diff_expert};
    selected_difficulty = (char *)diff_strings[diff_choice];
    
    new_argv[0] = prog_name;
    new_argv[1] = strdup(songs[selected].path);
    new_argv[2] = diff_arg_flag;
    new_argv[3] = selected_difficulty;
    new_argv[4] = NULL;
    
    free(songs);
    
    // Update argc/argv
    argc = 4;
    argv = new_argv;
  }
  
  SDL_setenv("SDL_VIDEODRIVER", SDL_VIDEO_DRIVER, 1);
  
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    fprintf(stderr, "Make sure WSLg is enabled (wsl --update)\n");
    return 1;
  }
  atexit(SDL_Quit);

  fprintf(stderr, "Creating SDL window for input...\n");
  
  SDL_Window *window = SDL_CreateWindow(
    SDL_WINDOW_TITLE,
    0, 0,  // Top-left corner
    SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT,
    SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS
  );
  
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    fprintf(stderr, "Check that WSLg is working: wsl --update\n");
    return 1;
  }
  
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (renderer) {
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_Rect r = {10, 10, 380, 130};
    SDL_RenderDrawRect(renderer, &r);
    
    SDL_Color white = {255, 255, 255, 255};
    (void)white;
    SDL_RenderPresent(renderer);
  }

  Args args;
  parse_args(argc, argv, &args);

  NoteVec notes = {0};
  TrackNameVec track_names = {0};
  
  fprintf(stderr, "Parsing MIDI: %s\n", args.midi_path);
  midi_parse(args.midi_path, &notes, &track_names);
  
  if (notes.n == 0) {
    fprintf(stderr, "No notes found in MIDI file.\n");
    return 1;
  }

  int diff = parse_diff(args.diff_str);
  if (diff < 0) {
    diff = choose_best_diff_present(&notes);
    if (diff < 0) {
      fprintf(stderr, "No valid difficulty in MIDI\n");
      return 1;
    }
    fprintf(stderr, "Auto-selected difficulty: %s\n", diff_name(diff));
  }

  int max_track = find_max_track(&notes);
  
  // Try to find "PART GUITAR" track as default
  int selected_track = -1;
  for (size_t i = 0; i < track_names.n; i++) {
    if (strstr(track_names.v[i].name, "PART GUITAR") != NULL) {
      selected_track = track_names.v[i].track_num;
      fprintf(stderr, "Auto-selected track: %s (track %d)\n", 
              track_names.v[i].name, selected_track);
      break;
    }
  }
  
  // If PART GUITAR not found, use all tracks
  if (selected_track == -1) {
    fprintf(stderr, "PART GUITAR not found, using all tracks\n");
  }
  
  ChordVec chords = {0};
  build_chords(&notes, diff, selected_track, args.hopo_frequency, &chords);
  
  if (chords.n == 0) {
    fprintf(stderr, "No notes for difficulty %s\n", diff_name(diff));
    return 1;
  }

  fprintf(stderr, "Found %zu chords for difficulty %s\n", chords.n, diff_name(diff));

  AudioEngine aud = {0};
  audio_init(&aud, AUDIO_SAMPLE_RATE);

  fprintf(stderr, "Loading %d Opus files...\n", args.opus_count);
  aud.stems = (Stem *)calloc((size_t)args.opus_count, sizeof(Stem));
  aud.stem_count = args.opus_count;

  for (int i = 0; i < args.opus_count; i++) {
    fprintf(stderr, "  [%d/%d] %s\n", i + 1, args.opus_count, args.opus_paths[i]);
    load_opus_file(args.opus_paths[i], &aud.stems[i]);
    aud.stems[i].gain = gain_for(&args, aud.stems[i].name);
    aud.stems[i].enabled = !muted(&args, aud.stems[i].name);
  }

  fprintf(stderr, "\nPress ENTER to start, or Q/ESC to quit.\n");
  fprintf(stderr, "Focus the SDL window if needed.\n");

  term_raw_on();
  clear_screen_hide_cursor();
  atexit(show_cursor);
  atexit(term_raw_off);

  while (1) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        goto cleanup;
      if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
        if (e.key.keysym.sym == KEY_QUIT2 || e.key.keysym.sym == KEY_QUIT)
          goto cleanup;
        if (e.key.keysym.sym == KEY_START || e.key.keysym.sym == KEY_START2)
          goto start_game;
      }
    }

    if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
      SDL_RaiseWindow(window);
    }

    SDL_Delay(10);
  }

start_game:
  for (int i = 0; i < aud.stem_count; i++)
    aud.stems[i].pos = 0;
  aud.frames_played = 0;
  aud.started = 1;

  double offset_ms = args.offset_ms;
  const double lookahead = args.lookahead;
  const double perfect = TIMING_PERFECT;
  const double good = TIMING_GOOD;
  const double bad = TIMING_BAD;

  uint8_t held = 0;
  uint8_t prev_held = 0;  // Track previous fret state for HOPO detection
  Stats st = {0};
  int paused = 0;  // Pause state
  
  char timing_feedback[32] = "";  // "TOO EARLY" or "TOO LATE"
  double feedback_timer = 0.0;    // How long to show feedback

  size_t cursor = 0;

  const double fps = TARGET_FPS;
  const double dt = 1.0 / fps;
  double next = now_sec();

  while (1) {
    (void)now_sec();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        goto cleanup;

      if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          held = 0;
        }
      }

      if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
        SDL_Keycode key = e.key.keysym.sym;

        if (key == KEY_QUIT || key == KEY_QUIT2)
          goto cleanup;

        if (key == KEY_PAUSE) {
          paused = !paused;
          if (paused) {
            // Stop audio when paused
            aud.started = 0;
          } else {
            // Resume audio
            aud.started = 1;
          }
        }

        // Don't process game input while paused
        if (paused)
          continue;

        uint8_t old_held = held;
        
        if (key == KEY_FRET_GREEN)
          held |= (1u << 0);
        if (key == KEY_FRET_RED)
          held |= (1u << 1);
        if (key == KEY_FRET_YELLOW)
          held |= (1u << 2);
        if (key == KEY_FRET_BLUE)
          held |= (1u << 3);
        if (key == KEY_FRET_ORANGE)
          held |= (1u << 4);

        // Check for HOPO hit on fret change
        if (held != old_held && cursor < chords.n && chords.v[cursor].is_hopo) {
          double offset_sec = offset_ms / 1000.0;
          double t = audio_time_sec(&aud) + offset_sec;
          double delta = chords.v[cursor].t_sec - t;
          double ad = fabs(delta);

          if (ad <= bad) {
            uint8_t expected = chords.v[cursor].mask;
            int match = 0;
            
            // Same matching logic as strum
            int note_count = 0;
            int highest_fret = -1;
            for (int l = 0; l < 5; l++) {
              if (expected & (1u << l)) {
                note_count++;
                highest_fret = l;
              }
            }
            
            if (note_count == 1) {
              int has_required = (held & (1u << highest_fret)) != 0;
              int invalid_higher = 0;
              for (int l = highest_fret + 1; l < 5; l++) {
                if (held & (1u << l)) {
                  invalid_higher = 1;
                  break;
                }
              }
              match = has_required && !invalid_higher;
            } else {
              match = (held == expected);
            }
            
            if (match) {
              st.hit++;
              st.streak++;
              int pts = (ad <= perfect) ? POINTS_PERFECT : (ad <= good ? POINTS_GOOD : POINTS_OK);
              st.score += pts * (1 + st.streak / STREAK_DIVISOR);
              
              int effect_type;
              if (ad <= perfect) {
                effect_type = EFFECT_TYPE_PERFECT;
              } else if (ad <= good) {
                effect_type = EFFECT_TYPE_GOOD;
              } else {
                effect_type = EFFECT_TYPE_OK;
              }
              
              uint8_t m = chords.v[cursor].mask;
              for (int l = 0; l < 5; l++) {
                if (m & (1u << l)) {
                  add_effect(l, effect_type, EFFECT_DURATION_HIT);
                }
              }
              
              cursor++;
            }
          }
        }

        if (key == KEY_CLEAR)
          held = 0;

        if (key == KEY_OFFSET_INC || key == KEY_OFFSET_INC2 || key == KEY_OFFSET_INC3)
          offset_ms += OFFSET_STEP;
        if (key == KEY_OFFSET_DEC || key == KEY_OFFSET_DEC2 || key == KEY_OFFSET_DEC3)
          offset_ms -= OFFSET_STEP;

        if (key >= KEY_TRACK_MIN && key <= KEY_TRACK_MAX) {
          int new_track = (int)(key - SDLK_0);
          if (new_track <= max_track && new_track != selected_track) {
            ChordVec new_chords = {0};
            build_chords(&notes, diff, new_track, args.hopo_frequency, &new_chords);

            if (new_chords.n > 0) {
              selected_track = new_track;
              free(chords.v);
              chords = new_chords;
              cursor = 0;
              st.score = 0;
              st.streak = 0;
              st.hit = 0;
              st.miss = 0;
            } else {
              free(new_chords.v);
            }
          }
        }

        if (key == KEY_TRACK_ALL) {
          if (selected_track != -1) {
            ChordVec new_chords = {0};
            build_chords(&notes, diff, -1, args.hopo_frequency, &new_chords);

            if (new_chords.n > 0) {
              selected_track = -1;
              free(chords.v);
              chords = new_chords;
              cursor = 0;
              st.score = 0;
              st.streak = 0;
              st.hit = 0;
              st.miss = 0;
            } else {
              free(new_chords.v);
            }
          }
        }

        if (key == KEY_STRUM_DOWN || key == KEY_STRUM_UP) {
          double offset_sec = offset_ms / 1000.0;
          double t = audio_time_sec(&aud) + offset_sec;

          // Notes that passed are already marked as missed in the main loop
          // Just check if we can hit the current note
          if (cursor < chords.n) {
            double delta = chords.v[cursor].t_sec - t;
            double ad = fabs(delta);

            if (ad <= bad) {
              uint8_t expected = chords.v[cursor].mask;
              int match = 0;
              
              // Count how many notes in the chord
              int note_count = 0;
              int highest_fret = -1;
              for (int l = 0; l < 5; l++) {
                if (expected & (1u << l)) {
                  note_count++;
                  highest_fret = l;
                }
              }
              
              // Guitar Hero rule: for single notes, allow higher frets + all lower frets
              if (note_count == 1) {
                // Single note: must hold the required fret
                // Can also hold any frets to the LEFT (lower numbered)
                int has_required = (held & (1u << highest_fret)) != 0;
                
                // Check no frets to the RIGHT (higher numbered) are held
                int invalid_higher = 0;
                for (int l = highest_fret + 1; l < 5; l++) {
                  if (held & (1u << l)) {
                    invalid_higher = 1;
                    break;
                  }
                }
                
                match = has_required && !invalid_higher;
              } else {
                // Chord (multiple notes): must match exactly
                match = (held == expected);
              }
              
              if (match) {
                st.hit++;
                st.streak++;
                int pts = (ad <= perfect) ? POINTS_PERFECT : (ad <= good ? POINTS_GOOD : POINTS_OK);
                st.score += pts * (1 + st.streak / STREAK_DIVISOR);
                
                int effect_type;
                if (ad <= perfect) {
                  effect_type = EFFECT_TYPE_PERFECT;
                } else if (ad <= good) {
                  effect_type = EFFECT_TYPE_GOOD;
                } else {
                  effect_type = EFFECT_TYPE_OK;
                }
                
                uint8_t m = chords.v[cursor].mask;
                for (int l = 0; l < 5; l++) {
                  if (m & (1u << l)) {
                    add_effect(l, effect_type, EFFECT_DURATION_HIT);
                  }
                }
                
                cursor++;
              } else {
                // Miss - show effects on wrong frets and timing feedback
                uint8_t diff_mask = held ^ expected;
                for (int l = 0; l < 5; l++) {
                  if (diff_mask & (1u << l)) {
                    add_effect(l, EFFECT_TYPE_MISS, EFFECT_DURATION_MISS);
                  }
                }
                st.miss++;
                st.streak = 0;
                
                // Show timing feedback for wrong frets
                snprintf(timing_feedback, sizeof(timing_feedback), "WRONG FRETS");
                feedback_timer = 0.5;  // Show for 0.5 seconds
              }
            } else {
              // Outside timing window - show too early or too late
              if (delta > 0) {
                snprintf(timing_feedback, sizeof(timing_feedback), "TOO EARLY");
              } else {
                snprintf(timing_feedback, sizeof(timing_feedback), "TOO LATE");
              }
              feedback_timer = 0.5;
            }
          }
        }
      }

      if (e.type == SDL_KEYUP && e.key.repeat == 0) {
        SDL_Keycode key = e.key.keysym.sym;
        if (key == KEY_FRET_GREEN)
          held &= ~(1u << 0);
        if (key == KEY_FRET_RED)
          held &= ~(1u << 1);
        if (key == KEY_FRET_YELLOW)
          held &= ~(1u << 2);
        if (key == KEY_FRET_BLUE)
          held &= ~(1u << 3);
        if (key == KEY_FRET_ORANGE)
          held &= ~(1u << 4);
      }
    }

    double offset_sec = offset_ms / 1000.0;
    double t = audio_time_sec(&aud) + offset_sec;

    if (cursor >= chords.n) {
      if (t > chords.v[chords.n - 1].t_sec + 2.0)
        break;
    }

    size_t view_cursor = cursor;
    while (view_cursor > 0 && chords.v[view_cursor - 1].t_sec > t - 0.5)
      view_cursor--;

    // Skip game logic if paused
    if (!paused) {
      // Check for missed notes (notes that passed without being hit)
      while (cursor < chords.n && chords.v[cursor].t_sec < t - bad) {
        uint8_t m = chords.v[cursor].mask;
        for (int l = 0; l < 5; l++) {
          if (m & (1u << l)) {
            add_effect(l, EFFECT_TYPE_MISS, EFFECT_DURATION_MISS);
          }
        }
        st.miss++;
        st.streak = 0;
        cursor++;
      }
    }

    update_effects(dt);
    
    // Update timing feedback timer
    if (feedback_timer > 0) {
      feedback_timer -= dt;
      if (feedback_timer <= 0) {
        timing_feedback[0] = '\0';  // Clear feedback
      }
    }

    draw_frame(&chords, view_cursor, t, lookahead, held, &st,
               offset_ms, selected_track, &track_names, timing_feedback);

    next += dt;
    double n = now_sec();
    double sleep_s = next - n;
    if (sleep_s > 0) {
      struct timespec ts;
      ts.tv_sec = (time_t)sleep_s;
      ts.tv_nsec = (long)((sleep_s - (double)ts.tv_sec) * 1e9);
      nanosleep(&ts, NULL);
    } else {
      next = n;
      usleep(1000);
    }
  }

cleanup:
  aud.started = 0;
  if (aud.dev)
    SDL_CloseAudioDevice(aud.dev);
  if (window)
    SDL_DestroyWindow(window);

  for (int i = 0; i < aud.stem_count; i++)
    free(aud.stems[i].pcm);
  free(aud.stems);
  free(chords.v);
  free(notes.v);

  if (args.needs_free) {
    free(args.midi_path);
    for (int i = 0; i < args.opus_count; i++)
      free(args.opus_paths[i]);
  }

  return 0;
}
