# gd-keypad

A custom analog macropad for **Geometry Dash**, built around magnetic Hall-effect switches for continuous key-travel sensing — the same technology used in Wooting and SayoDevice keyboards.

<!-- TODO: hero image of the assembled board or a KiCad 3D render -->
<!-- ![gd-keypad](images/hero.png) -->

---

## Overview

<!-- TODO: 2–3 sentences. What it is, why you built it, what makes it interesting.
Suggested points to cover:
- Hall-effect switches measure how far a key has travelled, not just pressed/not-pressed
- This enables rapid trigger and adjustable actuation, which matter in a timing-based game
- Compact macropad form factor: 3 analog jump keys + 6 mechanical keys + rotary encoder -->

## Features

<!-- TODO: trim/expand as needed -->
- **3 analog Hall-effect keys** with per-key position sensing (rapid trigger, adjustable actuation point)
- **6 mechanical keys** in a diode matrix
- **Rotary encoder** for per-application volume control in Geometry Dash
- **8 kHz USB polling** via the AT32F405's high-speed USB
- USB-C connector, DFU flashing (no external programmer required)
- Configurable through the [hmkconf](https://hmkconf.com/) web tool

## Hardware

<!-- TODO: confirm/adjust each line against your final BOM -->
- **MCU:** Artery AT32F405RCT7 (Cortex-M4F, LQFP-64)
- **Analog switches:** <!-- final switch choice --> with <!-- final sensor, e.g. SS39ET --> linear Hall sensors
- **Mechanical switches:** <!-- hotswap socket type + switch --> 
- **Rotary encoder:** <!-- final encoder part number -->
- **PCB:** 2-layer, 1.2 mm, designed in KiCad, manufactured by JLCPCB

<!-- Optional: a few design highlights that show depth. Examples:
- Separate low-noise LDO for the analog supply to keep the ADC reference clean
- Dedicated 12 MHz crystal required by the high-speed USB PHY
- Solid ground plane opposite the MCU for tight decoupling return paths -->

## Firmware

This board runs [libhmk](https://github.com/peppapighs/libhmk), an open-source Hall-effect keyboard firmware. Analog behaviour (actuation points, rapid trigger, key mapping) is configured at runtime through [hmkconf](https://hmkconf.com/).

On top of the base firmware, this project adds:
- <!-- TODO --> Mechanical matrix scanning
- <!-- TODO --> Rotary encoder handling (volume via F13/F14)
- <!-- TODO --> A companion PC script (AutoHotkey + SoundVolumeView) for per-application Geometry Dash volume control

<!-- TODO: note what is implemented vs planned -->

## Repository layout

<!-- TODO: adjust descriptions to match what actually ends up in each folder -->
```
cad/        Case and knob models
docs/       Design notes and documentation
images/     Renders and photos
kicad/      KiCad project (schematic + PCB)
libraries/  KiCad symbol/footprint libraries used by the project
```

## Build / usage

<!-- TODO: fill in once the board is assembled and firmware is written.
Suggested sections:
- Ordering the PCB (JLCPCB, board settings, BOM/CPL)
- Bill of materials
- Assembly notes (which parts are hand-soldered)
- Flashing the firmware (DFU: hold BOOT0, plug in USB, flash with STM32CubeProgrammer / WebUSB DFU)
- Configuring with hmkconf
- Setting up the volume-control script -->

## Design notes

<!-- Optional but strong for a portfolio. A few bullets on decisions and what you learned.
Examples from this build:
- Why an analog/Hall-effect board over a standard mechanical one
- The two-LDO split for analog supply isolation
- Why the crystal must be 12 MHz (fixed input to the USB high-speed PLL)
- Building on an existing open-source design (HE60) vs designing from scratch -->

## Acknowledgements

This project builds on the work of others in the open-source Hall-effect keyboard community:

- **[libhmk](https://github.com/peppapighs/libhmk)** and **[HE60](https://github.com/peppapighs/HE60)** by [peppapighs](https://github.com/peppapighs) — firmware and reference hardware design that this board is based on
- **[marbastlib](https://github.com/ebastler/marbastlib)** by [ebastler](https://github.com/ebastler) — KiCad footprint library for the Hall-effect and mechanical switches

## License

<!-- TODO: choose a license. MIT is a common, permissive choice for hardware/firmware portfolio projects.
Note: if you reuse or adapt files from libhmk / HE60 / marbastlib, check and respect their licenses. -->
