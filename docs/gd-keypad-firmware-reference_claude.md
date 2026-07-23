# GD Keypad — Firmware Reference

Everything about the libhmk firmware for this board: how it works, what was
written, why, how to build it, and what still needs testing.

Supersedes the earlier `libhmk-notes.md` and `INTEGRATION.md`.

---

# Part 1 — How libhmk works

## The pipeline

The firmware is a chain of stages, each with one job:

```
ADC hardware
  → DMA writes adc_values[]                    analog.c
  → analog_read(key)                           analog.c
  → EMA filter, travel distance,
    rapid trigger state machine                matrix.c
  → key_matrix[key].is_pressed                 matrix.c
  ─────────────── the seam ───────────────
  → edge detection, layers, keycode lookup,
    advanced keys, XInput                      layout.c
  → HID report                                 hid.c
  → USB                                        TinyUSB
```

**`key_matrix[].is_pressed` is the seam.** Everything above it answers "is this
key down?" Everything below it answers "what do I send to the PC?" The halves
communicate through one boolean per key and nothing else.

That is what made this project tractable. Your mechanical keys and encoder don't
need the analog half at all — they only need to reach that boolean.

## What you actually have to write

libhmk is structured so a new board is **one JSON file**. The `hardware/` and
`src/hardware/` directories are *not* per-board — they're driver code shared by
every board using that chip. Setting `"driver": "at32f405xx"` inherits all of it.

Confirmed: `keyboards/he60-v2/` contains only `keyboard.json` and a README.

The C module in Part 3 is extra, and only because this board has inputs that
libhmk doesn't model.

## Key indices

libhmk numbers keys `0` through `NUM_KEYS - 1`. For this board:

| Index | What | Driven by |
|---|---|---|
| 0, 1, 2 | Hall-effect jump keys | `matrix.c`, from the ADC |
| 3 – 8 | Mechanical matrix | `gd_keypad.c` |
| 9 | Encoder push switch | `gd_keypad.c` |
| 10 | Encoder rotate CW (virtual) | `gd_keypad.c` |
| 11 | Encoder rotate CCW (virtual) | `gd_keypad.c` |

"Keys 3–11" throughout this document means those nine indices.

## Why we expanded num_keys instead of calling layout_register()

The obvious approach was to call `layout_register()` directly from the scan code.
Reading `layout.c` revealed something better.

`layout_task()` is **edge-triggered**. It keeps a bitmap of what was pressed last
iteration and acts only on changes:

```c
if (k->is_pressed & !last_key_press) {        // press event
} else if (!k->is_pressed & last_key_press) { // release event
} else if (k->is_pressed) {                   // hold event
}
```

Two consequences:

1. It won't fight manual calls — it doesn't blindly overwrite HID state each loop.
2. More usefully: **if you can get a boolean into `key_matrix[i].is_pressed`,
   `layout_task()` does everything else.** Keycode lookup, layer resolution,
   advanced keys, XInput, HID reports.

So instead of hand-rolling keypresses, `num_keys` went from 3 to 12 and the extra
indices get written directly. The mechanical keys and encoder become first-class
keys — same code path as the analog ones, and **remappable in hmkconf** rather
than hardcoded.

`key_matrix` is declared `extern` in `matrix.h`. This is a supported entry point,
not a hack.

---

# Part 2 — keyboard.json

This file has two consumers. `scripts/make.py` turns it into compiler `-D`
defines at build time. hmkconf reads it to know what the board looks like.

## Field by field

### `hardware`

```json
"hardware": { "hse_value": 12000000, "driver": "at32f405xx" }
```

`driver` selects existing driver code under `src/hardware/at32f405xx/`.
`hse_value` becomes `HSE_VALUE`, telling the clock setup the crystal is 12 MHz.

### `usb`

```json
"usb": { "vid": "0xAB50", "pid": "0x6D01", "port": "hs" }
```

`port: "hs"` becomes `BOARD_USB_HS` — **this is the 8 kHz polling.** `"fs"` would
cap at 1 kHz.

`vid` is left as peppapighs'. USB vendor IDs are allocated to organizations and
cost money; nobody hobby-scale has their own. Changing only the `pid` is the
normal convention and is enough to stop Windows confusing this board with an HE60.

`name` and `manufacturer` become the USB descriptor strings — that's how you can
verify a built binary is really yours.

### `keyboard`

```json
"keyboard": {
  "num_profiles": 4, "num_layers": 4,
  "num_keys": 12, "num_advanced_keys": 8
}
```

**`num_keys` is not a variable.** `make.py` emits `-DNUM_KEYS=12`, a compile-time
constant that statically sizes:

