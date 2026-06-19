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

## Quick Start

### Clone the Repository

```bash
git clone https://github.com/Seaus-tech/PACMAN256-C.git
cd PACMAN256-C
```

### Build & Run (Native)

**Requirements:**
- CMake 3.10+
- C99 compiler (clang, gcc, MSVC)
- Platform-specific dependencies:
  - **macOS**: Xcode Command Line Tools
  - **Linux**: libx11-dev, libgl1-mesa-dev
  - **Windows**: Visual Studio or MinGW

**Build:**
```bash
mkdir build
cd build
cmake ..
cmake --build .
./pacman256
```

### Build for Web (WebAssembly)

**Requirements:**
- Emscripten SDK installed and activated

**Build:**
```bash
mkdir build
cd build
emcmake cmake ..
cmake --build .
# Open pacman256.html in a web browser
```

## Controls

- **Arrow Keys**: Move Pac-Man
- **1, 2, 3**: Activate power-up slots 1, 2, or 3
- **Esc**: Exit

## Architecture

The code follows a single-file design with no dynamic allocations. Key systems:

- **Game Tick**: Core 60Hz game loop
- **Time Triggers**: Simple event system for delayed actions
- **Procedural Maze**: Generates maze chunks as the glitch scrolls
- **Power-up Manager**: Tracks active power-ups and effects
- **Audio Emulation**: Namco WSG sound chip emulator
- **Rendering**: Tile-based with sprite overlay

## Project Structure

```
PACMAN256-C/
├── pacman256.c          # Main game source (~2000 lines)
├── sokol/               # Sokol headers for graphics/audio/input
├── CMakeLists.txt       # Build configuration
├── flake.nix            # Nix development environment
└── README.md            # This file
```

## Development

### Using Nix (optional)

If you have Nix installed, enter the dev environment:
```bash
nix flake update
nix develop
```

### Code Style

The codebase follows the style of the original Pac-Man ROM:
- Single monolithic state struct
- Static functions for encapsulation
- Trigger-based timing system
- No malloc/free (fixed-size allocations)

## Credits

- **Sokol**: https://github.com/floooh/sokol
- **Pac-Man Dossier**: https://www.raphkoster.com/games/pacman/pacmanual.txt
- **Original Pac-Man**: Namco, 1980

## License

This project is based on educational reverse-engineering of Pac-Man. Use for learning purposes.
