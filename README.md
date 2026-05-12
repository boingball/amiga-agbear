# Amiga AGBear

AG Bear themed AmigaOS experiments.

This repository contains experimental AmigaOS applications based around an AG Bear-style talking bear / virtual pet concept.

## Status

Prototype / preservation state.

This project is being preserved first and cleaned later. Some files are experimental and may require specific AmigaOS libraries, image assets, or sound files to run correctly.

## Included Projects

### AG Bear Talker

A talking AG Bear app using:

- `narrator.device`
- `translator.library`
- GadTools UI
- ILBM background image
- mouth open/closed overlays

### AG Bear Virtual Pet Prototype

A larger Tamagotchi-style AG Bear prototype using:

- Hardware BOB/sprite-style rendering experiments
- GadTools UI
- timer.device
- narrator.device / translator.library
- icons for actions such as play, clean, sleep, medicine and stats
- sound effect support
- simple virtual pet behaviour

## Target

- AmigaOS 3.x
- m68k-amigaos-gcc / Bebbo GCC
- picture.datatype for ILBM loading
- narrator.device and translator.library for speech

## Files

- `ag-bear-protov4.c` - larger AG Bear virtual pet prototype
- `AGBear-Talker/` - talking AG Bear app / related files
- `icons/` - ILBM icon assets
- `sounds/` - sound assets
- `Old-ProtoTypes/` - older experiments retained for reference

## Build Notes

Build commands may vary depending on the exact source file and toolchain setup.

Example:

```bash
m68k-amigaos-gcc -m68020 -O2 -noixemul ag-bear-protov4.c -o agbear -lamiga

Asset Notes

Source code is intended for open preservation.

The included sound samples were recorded from the author's own AG Bear toys for preservation and experimentation.

Bear images were photographed/digitised by the author from their own collection.

Some background artwork was generated with ChatGPT/OpenAI image tools for use in this project.

Source code is released under the repository licence. Assets are included for preservation and demonstration of the application.

## Licence

Source code: MIT Licence.

Assets: included for preservation, testing, and demonstration. Please do not reuse the AG Bear sound/image assets commercially without checking rights.

AG Bear is a vintage toy originally produced by Axlon. This is an unofficial fan/preservation project.