- `key_state_t key_matrix[NUM_KEYS]` — matrix.c
- `adc_values[NUM_KEYS]` — analog.c
- `active_keycodes[NUM_KEYS]`, `active_advanced_keys[NUM_KEYS]` — layout.c
- several bitmaps (`key_disabled`, `key_press_states`)
- the keymap arrays in eeconfig

This is why it had to change from 3. Writing `key_matrix[11]` in a 3-element
array is memory corruption, not an error message.

`num_advanced_keys` is storage for DKS / tap-hold bindings. Schema allows 1–64;
8 is generous for 12 keys.

### `analog`

```json
"analog": {
  "invert_adc": false,
  "raw": { "input": ["A0", "A1", "A2"], "vector": [1, 2, 3] }
}
```

The `raw` block is the one with **no example anywhere in libhmk** — all four
reference boards use `mux`. It was found in `scripts/schema/keyboard.py`, where
`KeyboardAnalog` declares both `raw` and `mux` as optional.

`input` pin names are converted to ADC channel numbers at build time, so a bad
name fails the build rather than producing a broken binary.

`vector` is **1-based**, with 0 meaning "not connected". From `analog.c`:

```c
const uint16_t key = raw_input_vector[i];
if (key)
  adc_values[key - 1] = adc_buffer[ADC_NUM_MUX_INPUTS + i];
```

So input 0 → `adc_values[0]` → key 0. Keys 3–11 aren't listed, so nothing ever
writes their ADC values.

`invert_adc` handles sensors whose output falls as the key travels down. `false`
is correct for MT9102ET per the he60-v2 commit; **verify against the sensor you
actually ordered.**

### `calibration`

```json
"calibration": { "initial_rest_value": 2400, "initial_bottom_out_threshold": 700 }
```

Starting ADC counts for a released key and the press threshold. These are tuned
for peppapighs' sensor on a 1.2 mm board. **Starting point only** — tune in
hmkconf against live readings.

### `layout`

Controls **only** how hmkconf draws the board. Zero functional effect. The `x`
offsets are a rough guess at the physical arrangement.

Note this block is **0-based** (`{"key": 0}`) while `analog.raw.vector` is
1-based. That inconsistency is real in libhmk's own files, not a mistake here.

### `keymap`

One row per layer (four rows because `num_layers: 4`), each row with `num_keys`
entries in index order.

```json
"keymap": [
  ["KC_1","KC_2","KC_3", "KC_ESC","KC_TAB","KC_Q","KC_E","KC_Z","KC_X",
   "KC_MUTE", "KC_F14","KC_F13"],
  ...
]
```

Reading positionally: keys 0–2 are the jump keys (`1`, `2`, `3` — distinct so GD
can bind them separately, e.g. one as P2 jump for duals), 3–8 the mechanical
matrix, 9 the encoder press, 10/11 the rotation.

`_______` is `KC_TRANSPARENT` — fall through to the layer below.

**This is only the default.** Once you remap in hmkconf, that lives in flash and
survives reflashing. Don't agonize over it: flash, press keys, fix what's wrong
in the browser.

## Known issue: layer 1 is unreachable

Layer 1 has `SP_BOOT` on key 8, but **nothing on layer 0 activates layer 1** —
there's no `MO(1)` in the keymap. As written, `SP_BOOT` can never fire.

This matters because the case is an open sandwich: the BOOT0 button sits under
the top plate and may be awkward to reach once assembled. `SP_BOOT` is the
software route into DFU.

Fixes, pick one:

- **Encoder press as the layer key** — set key 9 to `MO(1)` on layer 0. Hold the
  knob to reach layer 1. Costs you `KC_MUTE` on the knob press.
- **A mechanical key as the layer key** — same idea, on one of keys 3–8.
- **Drop it** — delete `SP_BOOT`, set `num_layers: 1`, rely on the physical
  BOOT0 button. Fine if it stays reachable.

Also worth knowing: `SP_BOOT` calls `board_enter_bootloader()`, and every layer
mapping is remappable in hmkconf later, so this isn't a permanent decision.

---

# Part 3 — The gd_keypad module

## gd_keypad.h

Constants only. The key-index map, and three tunables wrapped in
`#if !defined(...)` so they can be overridden without editing the header:

| Macro | Default | Meaning |
|---|---|---|
| `GD_DEBOUNCE_MS` | 5 | Debounce window for mechanical switches |
| `GD_ENC_STEPS_PER_DETENT` | 2 | Quadrature transitions per physical click |
| `GD_ENC_PULSE_MS` | 12 | How long a virtual rotate key is held |

