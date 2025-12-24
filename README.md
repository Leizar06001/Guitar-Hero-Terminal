# MidiFall

A terminal-based rhythm game inspired by Guitar Hero. Play along to your favorite songs using MIDI note data and multi-track Opus audio in your terminal!

## Requirements

### System Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install build-essential libsdl2-dev libopusfile-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc make SDL2-devel opusfile-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel sdl2 opusfile
```

**WSL2/WSLg:** Requires WSLg (Windows 11 or Windows 10 with WSL 2.0.0+) for audio/video support.

### Build Tools
- GCC compiler with C11 support
- GNU Make
- pkg-config

## Installation

1. Clone or download this repository
2. Build the program:
```bash
make clean && make
```

This produces the `midifall` executable.

## Getting Songs

Songs must be in **Clone Hero format** (MIDI chart + Opus audio tracks).

**Tip:** Press `O` from the song selection screen to configure controls and offset before playing.

### Download Sources

1. **Chorus (Recommended):** https://chorus.fightthe.pw/
2. **Enchor.us:** https://www.enchor.us/

### Song Format

Each song folder should contain:
- `notes.mid` - MIDI file with note chart
- `*.opus` - One or more Opus audio files (guitar, bass, drums, vocals, etc.)
- `song.ini` - (Optional) Metadata with song title, artist, year, HOPO frequency

Example song structure:
```
Songs/
  MyFavoriteSong/
    notes.mid
    guitar.opus
    bass.opus
    drums.opus
    song.ini
```

## Usage

### Quick Start

```bash
./midifall
```
This scans all subfolders in `Songs/` and presents a song selection menu.

### Advanced Usage

**Specify files manually:**
```bash
./midifall path/to/notes.mid --opus guitar.opus --opus bass.opus --opus drums.opus
```

**Options:**
```bash
--difficulty <easy|medium|hard|expert>   # Set difficulty (default: expert)
--offset-ms <milliseconds>               # Timing offset (default: -770ms)
--lookahead-sec <seconds>                # Note visibility time (default: 2.0s)
--gain <track:value>                     # Volume boost (e.g., --gain guitar:1.5)
--mute <track>                           # Mute specific track (e.g., --mute drums)
```

**Example:**
```bash
./midifall Songs/MySong --difficulty hard --offset-ms -800 --gain guitar:2.0 --mute vocals
```

## Controls

### In Song Selection Menu
- `↑`/`↓` - Navigate songs
- `Enter` - Select song
- `O` - Open options menu (configure keys & offset)
- `Escape`/`Q` - Quit application

### In Difficulty Selection Menu
- `↑`/`↓` - Navigate difficulties
- `Enter` - Select difficulty and start
- `Escape` - Return to song selection
- `Q` - Quit application

### In Song Menu
- `Enter` - Start song
- `1-9` - Select audio track to solo
- `0` - Enable all tracks
- `Escape` - Quit

### During Gameplay

**Fret Buttons (Left Hand):** (Default - customizable in Options menu)
- `Z` - Green (Lane 0)
- `X` - Red (Lane 1)
- `C` - Yellow (Lane 2)
- `V` - Blue (Lane 3)
- `B` - Orange (Lane 4)

**Strum (Right Hand):** (Default - customizable in Options menu)
- `Enter` - Strum to hit notes

**Other Controls:**
- `Escape` - Open pause menu (Resume/Restart/Options/Exit)
- `+`/`=` - Increase timing offset by 10ms
- `-` - Decrease timing offset by 10ms
- `1-9` - Solo specific audio track
- `0` - Enable all audio tracks
- `Q` - Quit to song menu

### Pause Menu (ESC)

- **Resume** - Continue playing
- **Restart** - Restart the song from the beginning
- **Options** - Configure controls and offset
- **Song List** - Return to song selection menu
- **Exit** - Quit application

### Options Menu

Customize all controls and settings:
- **Rebind Keys** - Press Enter on any key binding (5 frets + strum), then press your desired key
- **Adjust Offset** - Use `+`/`-` to fine-tune timing (auto-saves)
- **Back** - Return to pause menu (saves all changes)

### Gameplay Tips

1. **Timing calibration:** Use `+`/`-` during gameplay to adjust note timing if hits feel early/late
2. **Track isolation:** Press `1-9` to mute all tracks except the selected one (helpful for learning parts)
3. **HOPO notes:** Hammer-ons and pull-offs appear as smaller notes and don't require strumming
4. **Chords:** Multiple lanes light up simultaneously - hold all required frets and strum once

## Timing & Scoring

- **Perfect Hit:** ≤30ms timing window - 100 points
- **Good Hit:** ≤55ms timing window - 70 points
- **OK Hit:** ≤80ms timing window - 50 points
- **Miss:** Outside timing window - breaks streak

Score multiplier increases with note streak (max 4x).

## Settings & Persistence

All settings are automatically saved to `~/.midifall_settings` and persist across sessions:
- Custom key bindings
- Timing offset adjustments

Settings are saved when:
- You change a key binding in the Options menu
- You adjust offset using `+`/`-` (in-game or in Options menu)
- You exit the Options menu

## Troubleshooting

**No audio output:**
- Ensure SDL2 audio drivers are installed
- On WSL2, verify WSLg is working (`wslg --version`)
- Try adjusting buffer size in `config.h` (AUDIO_BUFFER_SIZE)

**Notes feel off-time:**
- Adjust `--offset-ms` value (more negative = notes appear earlier)
- Calibrate during gameplay with `+`/`-` keys

**Terminal too small:**
- Minimum 80x24 characters required
- Resize terminal or reduce font size

**Song won't load:**
- Verify folder contains `notes.mid` and at least one `.opus` file
- Check file permissions

## Configuration

### Runtime Configuration (Saved Automatically)

Use the in-game **Options menu** (ESC → Options) to customize:
- All key bindings (5 fret buttons + strum)
- Timing offset

These settings persist across sessions in `~/.midifall_settings`.

### Advanced Configuration (Requires Rebuild)

Edit `config.h` before building to customize:
- Default keybindings (if settings file doesn't exist)
- Timing windows
- Visual effects duration
- Audio buffer size (latency)
- Default difficulty/offset/lookahead

After editing, rebuild with `make clean && make`.

## License

See source files for licensing information.