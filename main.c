#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "audio.h"
#include "midi.h"
#include "chart.h"
#include "terminal.h"
#include "settings.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char path[512];
  char title[128];
  char artist[128];
  char year[16];
  int diff_guitar;  // Guitar difficulty level (0-10+)
  char loading_phrase[128];  // Loading phrase from ini
} SongEntry;

typedef enum {
  MENU_NONE,
  MENU_PAUSE,
  MENU_OPTIONS
} MenuState;

typedef enum {
  OPT_KEY_GREEN,
  OPT_KEY_RED,
  OPT_KEY_YELLOW,
  OPT_KEY_BLUE,
  OPT_KEY_ORANGE,
  OPT_KEY_STRUM,
  OPT_OFFSET,
  OPT_BACK,
  OPT_COUNT
} OptionItem;

static void draw_menu(MenuState menu, int selection, int waiting_for_key, const Settings *settings) {
  printf("\x1b[2J\x1b[H");
  
  if (menu == MENU_PAUSE) {
    const char *items[] = {"Resume", "Restart", "Options", "Song List", "Exit"};
    int count = 5;
    
    printf("\x1b[1;37m╔═══════════════════════════╗\x1b[0m\n");
    printf("\x1b[1;37m║      PAUSED - MENU        ║\x1b[0m\n");
    printf("\x1b[1;37m╚═══════════════════════════╝\x1b[0m\n\n");
    
    for (int i = 0; i < count; i++) {
      if (i == selection) {
        printf("  \x1b[1;33m► %s\x1b[0m\n", items[i]);
      } else {
        printf("    %s\n", items[i]);
      }
    }
    printf("\n\x1b[90mUse ↑/↓ and Enter\x1b[0m\n");
    
  } else if (menu == MENU_OPTIONS) {
    const char *key_names[] = {
      "Green Fret", "Red Fret", "Yellow Fret", "Blue Fret", "Orange Fret",
      "Strum", "Offset (ms)", "Back"
    };
    
    printf("\x1b[1;37m╔═══════════════════════════╗\x1b[0m\n");
    printf("\x1b[1;37m║         OPTIONS           ║\x1b[0m\n");
    printf("\x1b[1;37m╚═══════════════════════════╝\x1b[0m\n\n");
    
    for (int i = 0; i < OPT_COUNT; i++) {
      const char *prefix = (i == selection) ? "\x1b[1;33m► " : "  ";
      const char *suffix = (i == selection) ? "\x1b[0m" : "";
      
      if (i == OPT_OFFSET) {
        printf("%s%s: %.0f%s\n", prefix, key_names[i], settings->global_offset_ms, suffix);
      } else if (i == OPT_BACK) {
        printf("\n%s%s%s\n", prefix, key_names[i], suffix);
      } else {
        SDL_Keycode key;
        switch (i) {
          case OPT_KEY_GREEN: key = settings->key_fret_green; break;
          case OPT_KEY_RED: key = settings->key_fret_red; break;
          case OPT_KEY_YELLOW: key = settings->key_fret_yellow; break;
          case OPT_KEY_BLUE: key = settings->key_fret_blue; break;
          case OPT_KEY_ORANGE: key = settings->key_fret_orange; break;
          case OPT_KEY_STRUM: key = settings->key_strum; break;
          default: key = SDLK_UNKNOWN; break;
        }
        printf("%s%s: %s%s\n", prefix, key_names[i], SDL_GetKeyName(key), suffix);
      }
    }
    
    if (waiting_for_key) {
      printf("\n\x1b[1;32mPress new key...\x1b[0m\n");
    } else if (selection == OPT_OFFSET) {
      printf("\n\x1b[90mUse +/- to adjust, Enter to confirm\x1b[0m\n");
    } else if (selection < OPT_BACK) {
      printf("\n\x1b[90mPress Enter to rebind key\x1b[0m\n");
    } else {
      printf("\n\x1b[90mPress Enter to go back\x1b[0m\n");
    }
  }
  fflush(stdout);
}