## The matrix scan

The diodes point toward the rows, so current only flows column → row. That
dictates the direction: **drive a row low, read the columns.**

Rows are push-pull outputs idling **high**. Columns are inputs with internal
pull-ups, so with nothing pressed every column floats high.

Per row:

```c
gpio_bits_write(row_ports[r], row_pins[r], FALSE);  // drive low
gd_settle();
// read both columns — pressed reads RESET (low)
gpio_bits_write(row_ports[r], row_pins[r], TRUE);   // release
```

A pressed key lets its column's pull-up current flow through the switch, forward
through the diode, into the grounded row — pulling that column low.

Only one row is low at a time; that's what makes the matrix unambiguous. The
diodes stop current sneaking backwards through other keys — the ghosting fix,
in hardware.

`gd_settle()` is a short busy-wait. The pull-ups are weak (tens of kΩ) against
the line's parasitic capacitance, so the column needs a moment to actually
discharge before sampling. Read too early and you get the previous value.

## The debounce

Metal contacts bounce for a few milliseconds. Unfiltered, one keystroke becomes
a burst of presses.

Three arrays per switch — last raw reading, accepted state, timestamp:

```c
if (raw != sw_last_raw[idx]) {
    sw_last_raw[idx] = raw;
    sw_last_change[idx] = timer_read();      // moved — restart the clock
} else if ((raw != sw_stable[idx]) &&
           (timer_elapsed(sw_last_change[idx]) >= GD_DEBOUNCE_MS)) {
    sw_stable[idx] = raw;                    // steady long enough — accept
}
key_matrix[key].is_pressed = sw_stable[idx];
```

Any change restarts the timer. Only a reading that disagrees with the accepted
state *and* has held steady for 5 ms is promoted. During bounce the timer keeps
resetting and nothing is accepted.

This is "wait for quiet" — costs 5 ms of latency, completely immune to bounce.
Right tradeoff for ESC and TAB; wrong for a jump key, which is exactly why the
analog keys use a different path.

The encoder's push switch is a seventh entry in the same arrays.

## The encoder

A quadrature encoder has two contacts offset by a quarter cycle. Read as a 2-bit
value, turning produces `00 → 01 → 11 → 10` one way and the reverse the other.
**Direction comes from which transition occurred**, not from either pin alone.

Combine previous and current state into a 4-bit index, look up the answer:

```c
static const int8_t enc_lut[16] = {
    0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0,
};
```

Sixteen entries: four are no-change (prev == current), eight are valid single
steps giving ±1, four are illegal double transitions — both pins flipping at
once, physically impossible, meaning a reading was missed. Those return 0, so
noise is discarded rather than counted as motion.

Steps accumulate. Reaching `GD_ENC_STEPS_PER_DETENT` is one physical click.

### The pulse

A rotation is an **event**, but `layout_task()` only speaks press and release
edges. So the virtual key must go down, stay down long enough to be observed,
then come up:

```c
if (enc_pulse_until != 0) {
    // pulse in flight — hold it, or end it
} else if (enc_accum >= GD_ENC_STEPS_PER_DETENT) {
    // start a CW pulse
}
```

12 ms held, then released, and only *then* look for the next detent. That
serialization is deliberate: spin fast and you get a clean sequence of discrete
keypresses instead of one smeared hold.

**Flagged for review:** the deadline check uses
`timer_elapsed(enc_pulse_until) < UINT32_MAX / 2` to mean "deadline passed."
That's wraparound-safe unsigned arithmetic, but it's obscure and it assumes
`timer_elapsed()` is plain `timer_read() - x`. Reasonable, unverified. If it
misbehaves, store the start time and compare `timer_elapsed(start) >=
GD_ENC_PULSE_MS`.

## Why keys 3–11 don't self-actuate

They have no ADC input, so `adc_values[]` stays 0. During `matrix_recalibrate()`
their `adc_rest_value` is driven to 0, distance computes to 0, and `matrix_scan()`
leaves `is_pressed` false.

Even if that reasoning were wrong, `gd_keypad_task()` runs *after* `matrix_scan()`
and overwrites `is_pressed` for all nine before `layout_task()` reads them. The
ordering makes it safe either way — which is why the ordering isn't optional.

---

# Part 4 — Integration

## File placement

| File | Destination in the libhmk clone |
|---|---|
| `keyboard.json` | `keyboards/gd-keypad/keyboard.json` |
| `gd_keypad.h` | `include/gd_keypad.h` |
| `gd_keypad.c` | `src/gd_keypad.c` |

