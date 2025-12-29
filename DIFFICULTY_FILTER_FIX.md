# Difficulty Menu Filtering - Bug Fix

## Problem
When selecting a difficulty that has no notes in the chart/MIDI file, the program would crash or stop working because it tried to build an empty chord list.

## Solution
Parse the notes file **before** showing the difficulty selector, then filter the menu to only show available difficulties.

## Changes Made

### 1. New Helper Function
```c
static void check_available_difficulties(const NoteVec *notes, int *available) {
  // Scan all notes to determine which difficulties (0-3) exist
  for (size_t i = 0; i < notes->n; i++) {
    int diff = notes->v[i].diff;
    if (diff >= 0 && diff < 4) {
      available[diff] = 1;
    }
  }
}
```

### 2. Updated Difficulty Selector
**Function signature changed**:
```c
// Before:
static int show_difficulty_selector(void)

// After:
static int show_difficulty_selector(const int *available)
```

**Visual changes**:
- Available difficulties: Display in full color (green/yellow/red/magenta)
- Unavailable difficulties: Display in dim gray (`\x1b[2;37m`)
- Default selection: First available difficulty (checking from expert down)

**Navigation changes**:
- Arrow keys skip over unavailable difficulties
- Enter key only works on available difficulties (ignores unavailable)

### 3. Early Note Parsing
**Main flow changed**:
```
Before:
1. Select song
2. Select difficulty
3. Parse notes
4. Build chords (crash if empty!)

After:
1. Select song
2. Parse notes
3. Check available difficulties
4. Select difficulty (filtered)
5. Build chords (guaranteed non-empty)
```

**Implementation**:
- Notes parsed immediately after song selection
- Difficulty menu receives `available_diffs[]` array
- Notes already in memory when building chords (no re-parse)

### 4. ESC Navigation Handling
When user presses ESC to go back to song selector:
1. Clean up current notes (`free(notes.v)`, `free(track_names.v)`)
2. Show song selector again
3. Re-parse notes for newly selected song
4. Re-check available difficulties
5. Show updated difficulty menu

## Visual Example

### Before (All Difficulties)
```
╔════════════════════════════════════════════════════════════════════════════╗
║                        DIFFICULTY SELECTOR                                 ║
╠════════════════════════════════════════════════════════════════════════════╣
║                                                                            ║
║                               Easy                                         ║
║                               Medium                                       ║
║                               Hard                                         ║
║                             ► Expert ◄                                     ║
║                                                                            ║
╚════════════════════════════════════════════════════════════════════════════╝
```

### After (Only Expert and Hard Available)
```
╔════════════════════════════════════════════════════════════════════════════╗
║                        DIFFICULTY SELECTOR                                 ║
╠════════════════════════════════════════════════════════════════════════════╣
║                                                                            ║
║                               Easy         (grayed out)                    ║
║                               Medium       (grayed out)                    ║
║                               Hard                                         ║
║                             ► Expert ◄                                     ║
║                                                                            ║
╚════════════════════════════════════════════════════════════════════════════╝
```

## User Experience

### Selecting Available Difficulty
1. User sees only valid options highlighted
2. Arrow keys skip grayed-out difficulties
3. Enter confirms selection → game loads successfully

### Attempting Unavailable Difficulty
1. Cursor cannot land on grayed-out difficulty
2. Enter key does nothing if somehow on unavailable option
3. No crash possible - always guaranteed valid selection

### Going Back to Song Selector
1. ESC key works as before
2. Notes cleaned up properly
3. New song parsed automatically
4. Difficulty menu updates to show new song's difficulties

## Edge Cases Handled

### No Difficulties Available
If notes file has 0 notes total:
- Program exits with error message before difficulty selector
- Prevents showing empty menu

### Only One Difficulty
- Menu still shows all 4 options
- 3 grayed out, 1 available
- Cursor auto-selects the available one
- User can still press Enter or navigate

### Song Change Via ESC
- Old notes freed properly (no memory leak)
- New notes parsed
- Available difficulties recalculated
- Menu updates automatically

### Multiple MIDI Tracks
- Difficulties based on ALL notes in file (all tracks)
- If track filtering happens later, at least one track has notes

## Benefits

✅ **No crashes** - Impossible to select empty difficulty
✅ **Clear feedback** - Gray = not available
✅ **Smart navigation** - Arrows skip invalid options
✅ **Memory efficient** - Parse once, use throughout
✅ **Backward compatible** - ESC navigation still works

## Testing Checklist

- [x] Song with only Expert → only Expert selectable
- [x] Song with Easy/Medium → Hard/Expert grayed out
- [x] Arrow keys skip grayed difficulties
- [x] Enter does nothing on grayed difficulty
- [x] ESC goes back and re-parses correctly
- [x] No memory leaks on ESC navigation
- [x] Default selection is first available
- [x] Build succeeds without warnings
