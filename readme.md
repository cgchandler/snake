# C64 Snake Game
**A full-featured Snake game for the Commodore 64, written entirely in C using the Oscar64 compiler.**

## Overview
This project is a complete Snake game built for the Commodore 64 in C, compiled using the [Oscar64 compiler](https://github.com/drmortalwombat/oscar64). It started from the [simple Snake example](https://github.com/drmortalwombat/oscar64/blob/main/samples/games/snake.c) included in the Oscar64 sample programs, which demonstrated basic movement and screen output. From that foundation, this project evolved into a polished game featuring smooth animation, joystick and keyboard controls, scoring, growing tail mechanics, HUD indicators, sound effects, and a refined game loop.

The game runs on:
* Real Commodore 64 hardware or FPGA C64 emulators
* [VICE](https://vice-emu.sourceforge.io/) or other C64 emulators

## Screenshots

![Input Selection](Screenshot\_Input\_Selection.jpg "Input Selection")

![Game Play](Screenshot\_Game\_Play.jpg "Game Play")


## Features

### Controls
* Joystick in \*\*Port 2\*\*
* Keyboard controls using \*\*W / A / S / D\*\*
* Joystick button or space bar to pause game

### Gameplay
* Dynamic snake growth
* Wall and self-collision detection
* Heart (fruit) placement
* Speed scaling
* Game Over detection

### Display \& HUD
* Real-time updating score
* Speed indicator
* High score tracking
* Flash animation when a new high score is set
* Clean playfield layout

### SID Sound Effects
* Step sound with each movement tick
* Heart collection sound
* Unique tone for high-score updates

### Improvements Beyond the Oscar64 Sample
This project significantly expands the original Snake sample included with Oscar64:
* **Gameplay Logic** - Collision mechanics and scalable speed
* **Input Handling** - Joystick support plus responsive WASD keyboard input
* **HUD System** - Score, speed, and high score indicator
* **Sound** - Movement, collection, and high-score tones implemented with SID chip
* **Structure** - Clean game loop, modular C code, improved readability, and better state management
* **Polish** - Better visuals, smoother updates, and improved playability

## Build \& Run

### Requirements
* Oscar64 compiler - https://github.com/drmortalwombat/oscar64
* A C64 emulator (recommended: VICE) or real hardware

### Build Steps
* from the command line "oscar64 snake.c"

## Play Online
[Play Snake online running in Vice.js](https://www.cehost.com/snake/)

## License

This project is licensed under the MIT License.

You are free to use, modify, and distribute this software for personal or commercial purposes, provided that the original copyright notice and license text are included with any substantial portions of the code.

See the [LICENSE](LICENSE) file for full details.