# GH_TERMINAL

A terminal-based rhythm game similar to Guitar Hero that plays MIDI or .chart files synchronized with audio.

<img src="https://github.com/Leizar06001/Leizar06001/blob/fc226c9b16255b28db6786bbaba613a550e59389/greenHouse.PNG" width="350"/><img src="https://github.com/Leizar06001/Leizar06001/blob/fc226c9b16255b28db6786bbaba613a550e59389/escapeBomb.PNG" width="350"/>

## Features

- **File Format Support**: Parses both MIDI (.mid) and Clone Hero (.chart) files
- **Sustain Notes**: Visual trails show long notes that need to be held
- **HOPO Detection**: Hammer-ons and pull-offs displayed with special notation (`<*>`)
- **Audio Playback**: SDL2 with Opus multi-track audio (guitar, bass, drums, vocals, backing)
- **Visual Feedback**: 
  - Graphical hit/miss effects on both sides of the lanes
  - Clean fret buttons that only show pressed state
  - Color-coded lanes (Green, Red, Yellow, Blue, Orange)
  - Streak multiplier bar with dynamic colors
- **Album Artwork**: Displays album covers in the song selector using chafa
- **Song Metadata**: Shows artist, year, difficulty stars, and loading phrases
- **Customizable Controls**: Full key binding configuration via settings menu
- **Per-Song Offsets**: Automatic calibration storage for each song
- **Terminal Rendering**: ANSI escape codes for full-color visualization

## Building

### Dependencies
- SDL2
- libopusfile
- chafa (for album artwork display)
- gcc (with C11 support)

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install libsdl2-dev libopusfile-dev chafa gcc make
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc make SDL2-devel opusfile-devel chafa
```

**Arch Linux:**
```bash
sudo pacman -S base-devel sdl2 opusfile chafa
```

**WSL2/WSLg:** Requires WSLg (Windows 11 or Windows 10 with WSL 2.0.0+) for audio/video support.

### Compile
```bash
make clean && make
```

### Run
```bash
./gh_terminal
```

## Getting Songs

Songs must be in **Clone Hero format** (MIDI/chart + Opus audio tracks).

### Download Sources

1. **Chorus (Recommended):** https://chorus.fightthe.pw/
2. **Enchor.us:** https://www.enchor.us/

### Song Format

Each song folder should contain:

#### Required Files
- `notes.mid` OR `notes.chart` - Note data (chart format preferred for sustains)
- `song.opus` - Main audio file (can be multi-track) OR individual stem files
- `song.ini` - Metadata file

#### Optional Files
- `album.jpg` - Album artwork (displayed in song selector at 35x20 characters)
- Individual stem files: `guitar.opus`, `bass.opus`, `drums.opus`, `vocals.opus`, `song.opus` (backing)

### song.ini Format
```ini
[song]
name = Song Title
artist = Artist Name
charter = Charter Name
album = Album Name
year = 2024
diff_guitar = 5
loading_phrase = Fun fact about the song or artist
```

### Example Structure
```
Songs/
  Pink Floyd - Shine On You Crazy Diamond/
    notes.chart
    song.opus
    song.ini
    album.jpg
  Black Sabbath - War Pigs/
    notes.mid
    guitar.opus
    bass.opus
    drums.opus
    vocals.opus
    song.opus
    song.ini
    album.jpg
