# Pac-Man 256

A Pac-Man 256 clone written in C99 using the Sokol headers for platform abstraction.

## Features

- **Endless Scrolling Maze**: Procedurally generated maze that scrolls upward infinitely
- **The Glitch**: A corrupted wall scrolls up from the bottom, creating time pressure
- **Power-ups**: 15+ power-ups including:
  - Laser (fires beam to destroy ghosts)
  - Freeze (slows all ghosts)
  - Bomb (explosion radius damage)
  - Giant (Pac-Man grows huge)
  - Tornado, Stealth, Fire, Lightning, Magnet, Clone, Paint, Speed, Regen, Hammer, Spiral
- **Ghost AI**: 4 classic ghosts + 3 new variants with unique behaviors
- **Combo System**: Chain consecutive dots for score multipliers
- **Web Build**: Compiles to WebAssembly for browser play

## Building

### Native Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
./pacman256
```

### WebAssembly Build
```bash
mkdir build && cd build
emcmake cmake ..
cmake --build .
# Open pacman256.html in browser
```

## Architecture

The code follows a single-file design with no dynamic allocations. Key systems:

- **Game Tick**: Core 60Hz game loop
- **Time Triggers**: Simple event system for delayed actions
- **Procedural Maze**: Generates maze chunks as the glitch scrolls
- **Power-up Manager**: Tracks active power-ups and effects
- **Audio Emulation**: Namco WSG sound chip emulator
- **Rendering**: Tile-based with sprite overlay

## Credits

- Sokol: https://github.com/floooh/sokol
- Pac-Man Dossier: https://www.raphkoster.com/games/pacman/pacmanual.txt
