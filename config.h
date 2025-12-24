#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

/* ==================== Game Configuration ==================== */

/* Default timing offset in milliseconds (negative = notes appear earlier) */
#define DEFAULT_OFFSET -360

/* Default difficulty level */
#define DEFAULT_DIFFICULTY "expert"

/* Default lookahead time in seconds (how far ahead notes are visible) */
#define DEFAULT_LOOKAHEAD 2.0
#define MIN_LOOKAHEAD 0.5

/* Target frame rate */
#define TARGET_FPS 60.0

/* Performance tracking window size (for sound reaction) */
#define PERF_WINDOW 3  // Very small window = very fast reaction

/* Consecutive misses before lowering guitar volume */
#define CONSECUTIVE_MISS_THRESHOLD 2  // Lower guitar volume after 3 consecutive misses

/* ==================== Timing Windows ==================== */

/* Hit timing windows in seconds */
#define TIMING_PERFECT 0.030  // 30ms - perfect hit
#define TIMING_GOOD    0.055  // 55ms - good hit
#define TIMING_BAD     0.120  // 100ms - acceptable hit

/* Points awarded for each timing quality */
#define POINTS_PERFECT 100
#define POINTS_GOOD    70
#define POINTS_OK      50

/* Streak bonus divisor (bonus = score * (1 + streak / STREAK_DIVISOR)) */
#define STREAK_DIVISOR 10

/* Max multiplier for streak bar display */
#define MAX_MULTIPLIER 4

/* ==================== Input Keybindings ==================== */

/* Fret buttons (lanes 0-4) */
#define KEY_FRET_GREEN   SDLK_z  // Lane 0
#define KEY_FRET_RED     SDLK_x  // Lane 1
#define KEY_FRET_YELLOW  SDLK_c  // Lane 2
#define KEY_FRET_BLUE    SDLK_v  // Lane 3
#define KEY_FRET_ORANGE  SDLK_b  // Lane 4

/* Strum keys */
#define KEY_STRUM_DOWN   SDLK_RETURN
#define KEY_STRUM_UP     SDLK_RETURN2

/* Offset adjustment */
#define KEY_OFFSET_INC   SDLK_PLUS
#define KEY_OFFSET_INC2  SDLK_EQUALS
#define KEY_OFFSET_INC3  SDLK_KP_PLUS
#define KEY_OFFSET_DEC   SDLK_MINUS
#define KEY_OFFSET_DEC2  SDLK_UNDERSCORE
#define KEY_OFFSET_DEC3  SDLK_KP_MINUS
#define OFFSET_STEP      10.0  // Milliseconds per press

/* Track selection */
#define KEY_TRACK_ALL    SDLK_0
#define KEY_TRACK_MIN    SDLK_1
#define KEY_TRACK_MAX    SDLK_9

/* Game control */
#define KEY_QUIT         SDLK_q
#define KEY_MENU         SDLK_ESCAPE
#define KEY_START        SDLK_RETURN
#define KEY_START2       SDLK_RETURN2

/* ==================== Audio Configuration ==================== */

/* SDL audio buffer size in frames (affects latency) */
#define AUDIO_BUFFER_SIZE 256  // Increased for WSL2 thread scheduling tolerance

/* Audio sample rate (Opus is 48kHz) */
#define AUDIO_SAMPLE_RATE 48000

/* Number of audio channels */
#define AUDIO_CHANNELS 2

/* Latency compensation multiplier */
#define LATENCY_BUFFER_MULT 2

/* ==================== Visual Effects ==================== */

/* Maximum concurrent visual effects */
#define MAX_EFFECTS 32

/* Effect duration in seconds */
#define EFFECT_DURATION_HIT  0.2
#define EFFECT_DURATION_MISS 0.2

/* Effect types */
#define EFFECT_TYPE_MISS    0
#define EFFECT_TYPE_OK      1
#define EFFECT_TYPE_GOOD    2
#define EFFECT_TYPE_PERFECT 3

/* ==================== Display Configuration ==================== */

/* Minimum terminal size */
#define MIN_TERM_ROWS 24
#define MIN_TERM_COLS 80

/* Default terminal size if detection fails */
#define DEFAULT_TERM_ROWS 24
#define DEFAULT_TERM_COLS 80

/* Number of lanes */
#define NUM_LANES 5

/* Lane width in characters (how wide each note is) */
#define NOTE_WIDTH 5  // Width of each note (e.g., 5 = [###], 7 = [#####])
#define LANE_WIDTH NOTE_WIDTH

/* Minimum gameplay area height */
#define MIN_GAMEPLAY_HEIGHT 10

/* ==================== MIDI Configuration ==================== */

/* Guitar Hero pitch ranges for each difficulty */
#define MIDI_EASY_START   60  // Notes 60-64
#define MIDI_EASY_END     64
#define MIDI_MEDIUM_START 72  // Notes 72-76
#define MIDI_MEDIUM_END   76
#define MIDI_HARD_START   84  // Notes 84-88
#define MIDI_HARD_END     88
#define MIDI_EXPERT_START 96  // Notes 96-100
#define MIDI_EXPERT_END   100

/* Chord grouping epsilon (seconds) - notes within this time are one chord */
#define CHORD_EPSILON 0.0015  // 1.5ms

/* Default tempo (120 BPM in microseconds per quarter note) */
#define DEFAULT_TEMPO_USPQN 500000

/* ==================== Memory Limits ==================== */

/* Initial vector capacities */
#define INITIAL_NOTE_CAPACITY  2048
#define INITIAL_TEMPO_CAPACITY 64
#define INITIAL_TRACK_CAPACITY 16
#define INITIAL_CHORD_CAPACITY 2048

/* Maximum colored characters for rendering */
#define MAX_COLORED_CHARS 8192

/* Maximum gain/mute entries */
#define MAX_GAIN_ENTRIES 16
#define MAX_MUTE_ENTRIES 16

/* Maximum opus files */
#define MAX_OPUS_FILES 16

/* ==================== SDL Configuration ==================== */

/* SDL window configuration */
#define SDL_WINDOW_TITLE "midifall - CLICK HERE FOR INPUT"
#define SDL_WINDOW_WIDTH  400
#define SDL_WINDOW_HEIGHT 1

/* Preferred video driver for WSL */
// x11, Wayland
#define SDL_VIDEO_DRIVER "x11"

#endif
