# Sustain Flames Feature

## Overview
Automatic visual effect that displays animated flames on both sides of lanes when the player holds long notes (sustains).

## Behavior

### When Flames Appear
Flames are displayed when **ALL** of these conditions are met:
1. A note with `duration > 0.1 seconds` is currently active
2. Current game time is between note start and note end
3. Player is holding the **correct fret(s)** for that note

### When Flames Disappear
Flames disappear **immediately** when:
- Player releases any required fret
- The sustain note ends
- Player pauses the game

## Visual Design

### Position
- **Left side**: 2 characters to the left of each lane
- **Right side**: 1 character to the right of each lane
- **Height**: Extends from ~75% up the screen down to 2 rows above the hit line

### Animation
- **4-frame cycle**: `^ → * → ) → ^`
- **Staggered rows**: Each vertical row is offset by 1 frame, creating a "wave" effect
- **Frame rate**: ~10 frames per second (fast cycling)
- **Color**: Matches the lane color (green/red/yellow/blue/orange)

### Example
```
Lane with active sustain (green lane 0):

    ^  [###]  ^    <- Top of flame area
    *  [###]  *
    )  [###]  )
    ^  [###]  ^
    *  [###]  *
    )  [###]  )
  [ O  O  O ]      <- Hit line (fret pressed)
```

## Implementation Details

### New Variables (main.c)
```c
size_t sustain_cursor = 0;     // Track which chords to check for sustains
uint8_t active_sustains = 0;   // Bitmask of lanes with active sustains (0-31)
```

### Tracking Logic (main.c game loop)
Every frame, the system:
1. Scans chords near the cursor position
2. For each chord, checks if:
   - Note is currently playing (`note_time <= t <= note_end`)
   - Note has sustain (`duration > 0.1`)
   - Player holds correct frets (`(held & note_mask) == note_mask`)
3. Builds a bitmask of active sustains
4. Updates flame display via `set_sustain_flames(active_sustains)`

### Rendering (terminal.c)
```c
void set_sustain_flames(uint8_t lane_mask);  // Set which lanes show flames

// In draw_frame():
if (g_sustain_flames & (1 << lane)) {
  // Draw flames on both sides of this lane
  // Use time-based animation cycling
}
```

## Technical Notes

### Performance
- **Minimal overhead**: Only scans a small window of chords (cursor ± 5)
- **Early cleanup**: Old sustains pruned from tracking via `sustain_cursor`
- **Efficient rendering**: Flames drawn to screen buffer, not individual prints

### Synchronization
- Uses a **static variable** `flame_time` for animation timing
- Ensures all lanes animate in sync
- Row offset creates visual wave effect

### Edge Cases Handled
- Multiple simultaneous sustains (different lanes)
- Chord sustains (multiple lanes, same timing)
- Screen boundary clipping (flames don't render off-screen)
- Terminal resize (flame height scales with gameplay area)

## Example Use Case

**Song**: Guitar solo with long held notes
**Player action**: Holds green fret (lane 0) during 2-second sustain

**Result**:
- Green flames appear immediately on both sides of lane 0
- Flames animate continuously during the entire 2 seconds
- Flames disappear the instant player releases the green fret OR sustain ends
- If player briefly releases and re-grabs, flames flicker off/on accordingly

## Integration Points

### Files Modified
- `config.h` - Added `MULTILINE_EFFECT_FLAME` constant (102)
- `terminal.h` - Added `set_sustain_flames()` declaration
- `terminal.c` - Implemented flame rendering and `FLAME_FRAMES` data
- `main.c` - Added sustain tracking logic in game loop

### No Breaking Changes
- All existing effects continue to work unchanged
- Flame system runs independently
- Can be disabled by commenting out `set_sustain_flames()` call

## Future Enhancements

Possible improvements:
- Configurable flame height via settings
- Different flame styles per difficulty
- Intensity based on sustain length
- Color shift for perfect sustain timing
- Particle effects when sustain ends successfully