`build_src_filter` in `platformio.ini` only covers `src/`, so the `.c` **must**
go there. `make.py` puts `keyboards/gd-keypad/` on the *include* path only — a
`.c` placed next to `keyboard.json` would silently never be compiled.

## The main.c patch

Three edits. Without them the module is dead code.

```diff
 #include "wear_leveling.h"
 #include "xinput.h"
+#include "gd_keypad.h"
```

```diff
   analog_init();
   matrix_init();
+  gd_keypad_init();
   hid_init();
```

```diff
     analog_task();
     matrix_scan();
+    gd_keypad_task();
     layout_task();
```

**Why exactly there.** `matrix_scan()` loops over *all* `NUM_KEYS` — including
3–11, using their meaningless zero ADC values — so it writes `is_pressed` for
your keys before you do. You must run after it. `layout_task()` consumes
`is_pressed`, so you must run before it. There is exactly one correct slot.

Keep this diff. It's the one thing to re-apply after pulling libhmk updates.

## Build

```
python setup.py -k gd-keypad
```

then Build (checkmark in the VS Code status bar, or PlatformIO sidebar →
Project Tasks → gd-keypad → Build).

`make.py` re-reads `keyboard.json` on every build, so re-running `setup.py` after
a JSON-only change is belt-and-braces rather than strictly required. It's cheap;
do it anyway when `num_keys` changes.

Output: `.pio/build/gd-keypad/firmware.bin`

## Repo layout

The libhmk clone is a **build workspace**, not where your work lives. Keep the
source of truth in your own repo:

```
gd-keypad/
  firmware/
    keyboard.json
    gd_keypad.c
    gd_keypad.h
    main.c.patch      ← the diff above
  kicad/
  cad/
  docs/
```

Keep the libhmk clone *outside* the repo folder so it never gets committed.
Don't commit `firmware.bin` — it's a build artifact. If you want to distribute a
binary, attach it to a GitHub **Release**.

## Flashing

`setup.py` already sets `upload_protocol = dfu`.

1. Hold BOOT0
2. Plug in USB
3. Release
4. PlatformIO Upload, `pio run --target upload`, or the WebUSB DFU tool in Chrome

First time on Windows may need Zadig to install a driver so the DFU device is
recognized.

## hmkconf

hmkconf talks to the flashed board over WebUSB and edits the `eeconfig` structure
in flash. It does not recompile anything.

Because keys 3–11 are real entries in `key_matrix`, hmkconf sees all twelve and
lets you remap any of them — including the encoder rotation. It renders the board
from `layout.keymap`, which is why keys 10 and 11 are included there.

Settings made in hmkconf persist in flash through wear leveling, so they survive
reflashing.

---

# Part 5 — Optional: unused GPIO

The Artery hardware guide recommends configuring unused pins as output-low
rather than leaving them floating.

**Recommendation: skip it.** That guidance targets battery-powered designs (a
floating input can oscillate near its threshold and waste current) and
EMC-critical boards. A USB-powered macropad is neither. It also isn't known
whether libhmk's `board_init()` already handles this.

If you want it anyway, add to `gd_keypad_init()`:

```c
static void gd_init_unused_pins(void) {
  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;

  // Example — PA4, PA7. Build this list from your own schematic.
  gpio_init_struct.gpio_pins = GPIO_PINS_4 | GPIO_PINS_7;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_write(GPIOA, GPIO_PINS_4 | GPIO_PINS_7, FALSE);
}
```

**Do not include** pins that are in use. On this board that means: PA0–PA2
(sensors), PA3 (encoder SW), PA5/PA6 (columns), PA13/PA14 (SWD), PB0 (row 0),
PB3 (SWO), PC4/PC5 (rows 1–2), PF0/PF1 (crystal), PF4/PF5 (encoder A/B), plus
NRST and BOOT0. Driving a pin that's wired to something else can short an output
against an external driver.

---

# Part 6 — Action items

1. **Fix the typo in `main.c`** — `gd_keypad_init();m` has a stray `m`.
2. **Decide the layer-1 question** — add an `MO(1)` binding, or drop `SP_BOOT`
   and set `num_layers: 1`.
3. **Re-run `python setup.py -k gd-keypad`** and build.
4. **Fix HAL naming errors** as they appear (see Part 7).
5. **Commit to your repo** under `firmware/` — JSON, `.c`, `.h`, and the main.c
   diff. Not the libhmk clone, not the binary.
6. **With hardware:** flash via DFU, work the test list, tune calibration in
   hmkconf.

---

# Part 7 — What still needs verifying

## Build-time — surfaces immediately

