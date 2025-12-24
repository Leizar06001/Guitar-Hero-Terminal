#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "chart.h"
#include "midi.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// .chart format parser for Clone Hero / Phase Shift
// Format documentation:
// - [Song] section: metadata
// - [SyncTrack] section: tempo (BPM) and time signature changes
//   - tick = B bpm_value (e.g., "0 = B 117000" for 117 BPM)
//   - BPM value is BPM * 1000 (so 117000 = 117 BPM)
//   - tick = TS numerator (e.g., "0 = TS 4" for 4/4 time)
// - [ExpertSingle], [HardSingle], [MediumSingle], [EasySingle]: guitar notes
//   - tick = N lane duration (e.g., "1344 = N 0 6496" = green note at tick 1344, duration 6496)
//   - tick = N 5 0 = forced strum (break HOPO)
//   - tick = N 6 0 = tap note marker
//   - tick = S 2 duration = star power phrase
//   - tick = E event_name = event marker
// - Lanes: 0=green, 1=red, 2=yellow, 3=blue, 4=orange
// - Resolution: ticks per quarter note (e.g., 192)

typedef struct {
  int tick;
  int uspqn;  // Microseconds per quarter note
} TempoBPM;

typedef struct {
  TempoBPM *v;
  size_t n;
  size_t cap;
} TempoBPMVec;

static void tempo_bpm_vec_push(TempoBPMVec *vec, int tick, int uspqn) {
  if (vec->n >= vec->cap) {
    vec->cap = vec->cap ? vec->cap * 2 : 64;
    vec->v = (TempoBPM *)realloc(vec->v, vec->cap * sizeof(TempoBPM));
  }
  vec->v[vec->n].tick = tick;
  vec->v[vec->n].uspqn = uspqn;
  vec->n++;
}

// Convert tick to seconds using tempo map
static double tick_to_sec(int tick, int resolution, const TempoBPMVec *tempos) {
  if (tempos->n == 0) {
    // Default 120 BPM (500000 microseconds per quarter note)
    return (double)tick / (double)resolution * 0.5;
  }
  
  double sec = 0.0;
  int current_tick = 0;
  int current_uspqn = tempos->v[0].uspqn;
  
  for (size_t i = 0; i < tempos->n; i++) {
    if (tempos->v[i].tick > tick) {
      // Add time from current_tick to target tick
      int delta_ticks = tick - current_tick;
      double sec_per_tick = (double)current_uspqn / 1000000.0 / (double)resolution;
      sec += (double)delta_ticks * sec_per_tick;
      return sec;
    }
    
    // Add time from current_tick to this tempo change
    int delta_ticks = tempos->v[i].tick - current_tick;
    double sec_per_tick = (double)current_uspqn / 1000000.0 / (double)resolution;
    sec += (double)delta_ticks * sec_per_tick;
    
    current_tick = tempos->v[i].tick;
    current_uspqn = tempos->v[i].uspqn;
  }
  
  // Add remaining time after last tempo change
  int delta_ticks = tick - current_tick;
  double sec_per_tick = (double)current_uspqn / 1000000.0 / (double)resolution;
  sec += (double)delta_ticks * sec_per_tick;
  
  return sec;
}

// Trim leading/trailing whitespace
static char *trim(char *str) {
  char *end;
  
  // Trim leading space
  while (isspace((unsigned char)*str)) str++;
  
  if (*str == 0) return str;
  
  // Trim trailing space
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;
  
  end[1] = '\0';
  return str;
}

