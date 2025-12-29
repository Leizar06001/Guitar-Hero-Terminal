# Celebration Effects Feature

## Overview
Automatic visual celebration effects that spawn randomly across the screen when the player builds their streak. Effects start at half bar and intensify as the streak grows.

## Trigger Condition

### When Effects Start
Celebration effects begin spawning when the **streak bar reaches 50% full**:
- **Streak threshold**: `(MAX_MULTIPLIER - 1) × STREAK_DIVISOR / 2`
- **Default values**: `(4 - 1) × 10 / 2 = 15 streak`
- Effects start immediately upon reaching this threshold

### When Effects Stop
Effects stop spawning instantly when:
- Player makes a mistake (streak resets)
- Streak drops below half bar threshold
- Game is paused

## Behavior

### Intensity Scaling
The celebration system **intensifies as the streak bar fills**:

**At 50% bar (15 streak)**:
- Spawn rate: Every ~2.0 seconds (slowest)
- Effect: Slow, occasional celebrations

**At 75% bar (22 streak)**:
- Spawn rate: Every ~1.15 seconds
- Effect: Moderate celebration frequency

**At 100% bar (30 streak)**:
- Spawn rate: Every ~0.3 seconds (fastest)
- Effect: Rapid, intense celebrations

**Formula**: `cooldown = 2.0 - (progress × 1.7)` where progress = 0.0 (half bar) to 1.0 (full bar)

### Timing
- **First effect**: Spawns with cooldown based on current bar position
- **Subsequent effects**: Cooldown recalculated each spawn based on current streak
- **Variance**: ±10% random variation to prevent predictable patterns
- **Dynamic**: Cooldown decreases in real-time as streak grows

### Effect Types
Randomly selects between:
- **MULTILINE_EFFECT_EXPLOSION (100)**: Expanding burst animation
- **MULTILINE_EFFECT_SPARKLE (101)**: Pulsing sparkle animation
- 50% chance for each type per spawn

### Spawn Locations
Effects spawn in **safe zones** to avoid covering gameplay:

**Left Zone**:
- X position: Between streak bar and left edge of lanes
- Range: `x = 5` to `lane_start - 5`

**Right Zone**:
- X position: Right of lanes with margin
- Range: `x = lane_end + 15` to `cols - 5`

**Vertical Range**:
- Y position: Between top of gameplay area and near hit line
- Range: `y = top_y` to `hit_y - 5`

**Zone Selection**: 50% chance left or right zone each spawn

## Visual Examples

### 50% Bar (Streak = 15) - Slow Celebrations
```
 Score: 1500  Streak: 15    [Effect every ~2 seconds]
 #####                                    
 #####  |  [###]  |                       
 #####  |  [###]  |                            \   /
 .....  |  [###]  |                             ***
 .....  |  [###]  |                            /   \
 .....  |  [###]  |  
2x < O  O  O  O >
    ===========
```

### 75% Bar (Streak = 22) - Moderate Celebrations
```
 Score: 3200  Streak: 22    [Effect every ~1.15 seconds]
 #######                        .              *
 #######  |  [###]  |           .             * *
 #######  |  [###]  |          .               *
 #######  |  [###]  |
 ###....  |  [###]  |  
 ....... |  [###]  |
3x < O  O  O  O >
    ===========
```

### 100% Bar (Streak = 30) - Intense Celebrations
```
 Score: 5000  Streak: 30    [Effect every ~0.3 seconds - RAPID!]
 ########           \   /        .              *
 ########            ***         .             * *
 ########  |  [###] /   \       .               *
 ########  |  [###]  |
 ########  |  [###]  |     *
 ########  |  [###]  |    * *
4x < O  O  O  O >            *
    ===========
```

### After Miss (Streak = 0)
```
 Score: 5200  Streak: 0    [All celebration effects stop spawning]
 .
 .   |  [###]  |
 .   |  [###]  |
 .   |  [###]  |  
 .   |  [###]  |
     |  [###]  |
     |  [###]  |
1x < O  O  O  O >    XXXXX  [Miss indicator]
    ===========
```

## Implementation Details

### State Tracking (main.c)
```c
int was_at_max_multiplier = 0;      // Track celebration state changes
double next_celebration_time = 0.0; // When next effect should spawn
double celebration_cooldown = 0.0;  // Current cooldown duration
```

### Cooldown Calculation
```c
// Calculate current bar position (0.0 to 1.0)
int max_streak = (MAX_MULTIPLIER - 1) * STREAK_DIVISOR;  // 30
int half_streak = max_streak / 2;                         // 15
double progress = (streak - half_streak) / (max_streak - half_streak);

// Scale cooldown from 2.0s (half) to 0.3s (max)
double base_cooldown = 2.0 - (progress * 1.7);

// Add ±10% random variance
celebration_cooldown = base_cooldown * (0.9 + random(0.0, 0.2));
```

### Progression Table
| Streak | Bar % | Progress | Base Cooldown | Spawn Rate |
|--------|-------|----------|---------------|------------|
| 15     | 50%   | 0.00     | 2.00s         | 0.5 /sec   |
| 18     | 60%   | 0.20     | 1.66s         | 0.6 /sec   |
| 21     | 70%   | 0.40     | 1.32s         | 0.76 /sec  |
| 24     | 80%   | 0.60     | 0.98s         | 1.02 /sec  |
| 27     | 90%   | 0.80     | 0.64s         | 1.56 /sec  |
| 30     | 100%  | 1.00     | 0.30s         | 3.33 /sec  |