**Four Artery HAL identifiers were inferred, not confirmed.** `analog.c` gave us
`crm_periph_clock_enable`, `gpio_default_para_init`, `gpio_init`,
`gpio_bits_write`, `GPIO_MODE_OUTPUT`, `GPIO_MODE_INPUT`, `GPIO_PULL_NONE`,
`GPIO_DRIVE_STRENGTH_STRONGER`. But it never reads an input pin or uses a
pull-up, so these had nothing to copy from:

- `gpio_input_data_bit_read(gpio_type *, uint16_t)` returning `SET` / `RESET`
- `GPIO_PULL_UP`
- `GPIO_OUTPUT_PUSH_PULL`
- `CRM_GPIOF_PERIPH_CLOCK`

Real names live in the framework headers:

```
C:\Users\<you>\.platformio\packages\framework-at32firmlib\**\at32f402_405_gpio.h
C:\Users\<you>\.platformio\packages\framework-at32firmlib\**\at32f402_405_crm.h
```

Search for `gpio_input` and `gpio_pull_type`. This is a find-and-replace, not a
logic problem.

**`-Werror` with `-Wsign-conversion`, `-Wswitch-default`, `-Wstrict-prototypes`**
means the build is strict — a missing cast is a hard failure, not a warning.

**`KC_F13` / `KC_F14`** — confirmed present in `keycodes.h`. No action.

## Runtime — needs hardware

| What | Symptom if wrong | Fix |
|---|---|---|
| **Diode direction** | No mechanical key ever registers | Swap row/column roles in `gd_keypad.c` |
| **Physical key order** | Wrong letters from wrong keys | Remap in hmkconf |
| **Encoder direction** | Knob turns volume the wrong way | Swap the keycodes for keys 10 and 11 |
| **Steps per detent** | Two presses per click, or two clicks per press | Raise / lower `GD_ENC_STEPS_PER_DETENT` |
| **`gd_settle()` duration** | Phantom presses, or presses on the wrong row | Increase the loop count |
| **`timer_elapsed()` semantics** | Encoder pulses stick or never fire | Rework the deadline check (Part 3) |
| **Boot-time spurious press** | A key fires once at plug-in | `matrix_recalibrate()` runs before the first `gd_keypad_task()` |
| **`invert_adc`** | Jump keys inverted — pressed at rest | Flip to `true` |
| **Calibration values** | Poor travel range, bad rapid trigger | Tune in hmkconf against live readings |

## Worth just looking at

**How hmkconf presents keys 3–11.** It will offer actuation points and rapid
trigger settings for nine keys that have no analog input. Harmless in principle —
those settings only feed `matrix_scan()`, whose output you overwrite — but it may
look confusing, and it's untested.

---

# Appendix — How the schema was reverse-engineered

Four files, in the order they proved useful:

**`scripts/schema/keyboard.py`** — the pydantic schema. Every allowed field,
which are optional, types and constraints. This is the real documentation.
Where `raw` was found.

**`scripts/make.py`** — turns JSON into `-D` defines. Does not transform index
values, so the 1-based convention had to be found elsewhere. Also revealed that
pin names are validated at build time.

**`src/hardware/at32f405xx/analog.c`** — settled the index convention
(`key - 1`, `if (key)`), and provided the ADC channel table:

```
ch:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
pin: A0 A1 A2 A3 A4 A5 A6 A7 B0 B1 C0 C1 C2 C3 C4 C5
```

**`src/layout.c`** — showed `layout_task()` is edge-triggered, which is what made
the `num_keys` expansion viable instead of manual `layout_register()` calls.

## Side benefit of having no multiplexer

With a mux, the DMA handler must set select pins and run TMR6 to let outputs
settle between conversions. The raw path skips all of it:

```c
#else
    adc_initialized = true;
    adc_ordinary_software_trigger_enable(ADC1, TRUE);  // immediately
#endif
```

Back-to-back conversions, no settling delay — three keys scan faster than a
muxed board's would. Good property for a rhythm game.

Consequence: the `analog.delay` field is irrelevant here; it only feeds the mux
timer.

## License

libhmk is **GPL-3.0-or-later** (header on every source file, plus `LICENSE`).

`gd_keypad.c` / `gd_keypad.h` are written against libhmk's internals and
distributed as part of it, so they carry GPL-3.0 — the headers are already in
place.

The KiCad and CAD files are **not** affected. Hardware designs aren't derivative
works of the firmware. Expect to license `firmware/` as GPL-3.0 and the hardware
under something of your choosing. State both explicitly in the README rather
than leaving it ambiguous.
