# Agent Guidelines for gh_terminal

## Build & Run
- **Build**: `make clean && make`
- **Run**: `./midifall <folder_with_mid_and_opus_files>`
- **Clean**: `make clean`
- No tests currently exist

## Code Style
- **Language**: C11 (`-std=c11`)
- **Compiler**: gcc with `-O2 -Wall -Wextra`
- **Imports**: System headers grouped, SDL2/opus includes at top with `<angle brackets>`
- **Defines**: Feature macros first (`_DEFAULT_SOURCE`, `_POSIX_C_SOURCE`), then constants with `#define`
- **Naming**: `snake_case` for functions/variables, `UPPER_CASE` for constants/macros
- **Formatting**: 2-space indents, K&R brace style, 80-char soft limit
- **Error handling**: Check return values, print to stderr with `SDL_GetError()` or errno context
- **Memory**: Always free allocated resources, use `atexit()` for cleanup handlers
- **Comments**: Section headers with `/* --- */`, inline comments for non-obvious logic

## Architecture
- Single file (`main.c`, ~1750 lines) with sections: terminal display, MIDI parsing, audio engine, game loop
- SDL2 for audio/events, opusfile for multi-track playback, ANSI escape codes for terminal rendering
- **Critical**: Audio latency compensated via `buffer_size * 2` subtraction in `audio_time_sec()`
- WSL2/WSLg environment: Wayland driver preferred, X11 fallback