// Parse song.ini file for metadata
static int parse_song_ini(const char *ini_path, char *title, char *artist, char *year, 
                          int *diff_guitar, char *loading_phrase) {
  FILE *f = fopen(ini_path, "r");
  if (!f) return 0;
  
  title[0] = '\0';
  artist[0] = '\0';
  year[0] = '\0';
  loading_phrase[0] = '\0';
  *diff_guitar = 0;
  
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
    } else if (strncmp(p, "diff_guitar", 11) == 0) {
	  if (p[11] == '_') continue; // skip diff_guitar_real_XX
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        *diff_guitar = atoi(eq);
      }
    } else if (strncmp(p, "loading_phrase", 14) == 0) {
      char *eq = strchr(p, '=');
      if (eq) {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        strncpy(loading_phrase, eq, 127);
        loading_phrase[127] = '\0';
        char *nl = strchr(loading_phrase, '\n');
        if (nl) *nl = '\0';
      }
    }
  }

  // print parsed info for debugging
//   printf("> '%s'\nArtist='%s'\nYear='%s'\nDiff_guitar=%d\nLoading_phrase='%s'\n\n",
// 		 title, artist, year, *diff_guitar, loading_phrase);
  
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
    char ini_path[512], notes_path[512];
    snprintf(ini_path, sizeof(ini_path), "%s/song.ini", song_path);
    
    // Check for notes.chart or notes.mid
    int has_notes = 0;
    snprintf(notes_path, sizeof(notes_path), "%s/notes.chart", song_path);
    if (access(notes_path, R_OK) == 0) {
      has_notes = 1;
    } else {
      snprintf(notes_path, sizeof(notes_path), "%s/notes.mid", song_path);
      if (access(notes_path, R_OK) == 0) {
        has_notes = 1;
      }
    }
    
    int has_ini = (access(ini_path, R_OK) == 0);
    
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
    
    if (!has_ini || !has_notes || !has_opus) {
      printf("\nSkipping %s: missing required files\n", song_path);
      continue;
    }
    
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
    if (!parse_song_ini(ini_path, songs[count].title, songs[count].artist, songs[count].year,
                        &songs[count].diff_guitar, songs[count].loading_phrase)) {
      // Use folder name as fallback
      strncpy(songs[count].title, ent->d_name, 127);
      songs[count].title[127] = '\0';
      songs[count].artist[0] = '\0';
      songs[count].year[0] = '\0';
      songs[count].diff_guitar = 0;
      songs[count].loading_phrase[0] = '\0';
    }
    
    count++;
  }
  
  closedir(dir);
  *out_songs = songs;
  return count;
}

