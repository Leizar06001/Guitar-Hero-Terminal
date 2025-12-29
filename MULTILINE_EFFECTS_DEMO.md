# Multiline Effects System - Usage Guide

The multiline effects system has been added alongside existing single-line effects without breaking compatibility.

## Features

- **Separate effect system** - Multiline effects run independently from lane-based effects
- **Frame-based animation** - Effects transition through multiple frames based on elapsed time
- **Customizable** - Define your own ASCII art frames and animation patterns
- **Sustain flames** - Automatic flame effects when holding long notes
- **Celebration effects** - Random effects spawn when at max multiplier streak

## Built-in Effects

### MULTILINE_EFFECT_EXPLOSION (100)
3 frames, 5x3 size, expanding explosion animation:
```
Frame 0:  \|/      Frame 1: \   /     Frame 2: .   .
         -.*.-               ***                .
          /|\               /   \             .   .
```
**Uses**: Celebration effects, manual spawning

### MULTILINE_EFFECT_SPARKLE (101)
4 frames, 5x3 size, pulsing sparkle animation:
```
Frame 0:   *       Frame 1:  ***      Frame 2: *   *     Frame 3: .   .
          * *               *   *               *                .
           *                 ***              *   *            .   .
```
**Uses**: Celebration effects, manual spawning

### Sustain Flames (Automatic)
Special continuous effect that appears on both sides of lanes when holding long notes:
- **Auto-triggered** - Appears when player holds correct frets during a sustain note
- **Auto-removed** - Disappears immediately when player releases or sustain ends
- **Vertical flames** - Extends from ~75% up the screen down to hit line
- **Animated** - 4-frame cycling animation (`^ * ) ^`)
- **Lane-colored** - Flames match the color of the sustained lane

See `SUSTAIN_FLAMES_FEATURE.md` for details.

### Celebration Effects (Automatic)
Random explosions and sparkles spawn when at maximum multiplier:
- **Auto-triggered** - Starts when streak reaches `(MAX_MULTIPLIER-1) × STREAK_DIVISOR` (default: 30)
- **Random timing** - Spawns every 0.7 to 2.0 seconds (randomized)
- **Random position** - Left or right safe zones (avoids lanes and UI)
- **Random type** - 50% explosion, 50% sparkle
- **Auto-stopped** - Stops immediately when streak breaks

See `CELEBRATION_EFFECTS.md` for details.

## API Usage

### Add a multiline effect
```c
#include "terminal.h"

// Spawn explosion at screen position (x=40, y=10), lasting 0.5 seconds
add_multiline_effect(40, 10, MULTILINE_EFFECT_EXPLOSION, 0.5, 5, 3);

// Spawn sparkle at (x=60, y=15), lasting 0.3 seconds  
add_multiline_effect(60, 15, MULTILINE_EFFECT_SPARKLE, 0.3, 5, 3);
```

### Set sustain flames (automatic in gameplay)
```c
// Called internally by main game loop
// lane_mask: bitmask of lanes with active sustains (0-31)
set_sustain_flames(uint8_t lane_mask);

// Example: Show flames on lanes 0 and 2
set_sustain_flames((1 << 0) | (1 << 2));  // 0b00101

// Clear all flames
set_sustain_flames(0);
```

### Update effects (already integrated in main loop)
```c
update_multiline_effects(dt);  // Called once per frame in main.c
```

## Automatic Systems

### Sustain Flames
**Trigger**: Holding correct frets during note with `duration > 0.1s`

Every frame, the system:
1. Scans chords near cursor for active sustains
2. Checks if player holds correct frets
3. Updates flame display per lane

### Celebration Effects
**Trigger**: Streak reaches max multiplier threshold

When at max multiplier:
1. Initialize random timer (0.7-2.0s) on first reach
2. Spawn random effect (explosion/sparkle) at timer expiry
3. Choose random safe zone position
4. Schedule next effect with new random timer
5. Stop all spawning when streak breaks

## Adding Custom Effects

### 1. Define your frames in terminal.c
```c
static const char* MY_CUSTOM_FRAMES[3][4] = {
  // Frame 0 (4 lines)
  {"  ^  ",
   " /|\\ ",
   " | | ",
   " ' ' "},
   
  // Frame 1
  {" /^\\ ",
   "| | |",
   "| | |",
   "' ' '"},
   
  // Frame 2
  {"/   \\",
   "|   |",
   "|   |",
   "'   '"}
};
```

### 2. Add effect type to config.h
```c
#define MULTILINE_EFFECT_MY_CUSTOM 103
```

### 3. Handle in draw_frame() switch statement (terminal.c ~line 653)
```c
} else if (effect->type == MULTILINE_EFFECT_MY_CUSTOM) {
  frames = (const char **)MY_CUSTOM_FRAMES;
  num_frames = 3;
  color_code = -11; // Choose color: -10=red, -11=cyan, -12=green, -13=yellow
}
```

### 4. Use it
```c
add_multiline_effect(x, y, MULTILINE_EFFECT_MY_CUSTOM, 0.6, 5, 4);
```

## Animation Control

- **Progress**: Calculated as `1.0 - (time_left / time_total)` (0.0 → 1.0)
- **Frame selection**: `frame = (int)(progress * num_frames)`
- **Early frames** show at beginning (progress near 0.0)
- **Late frames** show at end (progress near 1.0)

## Example: Spawn explosion on perfect hit
```c
// In your hit detection code:
if (timing_quality == EFFECT_TYPE_PERFECT) {
  add_effect(lane, EFFECT_TYPE_PERFECT, 0.2);  // Lane effect (existing)
  
  // Calculate screen position for multiline effect
  int x = screen_center_x + 10;
  int y = hit_line_y - 5;
  add_multiline_effect(x, y, MULTILINE_EFFECT_EXPLOSION, 0.4, 5, 3);
}
```

## Configuration

Edit `config.h`:
```c
#define MAX_MULTILINE_EFFECTS 16  // Max concurrent multiline effects
#define MAX_MULTIPLIER 4          // Max multiplier (affects celebration trigger)
#define STREAK_DIVISOR 10         // Streak per multiplier level
```

**Celebration trigger calculation**: `(MAX_MULTIPLIER - 1) × STREAK_DIVISOR`
- Default: `(4 - 1) × 10 = 30 streak`

## Notes

- Effects draw **on top** of gameplay elements but respect screen boundaries
- Transparent spaces (` ` character) won't overwrite existing content
- Effects auto-remove when `time_left <= 0`
- Uses same color system as lane effects (-10 to -13 for special colors)
- Sustain flames use a **static variable** for animation timing to ensure smooth cycling
- Flame height is **responsive** to terminal size (scales with gameplay area)
- Celebration effects use **safe zones** to avoid covering lanes or UI elements
- Multiple systems can run simultaneously (sustains + celebrations + manual effects)