int chart_parse(const char *path, NoteVec *notes, TrackNameVec *track_names) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "Failed to open chart file: %s\n", path);
    return -1;
  }
  
  char line[512];
  char section[64] = "";
  int resolution = 192;  // Default resolution
  double chart_offset = 0.0;  // Offset in seconds from [Song] section
  TempoBPMVec tempos = {0};
  
  // Temporary storage for chart notes (tick-based)
  typedef struct {
    int tick;
    int lane;
    int duration;
    int diff;
    int is_forced;  // N 5 0 = forced strum
  } ChartNote;
  
  ChartNote *chart_notes = NULL;
  size_t chart_note_count = 0;
  size_t chart_note_cap = 0;
  
  while (fgets(line, sizeof(line), f)) {
    char *trimmed = trim(line);
    
    // Skip empty lines and comments
    if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
    
    // Section header
    if (trimmed[0] == '[') {
      char *end = strchr(trimmed, ']');
      if (end) {
        *end = '\0';
        strncpy(section, trimmed + 1, sizeof(section) - 1);
        section[sizeof(section) - 1] = '\0';
      }
      continue;
    }
    
    // Skip opening/closing braces
    if (strcmp(trimmed, "{") == 0 || strcmp(trimmed, "}") == 0) continue;
    
    // Parse key = value pairs
    char *eq = strchr(trimmed, '=');
    if (!eq) continue;
    
    *eq = '\0';
    char *key = trim(trimmed);
    char *value = trim(eq + 1);
    
    // Parse [Song] section
    if (strcmp(section, "Song") == 0) {
      if (strcmp(key, "Resolution") == 0) {
        resolution = atoi(value);
        if (resolution <= 0) resolution = 192;
      } else if (strcmp(key, "Offset") == 0) {
        // Offset is in seconds
        chart_offset = atof(value);
      }
    }
    // Parse [SyncTrack] section
    else if (strcmp(section, "SyncTrack") == 0) {
      int tick = atoi(key);
      
      // Parse tempo changes (B = BPM * 1000)
      // Example: "0 = B 117000" means 117 BPM
      if (value[0] == 'B') {
        int bpm_value = atoi(value + 2);
        // Convert BPM to microseconds per quarter note
        // Formula: uspqn = 60,000,000 / BPM
        double bpm = (double)bpm_value / 1000.0;
        int uspqn = (int)(60000000.0 / bpm);
        tempo_bpm_vec_push(&tempos, tick, uspqn);
      }
      // We can ignore TS (time signature) for now
    }
    // Parse guitar difficulty sections
    else if (strcmp(section, "ExpertSingle") == 0 ||
             strcmp(section, "HardSingle") == 0 ||
             strcmp(section, "MediumSingle") == 0 ||
             strcmp(section, "EasySingle") == 0) {
      
      int diff = 0;
      if (strcmp(section, "ExpertSingle") == 0) diff = 3;
      else if (strcmp(section, "HardSingle") == 0) diff = 2;
      else if (strcmp(section, "MediumSingle") == 0) diff = 1;
      else if (strcmp(section, "EasySingle") == 0) diff = 0;
      
      int tick = atoi(key);
      
      // Parse note format: "N lane duration"
      if (value[0] == 'N') {
        int lane = -1;
        int duration = 0;
        
        if (sscanf(value, "N %d %d", &lane, &duration) == 2) {
          // Lane 5 = forced strum (not a note)
          // Lane 6 = tap note marker (not a note)
          // Lanes 0-4 = actual notes
          if (lane >= 0 && lane <= 4) {
            // Add to chart notes
            if (chart_note_count >= chart_note_cap) {
              chart_note_cap = chart_note_cap ? chart_note_cap * 2 : 2048;
              chart_notes = (ChartNote *)realloc(chart_notes, chart_note_cap * sizeof(ChartNote));
            }
            
            chart_notes[chart_note_count].tick = tick;
            chart_notes[chart_note_count].lane = lane;
            chart_notes[chart_note_count].duration = duration;
            chart_notes[chart_note_count].diff = diff;
            chart_notes[chart_note_count].is_forced = 0;
            chart_note_count++;
          } else if (lane == 5 && duration == 0) {
            // Mark the note at this tick as forced strum
            for (size_t i = 0; i < chart_note_count; i++) {
              if (chart_notes[i].tick == tick && chart_notes[i].diff == diff) {
                chart_notes[i].is_forced = 1;
              }
            }
          }
        }
      }
      // We can ignore S (star power) and E (events) for now
    }
  }
  
  fclose(f);
  
  // Convert chart notes to NoteOn format
  for (size_t i = 0; i < chart_note_count; i++) {
    double t_sec = tick_to_sec(chart_notes[i].tick, resolution, &tempos);
    
    // Calculate duration in seconds
    int end_tick = chart_notes[i].tick + chart_notes[i].duration;
    double end_sec = tick_to_sec(end_tick, resolution, &tempos);
    double duration_sec = end_sec - t_sec;
    
    // Apply chart offset
    t_sec += chart_offset;
    
    // Convert lane to pitch (like MIDI)
    // Easy: 60-64, Medium: 72-76, Hard: 84-88, Expert: 96-100
    int base_pitch = 60 + chart_notes[i].diff * 12;
    int pitch = base_pitch + chart_notes[i].lane;
    
    NoteOn note;
    note.tick = (uint64_t)chart_notes[i].tick;
    note.t_sec = t_sec;
    note.pitch = pitch;
    note.lane = chart_notes[i].lane;
    note.diff = chart_notes[i].diff;
    note.vel = chart_notes[i].is_forced ? 96 : 100;  // Use velocity to mark forced notes
    note.track = 0;  // All chart notes are on track 0
    note.duration_sec = duration_sec;
    
    nv_push(notes, note);
  }
  
  // Add a track name for the guitar part
  if (track_names) {
    TrackName tn;
    tn.track_num = 0;
    strncpy(tn.name, "PART GUITAR", sizeof(tn.name) - 1);
    tn.name[sizeof(tn.name) - 1] = '\0';
    tnv_push(track_names, tn);
  }
  
  // Cleanup
  free(chart_notes);
  free(tempos.v);
  
  fprintf(stderr, "Parsed %zu notes from .chart file (resolution=%d, offset=%.3fs)\n", 
          notes->n, resolution, chart_offset);
  
  // Debug: print first few notes
  if (notes->n > 0) {
    fprintf(stderr, "First 5 notes:\n");
    for (size_t i = 0; i < notes->n && i < 5; i++) {
      fprintf(stderr, "  [%zu] tick=%lu, t=%.3fs, lane=%d, diff=%d\n",
              i, (unsigned long)notes->v[i].tick, notes->v[i].t_sec, 
              notes->v[i].lane, notes->v[i].diff);
    }
  }
  
  return 0;
}