// Display song selector and return selected index (-1 if quit)
static int show_song_selector(SongEntry *songs, int count, Settings *settings) {
  int selected = 0;
  int menu_mode = 0;  // 0 = songs, 1 = options
  int option_selection = 0;
  int waiting_for_key = 0;
  int need_redraw = 1;  // Flag to control when to redraw
  
  // Flush stdin and clear any error state
  tcflush(STDIN_FILENO, TCIFLUSH);
  clearerr(stdin);
  
  // Set terminal to raw mode for immediate key reading
  struct termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  new_term.c_cc[VMIN] = 1;
  new_term.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
  
  while (1) {
    // Clear screen and show menu only when needed
    if (need_redraw) {
      printf("\x1b[2J\x1b[1;1H");
      
      if (menu_mode == 1) {
        // Options menu
        draw_menu(MENU_OPTIONS, option_selection, waiting_for_key, settings);
      } else {
        // Song selector
        printf("\x1b[1;36m╔═══════════════════════════════════════════════════════════════════════════════════════════╗\x1b[0m\n");
        printf("\x1b[1;36m║                                   SONG SELECTOR                                           ║\x1b[0m\n");
        printf("\x1b[1;36m╠═══════════════════════════════════════════════════════════════════════════════════════════╣\x1b[0m\n");
        printf("\x1b[1;36m║   Song                            Artist                         Year        Difficulty   ║\x1b[0m\n");
        printf("\x1b[1;36m╠═══════════════════════════════════════════════════════════════════════════════════════════╣\x1b[0m\n");

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
          
          // Difficulty stars
          char stars[16] = "";
          int diff = songs[i].diff_guitar;
          if (diff > 10) diff = 10;  // Cap at 10 stars
          for (int s = 0; s < diff; s++) {
            stars[s] = '*';
          }
          stars[diff] = '\0';

		  // Crop long titles/artists
		  char display_title[31];
		  char display_artist[31];
		  strncpy(display_title, songs[i].title, 30);
		  display_title[30] = '\0';
		  if (strlen(songs[i].title) > 30) {
		    display_title[27] = '.';
		    display_title[28] = '.';
		    display_title[29] = '.';
		  }
		  strncpy(display_artist, songs[i].artist, 30);
		  display_artist[30] = '\0';
		  if (strlen(songs[i].artist) > 30) {
		    display_artist[27] = '.';
		    display_artist[28] = '.';
		    display_artist[29] = '.';
		  }
          
          // Print: title, artist, year, difficulty stars
          printf("\x1b[1;37m%-30s\x1b[0m  \x1b[36m%-30s\x1b[0m  \x1b[33m%-10s\x1b[0m  \x1b[1;93m%-10s\x1b[0m", 
                 display_title, display_artist, songs[i].year, stars);
          // Position cursor and print right wall
          printf("\x1b[93G\x1b[1;36m║\x1b[0m\n");
        }
        
        printf("\x1b[1;36m╚═══════════════════════════════════════════════════════════════════════════════════════════╝\x1b[0m\n");
        
        // Display album artwork using chafa
        char album_path[512];
        snprintf(album_path, sizeof(album_path), "%s/album.jpg", songs[selected].path);
        
        // Check if album artwork exists
        FILE *album_check = fopen(album_path, "r");
        if (album_check) {
          fclose(album_check);
          
          printf("\n");  // Blank line before artwork
          
          // Use fork/exec to avoid shell escaping issues
          pid_t pid = fork();
          if (pid == 0) {
            // Child process
            char *args[] = {
              "chafa",
              "--size", "35x20",
              "--colors", "256",
              album_path,
              NULL
            };
            execvp("chafa", args);
            _exit(1);  // If exec fails
          } else if (pid > 0) {
            // Parent process - wait for chafa to finish
            int status;
            waitpid(pid, &status, 0);
          }
          
          printf("\n");  // Blank line after artwork
        }
        
        printf("\x1b[37mUse \x1b[1;32m↑/↓\x1b[0;37m to select, \x1b[1;32mENTER\x1b[0;37m to play, \x1b[1;32mO\x1b[0;37m for options, \x1b[1;31mq/ESC\x1b[0;37m to quit\x1b[0m\n");
      }
      fflush(stdout);
      need_redraw = 0;
    }
    
    // Read key
    char c = getchar();
    
    if (menu_mode == 1) {
      // Handle options menu input
      if (waiting_for_key) {
        if (c == 27) {
          waiting_for_key = 0;
          need_redraw = 1;
        } else {
          switch (option_selection) {
            case OPT_KEY_GREEN: settings->key_fret_green = (SDL_Keycode)c; break;
            case OPT_KEY_RED: settings->key_fret_red = (SDL_Keycode)c; break;
            case OPT_KEY_YELLOW: settings->key_fret_yellow = (SDL_Keycode)c; break;
            case OPT_KEY_BLUE: settings->key_fret_blue = (SDL_Keycode)c; break;
            case OPT_KEY_ORANGE: settings->key_fret_orange = (SDL_Keycode)c; break;
            case OPT_KEY_STRUM: settings->key_strum = (SDL_Keycode)c; break;
          }
          waiting_for_key = 0;
          settings_save(settings);
          need_redraw = 1;
        }
        continue;
      }
      
      if (c == 27) {  // ESC or arrow
        // Set timeout to detect if it's a bare ESC or arrow sequence
        struct termios temp_term = new_term;
        temp_term.c_cc[VMIN] = 0;
        temp_term.c_cc[VTIME] = 1;  // 100ms timeout
        tcsetattr(STDIN_FILENO, TCSANOW, &temp_term);
        
        int next = getchar();
        
        // Restore original settings
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        
        if (next == '[') {
          char arrow = getchar();
          if (arrow == 'A') {
            option_selection = (option_selection > 0) ? option_selection - 1 : (OPT_COUNT - 1);
            need_redraw = 1;
          } else if (arrow == 'B') {
            option_selection = (option_selection < OPT_COUNT - 1) ? option_selection + 1 : 0;
            need_redraw = 1;
          }
        } else if (next == EOF) {
          // Timeout - just ESC - go back to song selector
          menu_mode = 0;
          need_redraw = 1;
        }
        // else: some other character after ESC, ignore
      } else if (c == '\n' || c == '\r') {
        if (option_selection == OPT_BACK) {
          menu_mode = 0;
          settings_save(settings);
          need_redraw = 1;
        } else if (option_selection == OPT_OFFSET) {
          // Offset adjusted with +/-
        } else {
          waiting_for_key = 1;
          need_redraw = 1;
        }
      } else if ((c == '+' || c == '=') && option_selection == OPT_OFFSET) {
        settings->global_offset_ms += OFFSET_STEP;
        need_redraw = 1;
      } else if (c == '-' && option_selection == OPT_OFFSET) {
        settings->global_offset_ms -= OFFSET_STEP;
        need_redraw = 1;
      }
      continue;
    }
    
    // Handle song selector input
    if (c == 27) {  // ESC or arrow key
      // Set timeout to detect if it's a bare ESC or arrow sequence
      struct termios temp_term = new_term;
      temp_term.c_cc[VMIN] = 0;
      temp_term.c_cc[VTIME] = 1;  // 100ms timeout
      tcsetattr(STDIN_FILENO, TCSANOW, &temp_term);
      
      int next = getchar();
      
      // Restore original settings
      tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
      
      if (next == '[') {  // Arrow key sequence
        char arrow = getchar();
        if (arrow == 'A') {  // Up arrow
          selected = (selected > 0) ? selected - 1 : count - 1;
          need_redraw = 1;
        } else if (arrow == 'B') {  // Down arrow
          selected = (selected < count - 1) ? selected + 1 : 0;
          need_redraw = 1;
        }
      } else if (next == EOF) {
        // Timeout - just ESC - quit
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        return -1;
      }
      // else: some other character after ESC, ignore
    } else if (c == '\n' || c == '\r') {  // Enter
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return selected;
    } else if (c == 'o' || c == 'O') {  // Options
      menu_mode = 1;
      option_selection = 0;
      need_redraw = 1;
    } else if (c == 'q' || c == 'Q') {  // q to quit
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return -1;
    }
  }
}