### Spawn Logic
```c
// Check if above half bar
int half_streak_for_max_mult = (MAX_MULTIPLIER - 1) * STREAK_DIVISOR / 2;
int celebration_active = (st.streak >= half_streak_for_max_mult);

// First time reaching half - schedule first effect
if (celebration_active && !was_at_max_multiplier) {
  calculate_cooldown_based_on_current_streak();
  next_celebration_time = current_time + cooldown;
}

// Spawn effects at scheduled times
if (celebration_active && current_time >= next_celebration_time) {
  spawn_random_celebration_effect();
  calculate_cooldown_based_on_current_streak();  // Recalculate!
  next_celebration_time = current_time + cooldown;
}
```

### Safe Zone Calculation
```c
// Calculate lane boundaries
int grid_w = lanes * lane_w;
int x0 = (cols - grid_w) / 2;

// Define safe zones
int left_zone_x = 5;                    // After streak bar
int right_zone_x = x0 + grid_w + 15;    // After lanes + margin
int safe_y_min = top_y;
int safe_y_max = hit_y - 5;

// Random position in selected zone
spawn_x = zone_start + rand() % (zone_width);
spawn_y = safe_y_min + rand() % (safe_y_max - safe_y_min + 1);
```

## Configuration

### Scaling Parameters
Edit celebration logic in `main.c`:
```c
// Cooldown range: 2.0s (half bar) to 0.3s (full bar)
double base_cooldown = 2.0 - (progress * 1.7);
//                     ^^^              ^^^
//                     MAX              RANGE (2.0 - 0.3 = 1.7)

// Random variance: ±10%
cooldown = base_cooldown * (0.9 + ((double)rand() / RAND_MAX) * 0.2);
//                          ^^^                                  ^^^
//                          -10%                                 +10%
```

### Start Threshold
```c
// Start celebrations at 50% bar
int half_streak = max_streak_for_max_mult / 2;
//                                          ^
//                                          Change divisor (2 = 50%, 3 = 33%, etc.)
```

### Effect Duration
Effects last 0.5 seconds (defined in spawn call):
```c
add_multiline_effect(spawn_x, spawn_y, effect_type, 0.5, 5, 3);
```

## Technical Notes

### Performance
- **Dynamic recalculation**: Cooldown updates every spawn (O(1) per spawn)
- **Intensity scales naturally**: No separate timers or complex logic
- **Bounded rate**: Even at max (0.3s), only ~3 effects/sec
- **Auto-cleanup**: Effects expire after 0.5s, limiting memory usage

### Randomness
- Uses standard `rand()` function
- Should call `srand()` at game start for variety
- Each spawn independently randomized for type, position, and timing
- Small variance (±10%) prevents machine-gun effect at max

### Edge Cases Handled
- **Progress clamping**: `if (progress > 1.0) progress = 1.0` prevents overflow
- **Small terminals**: Zones adjust if lanes take full width
- **Zone fallback**: If no safe zone available, uses `cols/4`
- **Boundary clipping**: Effects won't render outside terminal bounds
- **Pause state**: Celebration stops when `menu_state != MENU_NONE`

### State Management
- `was_at_max_multiplier` tracks when above/below half bar (renamed but same logic)
- Prevents re-initialization on every frame
- Cleanly stops when dropping below half bar threshold
- Cooldown recalculates dynamically as streak changes

## Integration with Gameplay

### Synergy with Other Systems
- **Streak bar**: Visual indicator of celebration intensity (higher = faster)
- **Multiplier display**: Shows 2x, 3x, 4x as milestones
- **Sustain flames**: Can occur simultaneously with celebrations
- **Hit effects**: Lane-based effects continue normally

### Player Feedback Loop
1. Player builds streak → bar fills → celebrations start at 50%
2. Streak continues growing → celebrations speed up visually
3. At max multiplier → screen fills with rapid effects
4. Make mistake → celebrations stop → clear feedback of streak loss
5. Rebuild streak → celebrations resume and intensify again

## Example Session

**Time 10s**: Player at 14 streak (just below half)
- No celebrations
- Bar at 47%

**Time 15s**: Hit note, streak = 15 (half bar reached!)
- Celebration timer starts: ~2.0s cooldown
- Bar at 50%

**Time 17.0s**: First explosion spawns (right zone)
- Effect lasts 0.5s
- Next celebration scheduled: ~2.0s (still at 15 streak)

**Time 20s**: Player at 20 streak (67% bar)
- Cooldown now ~1.5s (faster!)
- Sparkle spawns

**Time 21.5s**: Another effect (cooldown decreasing)

**Time 25s**: Player at 30 streak (100% bar!)
- Cooldown now ~0.3s (very fast!)
- Effects spawning rapidly

**Time 26s**: 3-4 effects visible simultaneously
- Screen feels alive with celebrations

**Time 28s**: Player misses note, streak = 0
- All future celebrations cancelled
- Current effects finish their 0.5s duration
- Screen quiets down

## Future Enhancements

Possible improvements:
- More effect types at higher intensities
- Different celebration colors per multiplier tier
- Special "mega" effect at full bar
- Configurable intensity curve (linear/exponential)
- Beat-synced spawning at max intensity
- Particle trails at highest streaks

