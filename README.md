# Pac-Man 256

[![WASM Demo](https://img.shields.io/badge/Demo-WASM-blue?style=flat-square&logo=webassembly)](https://seaus-tech.github.io/PACMAN256-C/pacman256.html)
[![Language-C99](https://img.shields.io/badge/Language-C99-A8B9CC?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C99)
[![License: MIT](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)

A Pac-Man 256 clone written in C99 using the Sokol headers for platform abstraction.

> **Play in Browser**: [WASM Demo](https://seaus-tech.github.io/PACMAN256-C/pacman256.html)

## 📖 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Quick Start](#quick-start)
- [Requirements](#requirements)
- [Build & Run](#build--run-native)
- [Build for WebAssembly](#build-for-webassembly)
- [Controls](#controls)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Development](#development)
- [Credits](#credits)
- [License](#license)

## Overview

This repository contains a C99 implementation of **Pac-Man 256**, the procedural endless-corridor arcade game. Written entirely in C, the game bypasses web-view engines and runs directly on metal via Sokol, featuring real-time generated synth chiptunes and responsive controls.

## Features

- 🌀 **Endless Scrolling Maze** - Procedurally generated maze that scrolls upward infinitely
- ⚡ **The Glitch** - Corrupted wall scrolls up from the bottom, creating time pressure
- 🪄 **Power-ups** - 15+ power-ups including:
  - **Laser** - Fires beam to destroy ghosts
  - **Freeze** - Slows all ghosts
  - **Bomb** - Explosion radius damage
  - **Giant** - Pac-Man grows huge
  - **Tornado, Stealth, Fire, Lightning, Magnet, Clone, Paint, Speed, Regen, Hammer, Spiral**
- 👻 **Ghost AI** - 4 classic ghosts + 3 new variants with unique behaviors
- 🔗 **Combo System** - Chain consecutive dots for score multipliers
- 🌐 **Web Build** - Compiles to WebAssembly for browser play

## Quick Start

```bash
git clone https://github.com/Seaus-tech/PACMAN256-C.git
cd PACMAN256-C
```

## Requirements

- CMake 3.10+
- C99 compiler (clang, gcc, MSVC)
- Platform-specific dependencies:
  - **macOS**: Xcode Command Line Tools
  - **Linux**: libx11-dev, libgl1-mesa-dev, libasound2-dev
  - **Windows**: Visual Studio or MinGW

## Build & Run (Native)

```bash
mkdir build
cd build
cmake ..
cmake --build .
./pacman256
```

## Build for Web (WebAssembly)

```bash
mkdir build
cd build
emcmake cmake ..
cmake --build .
# Open pacman256.html in a web browser
```

## Controls

| Key | Action |
|-----|--------|
| Arrow Keys | Move Pac-Man |
| 1, 2, 3 | Activate power-up slots 1, 2, or 3 |
| Esc | Exit |

## Architecture

The code follows a single-file design with no dynamic allocations. Key systems:

- **Game Tick** - Core 60Hz game loop
- **Time Triggers** - Simple event system for delayed actions
- **Procedural Maze** - Generates maze chunks as the glitch scrolls
- **Power-up Manager** - Tracks active power-ups and effects
- **Audio Emulation** - Namco WSG sound chip emulator
- **Rendering** - Tile-based with sprite overlay

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

© 2026 Seaus Tech. All rights reserved.