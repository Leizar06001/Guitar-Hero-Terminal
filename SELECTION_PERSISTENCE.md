# Selection Persistence Feature

## Overview
The game now remembers your last selected song and difficulty, making it faster to resume playing your favorite songs.

## Features

### 1. Last Selected Song Memory
**What it does**:
- When you select a song, the game saves that selection
- Next time you open the game, the song list starts at your last selection
- Cursor automatically positions at the last played song

**How it works**:
```c
// On song selection (Enter key):
settings->last_song_index = selected;
settings_save(settings);

// On next launch:
int selected = settings->last_song_index;
if (selected < 0 || selected >= count) {
  selected = 0;  // Fallback to first song if invalid
}
```

**Edge cases handled**:
- Song deleted: Falls back to first song (index 0)
- Song list changed: Clamps to valid range
- First launch: Defaults to first song

### 2. Last Selected Difficulty Memory
**What it does**:
- When you select a difficulty, the game saves that preference
- Next song selection defaults to your last chosen difficulty
- If that difficulty isn't available, falls back to highest available

**How it works**:
```c
// On difficulty selection (Enter key):
settings->last_difficulty = selected;
settings_save(settings);

// On next song:
int selected = settings->last_difficulty;
if (!available[selected]) {
  // Fallback: find first available (expert → hard → medium → easy)
  for (int i = 3; i >= 0; i--) {
    if (available[i]) {
      selected = i;
      break;
    }
  }
}
```

**Smart fallback**:
- Preferred difficulty unavailable → selects highest available difficulty
- Example: Last played Expert, new song only has Medium → selects Medium
- Preserves Expert preference for next song that has it

## Settings File Format

### Location
```
~/.midifall_settings
```

### New Fields
```ini
last_difficulty=3        # 0=Easy, 1=Medium, 2=Hard, 3=Expert
last_song_index=5        # Zero-based index in song list
```

### Complete Example
```ini
key_fret_green=122
key_fret_red=120
key_fret_yellow=99
key_fret_blue=118
key_fret_orange=98
lookahead_sec=2.00
key_strum=13
global_offset_ms=-360.0
inverted_mode=0
last_difficulty=3
last_song_index=5
```

## User Experience Examples

### Example 1: Typical Session
```
Session 1:
1. Launch game
2. Song list opens (cursor at Song #1 by default)
3. Scroll down to "Through the Fire and Flames" (song #5)
4. Press Enter
5. Select "Expert" difficulty
6. Play song
7. Quit game

Session 2 (next launch):
1. Launch game
2. Song list opens with cursor ALREADY at "Through the Fire and Flames" ✨
3. Press Enter (no scrolling needed!)
4. Difficulty selector defaults to "Expert" ✨
5. Press Enter
6. Play song immediately
```

### Example 2: Song No Longer Has Expert
```
Last played: "Song A" on Expert

New session:
1. Launch game
2. Select "Song B" (only has Easy/Medium/Hard)
3. Difficulty menu opens
4. Expert is grayed out
5. Cursor defaults to Hard (highest available) ✨
6. Your Expert preference is PRESERVED for next song
```

### Example 3: Song Deleted
```
Last played: Song #10 (deleted)

New session:
1. Launch game
2. Song list only has 8 songs now
3. Cursor defaults to Song #1 (safe fallback)
4. Select any song
5. New selection saved for next time
```

## Implementation Details

### Settings Structure
```c
typedef struct {
  // ... existing fields ...
  int last_difficulty;   // 0-3 (easy/medium/hard/expert)
  int last_song_index;   // 0-based index
} Settings;
```

### Save Points
**Song selection saved**:
- When user presses Enter on song selector
- Before parsing notes file
- Immediately written to disk

**Difficulty selection saved**:
- When user presses Enter on difficulty selector
- After validating it's available
- Immediately written to disk

### Validation
**Song index validation**:
```c
if (selected < 0 || selected >= song_count) {
  selected = 0;
}
```

**Difficulty validation**:
```c
if (last_difficulty < 0 || last_difficulty > 3) {
  last_difficulty = 3;  // Default to Expert
}

if (!available[last_difficulty]) {
  // Find highest available difficulty
  for (int i = 3; i >= 0; i--) {
    if (available[i]) {
      last_difficulty = i;
      break;
    }
  }
}
```

## Persistence Behavior

### What IS Saved
✅ Last selected song index
✅ Last selected difficulty
✅ Saved on every selection (not just on quit)
✅ Persists across game restarts

### What IS NOT Saved
❌ Scroll position in song list (always centers on selection)
❌ Menu state (options menu vs song list)
❌ Song playback position
❌ Temporary selections (only confirmed selections)

## Benefits

### User Experience
- **Faster access**: No scrolling through long song lists
- **Muscle memory**: Same difficulty every time
- **Resume play**: Quick return to favorite songs
- **Smart defaults**: Automatic fallback when needed

### Technical
- **Minimal storage**: 2 integers (8 bytes)
- **Instant load**: No performance impact
- **Atomic writes**: Settings saved immediately on selection
- **Safe fallbacks**: Never crashes from invalid data

## Edge Cases

### Song List Changes
| Scenario | Behavior |
|----------|----------|
| Song added at end | Previous selection still valid |
| Song removed from middle | Index clamped to last song |
| All songs removed | Fallback to index 0 (first song) |
| Songs folder empty | Game exits before using index |
| Songs reordered | May select different song (uses index, not name) |

### Difficulty Changes
| Scenario | Behavior |
|----------|----------|
| Last diff available | Selects last difficulty |
| Last diff unavailable | Selects highest available |
| No difficulties available | Game exits before showing menu |
| Song has only Easy | Selects Easy (overrides Expert preference temporarily) |

### Settings File Issues
| Scenario | Behavior |
|----------|----------|
| File doesn't exist | Creates new with defaults |
| File corrupted | Ignores bad lines, uses defaults |
| Values out of range | Clamps to valid range |
| Negative values | Clamps to 0 |

## Testing Checklist

- [x] Select song → quit → relaunch → same song selected
- [x] Select difficulty → next song → same difficulty selected
- [x] Delete last played song → relaunch → falls back to first song
- [x] Select Expert → play song without Expert → falls back to Hard
- [x] ESC from difficulty → re-select song → last difficulty remembered
- [x] Empty song list → doesn't crash
- [x] First launch (no settings file) → defaults work
- [x] Settings file with only old fields → new fields added on save
- [x] Corrupted settings file → ignores corruption, uses defaults

## Future Enhancements

Possible improvements:
- Remember song selection **per folder** (if multiple song folders supported)
- Save song name instead of index (survives song list reordering)
- Remember difficulty per song (different songs, different difficulties)
- Save last 10 played songs (quick history)
- "Favorites" system (mark songs, show at top)