```

## Controls

### Song Selection Menu
- **↑/↓**: Navigate songs
- **Enter**: Play selected song
- **O**: Open options menu (configure keys & offset)
- **q/ESC**: Quit application

### Gameplay
**Fret Buttons (Default - customizable in Options):**
- **Green**: `a`
- **Red**: `s`
- **Yellow**: `d`
- **Blue**: `f`
- **Orange**: `g`

**Strum:**
- **Strum**: `space`

**Other Controls:**
- **ESC**: Open pause menu (Resume/Restart/Options/Exit)
- **+/=**: Increase timing offset by 10ms
- **-**: Decrease timing offset by 10ms
- **Q**: Quit to song selection
- **Backspace**: Return to song list

### Options Menu

Accessible from song selection or pause menu:
- **Rebind Keys**: Press Enter on any key binding, then press your desired key
- **Adjust Offset**: Fine-tune timing (auto-saves per song)
- **ESC/Back**: Return to previous menu (saves all changes)

## .chart File Format Support

The game fully supports Clone Hero/Phase Shift .chart format:
- **Sections**: `[Song]`, `[SyncTrack]`, `[ExpertSingle]`, `[HardSingle]`, `[MediumSingle]`, `[EasySingle]`
- **Tempo Changes**: Dynamic BPM handling throughout the song
- **Note Format**: `<tick> = N <lane> <duration>`
- **Sustain Notes**: Full support with visual trails that extend through the fret line
- **Resolution**: Configurable ticks per beat

## Visual Features

### Sustain Trails
Long notes display vertical trails (`|`) that extend from the note head down through the fret line. The trail remains visible until the sustain ends, making it clear when to hold and release notes.

### Hit Feedback
Effects appear on both sides of the lanes:
- **Perfect**: `==*==` (bright yellow) - ≤30ms timing
- **Good**: `--*--` (bright green) - ≤55ms timing
- **OK**: `..o..` (bright cyan) - ≤80ms timing
- **Miss**: `XXXXX` (bright red) - Outside timing window

### Streak Multiplier
Visual bar on the left shows your combo progress with dynamic colors:
- **1x**: Blue (0-9 streak)
- **2x**: Green (10-19 streak)
- **3x**: Magenta (20-29 streak)
- **4x**: Yellow (30+ streak)

The bar fills up as you build your streak, showing progress toward the next multiplier.

### Note Types
- **Regular Notes**: `[#]` - Requires strum
- **HOPO Notes**: `<*>` - Hammer-on/Pull-off, no strum required after first note
- **Chords**: Multiple lanes light up - Hold all frets and strum once

## Timing & Scoring

- **Perfect Hit**: ≤30ms timing window - 100 points
- **Good Hit**: ≤55ms timing window - 70 points
- **OK Hit**: ≤80ms timing window - 50 points
- **Miss**: Outside timing window - breaks streak, 0 points

Score multiplier increases with note streak:
- 10 notes: 2x multiplier
- 20 notes: 3x multiplier
- 30+ notes: 4x multiplier (max)

## Configuration

Settings are stored in `settings.ini` including:
- Key bindings for all frets and strum
- Global audio offset (milliseconds)
- Per-song offsets (automatically saved when adjusted in-game)

All changes in the Options menu are automatically saved.

## Troubleshooting

**No audio output:**
- Ensure SDL2 audio drivers are installed
- On WSL2, verify WSLg is working
- Try adjusting buffer size in `config.h` (AUDIO_BUFFER_SIZE)

**Notes feel off-time:**
- Use `+`/`-` during gameplay to adjust timing
- Offset is automatically saved per song

**Terminal too small:**
- Minimum 80x24 characters required
- Resize terminal or reduce font size

**Song won't load:**
- Verify folder contains `notes.mid` or `notes.chart` and at least one `.opus` file
- Check file permissions
- Ensure `song.ini` exists with proper format

**Album artwork not displaying:**
- Ensure `chafa` is installed (`sudo apt install chafa`)
- Verify `album.jpg` exists in the song folder
- Check that terminal supports 256 colors

## Technical Details

- **Language**: C11
- **Compiler**: gcc with `-O2 -Wall -Wextra`
- **Code Style**: K&R bracing, 2-space indents, snake_case naming
- **Architecture**: Modular design with separate files for:
  - `main.c`: Game loop, song loading, input handling (1800+ lines)
  - `terminal.c`: Rendering and visual effects (600+ lines)
  - `midi.c`: MIDI file parsing (580+ lines)
  - `chart.c`: .chart file parsing (260+ lines)
  - `audio.c`: SDL2 audio engine with stem mixing
  - `settings.c`: Configuration management
  - `config.h`: Game constants and defaults

## License

See LICENSE file.