// Display difficulty selector and return selected difficulty (-1 if back, -2 if quit)
static int show_difficulty_selector(void) {
  const char *difficulties[] = {"Easy", "Medium", "Hard", "Expert"};
  const char *colors[] = {"\x1b[1;32m", "\x1b[1;33m", "\x1b[1;31m", "\x1b[1;35m"};  // Green, Yellow, Red, Magenta
  int selected = 3;  // Default to Expert
  
  // Flush stdin and clear any error state
  tcflush(STDIN_FILENO, TCIFLUSH);
  clearerr(stdin);
  
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
    printf("\x1b[37mUse \x1b[1;32m↑/↓\x1b[0;37m to select, \x1b[1;32mENTER\x1b[0;37m to continue, \x1b[1;31mESC\x1b[0;37m to go back, \x1b[1;31mQ\x1b[0;37m to quit\x1b[0m\n");
    fflush(stdout);
    
    // Read key
    char c = getchar();
    
    if (c == 27) {  // ESC or arrow key
      // Set timeout to detect if it's a bare ESC or arrow sequence
      struct termios temp_term = new_term;
      temp_term.c_cc[VMIN] = 0;
      temp_term.c_cc[VTIME] = 1;  // 100ms timeout
      tcsetattr(STDIN_FILENO, TCSANOW, &temp_term);
      
      int next = getchar();
      
      // Restore original settings
      tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
      
      if (next == '[') {  // Arrow key sequence
        char arrow = getchar();
        if (arrow == 'A') {  // Up arrow
          selected = (selected > 0) ? selected - 1 : 3;
        } else if (arrow == 'B') {  // Down arrow
          selected = (selected < 3) ? selected + 1 : 0;
        }
      } else if (next == EOF) {
        // Timeout - just ESC - go back to song selection
        // Don't restore terminal, let song selector handle it
        return -1;
      }
      // else: some other character after ESC, ignore
    } else if (c == '\n' || c == '\r') {  // Enter
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return selected;
    } else if (c == 'q' || c == 'Q') {  // Q to quit app
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      return -2;
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
  (void)argc;  // Unused
  (void)argv;  // Unused
  
  Settings settings;
  settings_load(&settings);

select_song:
  // Always use song selector
  SongEntry *songs = NULL;
  int song_count = scan_songs_directory("Songs", &songs);

  if (song_count == 0) {
    fprintf(stderr, "No valid songs found in Songs/ directory\n");
    fprintf(stderr, "Each song folder must contain: notes.mid, *.opus, and song.ini\n");
    return 1;
  }
  
  int selected = show_song_selector(songs, song_count, &settings);
  
  if (selected < 0) {
    free(songs);
    return 0;  // User quit
  }
  
  // Show difficulty selector (loop back if user presses ESC)
  int diff_choice;
  while (1) {
    diff_choice = show_difficulty_selector();
    
    if (diff_choice == -2) {
      // User pressed Q - quit app
      free(songs);
      return 0;
    }
    
    if (diff_choice == -1) {
      // User pressed ESC - go back to song selector
      selected = show_song_selector(songs, song_count, &settings);
      if (selected < 0) {
        free(songs);
        return 0;  // User quit from song selector
      }
      continue;  // Try difficulty selection again
    }
    break;  // Valid difficulty selected
  }
  
  // Get song info - copy before freeing songs
  char song_path[512];
  char loading_phrase[128];
  strncpy(song_path, songs[selected].path, sizeof(song_path) - 1);
  song_path[511] = '\0';
  strncpy(loading_phrase, songs[selected].loading_phrase, sizeof(loading_phrase) - 1);
  loading_phrase[127] = '\0';
  
  const char *diff_strings[] = {"easy", "medium", "hard", "expert"};
  const char *diff_str = diff_strings[diff_choice];
  
  free(songs);  // Done with songs list
  
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

  // Load song files from selected folder
  char notes_path[512];
  char *opus_paths[16];
  int opus_count = 0;
  int is_chart = 0;
  
  // Check for notes.chart first, then notes.mid
  snprintf(notes_path, sizeof(notes_path), "%s/notes.chart", song_path);
  if (access(notes_path, R_OK) == 0) {
    is_chart = 1;
  } else {
    snprintf(notes_path, sizeof(notes_path), "%s/notes.mid", song_path);
    if (access(notes_path, R_OK) != 0) {
      fprintf(stderr, "No notes.mid or notes.chart file found in %s\n", song_path);
      return 1;
    }
  }
  
  // Scan for opus files only (we already know notes path)
  DIR *dir = opendir(song_path);
  if (dir) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_name[0] == '.') continue;
      
      size_t len = strlen(entry->d_name);
      if (len > 5 && strcmp(entry->d_name + len - 5, ".opus") == 0) {
        if (opus_count < 16) {
          char *full_path = (char *)malloc(strlen(song_path) + strlen(entry->d_name) + 2);
          sprintf(full_path, "%s/%s", song_path, entry->d_name);
          opus_paths[opus_count++] = full_path;
        }
      }
    }
    closedir(dir);
  }
  
  int hopo_frequency = parse_hopo_from_ini(song_path);

  // Display loading phrase if available in cyan
  if (loading_phrase[0] != '\0') {
    fprintf(stderr, "\n\x1b[1;36m%s\x1b[0m\n\n", loading_phrase);
  }

  NoteVec notes = {0};
  TrackNameVec track_names = {0};
  
  if (is_chart) {
    fprintf(stderr, "Parsing .chart file: %s\n", notes_path);
    if (chart_parse(notes_path, &notes, &track_names) != 0) {
      fprintf(stderr, "Failed to parse .chart file\n");
      return 1;
    }
  } else {
    fprintf(stderr, "Parsing MIDI: %s\n", notes_path);
    midi_parse(notes_path, &notes, &track_names);
  }
  
  if (notes.n == 0) {
    fprintf(stderr, "No notes found in notes file.\n");
    return 1;
  }

  int diff = parse_diff(diff_str);
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
  build_chords(&notes, diff, selected_track, hopo_frequency, &chords);
  
  if (chords.n == 0) {
    fprintf(stderr, "No notes for difficulty %s\n", diff_name(diff));
    return 1;
  }

  fprintf(stderr, "Found %zu chords for difficulty %s\n", chords.n, diff_name(diff));

  AudioEngine aud = {0};
  audio_init(&aud, AUDIO_SAMPLE_RATE);

  fprintf(stderr, "Loading %d Opus files...\n", opus_count);
  aud.stems = (Stem *)calloc((size_t)opus_count, sizeof(Stem));
  aud.stem_count = opus_count;

  int guitar_stem_idx = -1;
  for (int i = 0; i < opus_count; i++) {
    fprintf(stderr, "  [%d/%d] %s\n", i + 1, opus_count, opus_paths[i]);
    load_opus_file(opus_paths[i], &aud.stems[i]);
    aud.stems[i].gain = 1.0f;
    aud.stems[i].target_gain = 1.0f;
    aud.stems[i].enabled = 1;
    
    // Check if this is the guitar track
    if (strstr(aud.stems[i].name, "guitar") != NULL || 
        strstr(aud.stems[i].name, "Guitar") != NULL ||
        strstr(aud.stems[i].name, "GUITAR") != NULL) {
      aud.stems[i].is_player_track = 1;
      guitar_stem_idx = i;
      fprintf(stderr, "  -> Detected as player track (dynamic volume)\n");
    }
  }

  fprintf(stderr, "\nPress \x1b[0;96mENTER\x1b[0m to start, or Q/ESC to quit.\n");
  fprintf(stderr, "\x1b[0;93mFocus the SDL window if needed.\x1b[0m\n");

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
        if (e.key.keysym.sym == KEY_MENU || e.key.keysym.sym == KEY_QUIT)
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

  // Load song-specific offset from song.ini
  double song_offset_ms = song_offset_load(song_path);
  double global_offset_ms = settings.global_offset_ms;
  double total_offset_ms = global_offset_ms + song_offset_ms;
  
  const double lookahead = DEFAULT_LOOKAHEAD;
  const double perfect = TIMING_PERFECT;
  const double good = TIMING_GOOD;
  const double bad = TIMING_BAD;

  uint8_t held = 0;
  Stats st = {0};
  MenuState menu_state = MENU_NONE;
  int menu_selection = 0;
  int waiting_for_key = 0;
  
  // Performance tracking for dynamic guitar volume
  #define PERF_WINDOW 5  // Very small window = very fast reaction
  int recent_notes[PERF_WINDOW] = {1, 1, 1, 1, 1};  // Start optimistic - assume all hits
  int perf_idx = 0;
  
  char timing_feedback[32] = "";
  double feedback_timer = 0.0;

  size_t cursor = 0;

  const double fps = TARGET_FPS;
  const double dt = 1.0 / fps;
  double next = now_sec();
  
  // Helper to update guitar volume based on performance
  auto void update_guitar_volume() {
    if (guitar_stem_idx < 0) return;
    
    int hits = 0;
    for (int i = 0; i < PERF_WINDOW; i++) {
      hits += recent_notes[i];
    }
    
    float hit_rate = (float)hits / (float)PERF_WINDOW;
    
    // Very reactive: 60% for full volume, <40% for quiet
    float target;
    if (hit_rate >= 0.6f) {
      target = 1.0f;  // Full volume - 3/5 hits
    } else if (hit_rate < 0.4f) {
      target = 0.1f;  // Quiet - less than 2/5 hits
    } else {
      // Linear interpolation between 0.1 and 1.0 (40%-60% range)
      target = 0.1f + (hit_rate - 0.4f) / 0.2f * 0.9f;
    }
    
    aud.stems[guitar_stem_idx].target_gain = target;
  }

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

        // Menu handling
        if (menu_state != MENU_NONE) {
          if (waiting_for_key) {
            // Rebinding a key
            if (key == SDLK_ESCAPE) {
              waiting_for_key = 0;
            } else {
              switch (menu_selection) {
                case OPT_KEY_GREEN: settings.key_fret_green = key; break;
                case OPT_KEY_RED: settings.key_fret_red = key; break;
                case OPT_KEY_YELLOW: settings.key_fret_yellow = key; break;
                case OPT_KEY_BLUE: settings.key_fret_blue = key; break;
                case OPT_KEY_ORANGE: settings.key_fret_orange = key; break;
                case OPT_KEY_STRUM: settings.key_strum = key; break;
              }
              waiting_for_key = 0;
              settings_save(&settings);
            }
            draw_menu(menu_state, menu_selection, waiting_for_key, &settings);
            continue;
          }
          
          if (key == SDLK_ESCAPE || (key == SDLK_q && menu_state == MENU_PAUSE)) {
            if (menu_state == MENU_OPTIONS) {
              menu_state = MENU_PAUSE;
              menu_selection = 2;
            } else {
              menu_state = MENU_NONE;
              aud.started = 1;
              clear_screen_hide_cursor();
            }
            draw_menu(menu_state, menu_selection, 0, &settings);
            continue;
          }
          
          if (key == SDLK_UP) {
            menu_selection--;
            if (menu_selection < 0) {
              menu_selection = (menu_state == MENU_PAUSE) ? 4 : (OPT_COUNT - 1);
            }
            draw_menu(menu_state, menu_selection, 0, &settings);
            continue;
          }
          
          if (key == SDLK_DOWN) {
            menu_selection++;
            int max = (menu_state == MENU_PAUSE) ? 4 : (OPT_COUNT - 1);
            if (menu_selection > max) menu_selection = 0;
            draw_menu(menu_state, menu_selection, 0, &settings);
            continue;
          }
          
          if (key == SDLK_RETURN || key == SDLK_RETURN2) {
            if (menu_state == MENU_PAUSE) {
              switch (menu_selection) {
                case 0:  // Resume
                  menu_state = MENU_NONE;
                  aud.started = 1;
                  clear_screen_hide_cursor();
                  break;
                case 1:  // Restart
                  // Restart - reset everything and jump to start_game
                  for (int i = 0; i < aud.stem_count; i++)
                    aud.stems[i].pos = 0;
                  aud.frames_played = 0;
                  cursor = 0;
                  st.score = 0;
                  st.streak = 0;
                  st.hit = 0;
                  st.miss = 0;
                  held = 0;
                  timing_feedback[0] = '\0';
                  feedback_timer = 0.0;
                  menu_state = MENU_NONE;
                  aud.started = 1;
                  clear_screen_hide_cursor();
                  break;
                case 2:  // Options
                  menu_state = MENU_OPTIONS;
                  menu_selection = 0;
                  draw_menu(menu_state, menu_selection, 0, &settings);
                  break;
                case 3:  // Song List
                  // Return to song list - cleanup current song
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
                  for (int i = 0; i < opus_count; i++)
                    free(opus_paths[i]);
                  show_cursor();
                  term_raw_off();
                  goto select_song;
                case 4:  // Exit
                  // Exit application
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
                  for (int i = 0; i < opus_count; i++)
                    free(opus_paths[i]);
                  exit(0);
              }
            } else if (menu_state == MENU_OPTIONS) {
              if (menu_selection == OPT_BACK) {
                menu_state = MENU_PAUSE;
                menu_selection = 2;
                settings_save(&settings);
                total_offset_ms = settings.global_offset_ms + song_offset_ms;
              } else if (menu_selection == OPT_OFFSET) {
                // Offset is adjusted with +/-, not Enter
              } else {
                waiting_for_key = 1;
              }
              draw_menu(menu_state, menu_selection, waiting_for_key, &settings);
            }
            continue;
          }
          
          if (menu_state == MENU_OPTIONS && menu_selection == OPT_OFFSET) {
            if (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
              settings.global_offset_ms += OFFSET_STEP;
              total_offset_ms = settings.global_offset_ms + song_offset_ms;
              draw_menu(menu_state, menu_selection, 0, &settings);
              continue;
            }
            if (key == SDLK_MINUS || key == SDLK_UNDERSCORE || key == SDLK_KP_MINUS) {
              settings.global_offset_ms -= OFFSET_STEP;
              total_offset_ms = settings.global_offset_ms + song_offset_ms;
              draw_menu(menu_state, menu_selection, 0, &settings);
              continue;
            }
          }
          
          continue;
        }

        // In-game controls
        if (key == KEY_QUIT)
          goto cleanup;

        if (key == KEY_MENU) {
          menu_state = MENU_PAUSE;
          menu_selection = 0;
          aud.started = 0;
          draw_menu(menu_state, menu_selection, 0, &settings);
          continue;
        }
        
        // Song-specific offset adjustment (in-game)
        if (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
          song_offset_ms += OFFSET_STEP;
          total_offset_ms = settings.global_offset_ms + song_offset_ms;
          song_offset_save(song_path, song_offset_ms);
          continue;
        }
        if (key == SDLK_MINUS || key == SDLK_UNDERSCORE || key == SDLK_KP_MINUS) {
          song_offset_ms -= OFFSET_STEP;
          total_offset_ms = settings.global_offset_ms + song_offset_ms;
          song_offset_save(song_path, song_offset_ms);
          continue;
        }

        uint8_t old_held = held;
        
        if (key == settings.key_fret_green)
          held |= (1u << 0);
        if (key == settings.key_fret_red)
          held |= (1u << 1);
        if (key == settings.key_fret_yellow)
          held |= (1u << 2);
        if (key == settings.key_fret_blue)
          held |= (1u << 3);
        if (key == settings.key_fret_orange)
          held |= (1u << 4);

        // Check for HOPO hit on fret change
        if (held != old_held && cursor < chords.n && chords.v[cursor].is_hopo) {
          double offset_sec = total_offset_ms / 1000.0;
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
              recent_notes[perf_idx] = 1;
              perf_idx = (perf_idx + 1) % PERF_WINDOW;
              update_guitar_volume();
              
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

        if (key >= KEY_TRACK_MIN && key <= KEY_TRACK_MAX) {
          int new_track = (int)(key - SDLK_0);
          if (new_track <= max_track && new_track != selected_track) {
            ChordVec new_chords = {0};
            build_chords(&notes, diff, new_track, hopo_frequency, &new_chords);

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
            build_chords(&notes, diff, -1, hopo_frequency, &new_chords);

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

        if (key == settings.key_strum) {
          double offset_sec = total_offset_ms / 1000.0;
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
                recent_notes[perf_idx] = 1;
                perf_idx = (perf_idx + 1) % PERF_WINDOW;
                update_guitar_volume();
                
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
                recent_notes[perf_idx] = 0;
                perf_idx = (perf_idx + 1) % PERF_WINDOW;
                update_guitar_volume();
                
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
        if (key == settings.key_fret_green)
          held &= ~(1u << 0);
        if (key == settings.key_fret_red)
          held &= ~(1u << 1);
        if (key == settings.key_fret_yellow)
          held &= ~(1u << 2);
        if (key == settings.key_fret_blue)
          held &= ~(1u << 3);
        if (key == settings.key_fret_orange)
          held &= ~(1u << 4);
      }
    }

    double offset_sec = total_offset_ms / 1000.0;
    double t = audio_time_sec(&aud) + offset_sec;

    if (cursor >= chords.n) {
      if (t > chords.v[chords.n - 1].t_sec + 2.0)
        break;
    }

    // Calculate view_cursor to include notes with active sustains
    // Need to look back far enough to catch sustains that are still playing
    size_t view_cursor = cursor;
    while (view_cursor > 0) {
      const Chord *prev = &chords.v[view_cursor - 1];
      double sustain_end = prev->t_sec + prev->duration_sec;
      // Include note if either the note head or sustain end is recent
      if (prev->t_sec > t - 0.5 || sustain_end > t - 0.3) {
        view_cursor--;
      } else {
        break;
      }
    }

    // Skip game logic if in menu
    if (menu_state == MENU_NONE) {
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
        recent_notes[perf_idx] = 0;
        perf_idx = (perf_idx + 1) % PERF_WINDOW;
        update_guitar_volume();
        cursor++;
      }
    }

    update_effects(dt);
    
    // Update timing feedback timer
    if (feedback_timer > 0) {
      feedback_timer -= dt;
      if (feedback_timer <= 0) {
        timing_feedback[0] = '\0';
      }
    }

    if (menu_state == MENU_NONE) {
      draw_frame(&chords, view_cursor, t, lookahead, held, &st,
                 song_offset_ms, global_offset_ms, selected_track, &track_names, timing_feedback);
    }

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

  for (int i = 0; i < opus_count; i++)
    free(opus_paths[i]);

  return 0;
}
