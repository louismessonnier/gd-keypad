# libhmk Notes — GD Keypad

## What you actually have to write

**One file:** `keyboards/gd-keypad/keyboard.json`

That's it. The `hardware/` and `src/hardware/` files are **not** per-board — they're
driver code shared by every board using that chip. Your `"driver": "at32f405xx"`
points at code that already exists in the repo, so you inherit it for free.

Confirmed by: `keyboards/he60-v2/` contains only `keyboard.json` and a README.

---

## How the schema was figured out

Four files, in the order they were useful:

### 1. `scripts/schema/keyboard.py` — authoritative field list

Pydantic schema. Every allowed field, which are optional, types and constraints.
This is the real documentation. If unsure whether a field exists or what it accepts,
this file answers it.

Key discovery — `KeyboardAnalog` has **both**:

```python
raw: KeyboardAnalogRaw | None = None
mux: KeyboardAnalogMux | None = None
```

So direct-connected sensors (no multiplexer) are a first-class option, even though
**all four example boards use `mux`** and there is no `raw` example anywhere in the repo.

`KeyboardAnalogRaw` is just:
- `input` — list of ADC pins (as strings like `"A0"`, or raw channel numbers)
- `vector` — key index for each input

### 2. `scripts/make.py` — JSON → C macros

Runs pre-build. Turns `keyboard.json` into compiler `-D` defines.
Does **not** transform index values:

```python
build_flags.define("ADC_RAW_INPUT_VECTOR", utils.to_c_array(raw.vector))
```

So the index convention lives in the C, not the Python.

Also: `driver.metadata.adc.to_adc_inputs(raw.input)` converts pin-name strings to
channel numbers, which means **pin names get validated at build time**. A bad pin
name fails the build rather than producing a broken binary.

### 3. `src/hardware/at32f405xx/analog.c` — settles the index convention

```c
const uint16_t key = raw_input_vector[i];
if (key)
  adc_values[key - 1] = adc_buffer[ADC_NUM_MUX_INPUTS + i];
```

- `key - 1` → **`vector` is 1-based**
- `if (key)` → **0 means "not connected"**

Note: the code comment above that array says values ≥ `NUM_KEYS` mean unconnected.
That's stale — the code guards on zero. Trust the code.

Also contains the ADC channel table, which confirms valid pin names:

```
ch:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
pin: A0 A1 A2 A3 A4 A5 A6 A7 B0 B1 C0 C1 C2 C3 C4 C5
```

So `A0`/`A1`/`A2` = channels 0/1/2.

### 4. `scripts/drivers.py` — chip-level metadata

(A file, not a folder.) Defines flash layout, bootloader address, ADC pin list, etc.
per driver. Rarely need to touch it.

---

## Gotcha: two different index conventions in the same file

| Field | Convention |
|---|---|
| `analog.raw.vector` | **1-based**, 0 = unconnected |
| `analog.mux.matrix` | **1-based**, 0 = unconnected |
| `layout.keymap[].key` | **0-based** |

This is real, not a typo. HE16 maps 16 keys as `1..16` in `mux.matrix` but as
`0..15` in `layout.keymap`.

---

## Field-by-field, for the GD Keypad

| Field | Value | Why |
|---|---|---|
| `hardware.driver` | `at32f405xx` | Your chip; driver code already in repo |
| `hardware.hse_value` | `12000000` | Your 12 MHz crystal |
| `usb.port` | `hs` | High speed — this is what gives 8 kHz polling |
| `usb.vid` | `0xAB50` | Left as peppapighs'; USB VIDs are org-allocated, nobody hobby-scale has one |
| `usb.pid` | `0x6D01` | **Changed** so your board doesn't collide with an HE60 on the same PC |
| `keyboard.num_keys` | `3` | Analog keys only — mechanical keys + encoder are outside libhmk's model |
| `keyboard.num_advanced_keys` | `8` | Storage for DKS/tap-hold configs. Schema allows 1–64; HE16 uses 32 |
| `analog.invert_adc` | `false` | Correct for MT9102ET per the he60-v2 commit. **Verify against your actual sensor** |
| `analog.raw.input` | `["A0","A1","A2"]` | Your three sensors, direct to ADC |
| `analog.raw.vector` | `[1,2,3]` | 1-based → keys 0,1,2 |
| `calibration.*` | `2400` / `700` | ADC counts. Tuned for *his* sensor and board — **starting point only** |
| `layout.keymap` | one row of 3 | 0-based here |
| `keymap` | `KC_SPC` ×3 | GD jump is space; three keys lets you alternate fingers |

### Still to verify
- `invert_adc` against the sensor you actually ordered (SS39ET should also be non-inverted)
- Calibration values — tune in hmkconf once you can read live values

---

## Side benefit of having no multiplexer

With a mux, the DMA handler has to set select pins and run TMR6 to let the mux
outputs settle before each conversion. Your raw-only path skips all of it:

```c
#else
    adc_initialized = true;
    // Immediately start the next conversion
    adc_ordinary_software_trigger_enable(ADC1, TRUE);
#endif
```

Back-to-back conversions, no settling delay. Your 3 keys scan faster than a muxed
board's would — good property for a rhythm game.

Consequence: the `analog.delay` field is irrelevant to you. It only feeds the mux timer.

---

## License

libhmk is **GPL-3.0-or-later** (see the header on every source file, and `LICENSE`).

Relevant to your README: firmware source you write as part of libhmk and then
distribute is subject to it. Your KiCad and CAD files are **not** — hardware designs
aren't derivative works of the firmware. So you'll likely end up with GPL-3.0 for
`firmware/` and a license of your choosing for the hardware. State both explicitly.

---

# Build Guide

## Where the file goes

**For building:** `libhmk/keyboards/gd-keypad/keyboard.json`

The libhmk clone is a **build workspace**, not where your work lives. Keep the
source of truth in your own repo:

```
gd-keypad/                 ← your repo
  firmware/
    keyboard.json          ← source of truth
    src/                   ← matrix + encoder code, later
  kicad/
  cad/
```

Then copy `firmware/keyboard.json` into the libhmk clone to build. Keep the libhmk
clone *outside* your repo folder so it never gets committed.

Alternative: fork libhmk and add your board there. Legitimate (it's how upstream
expects boards to be contributed, and you could PR it), but splits your firmware
from your hardware work, which is worse for a portfolio.

## Steps

**One-time setup** (already done):
- Python installed, `python -m pip install -r requirements.txt`
- VS Code + PlatformIO IDE extension
- libhmk cloned, opened in VS Code as the **workspace root** (so `platformio.ini` is
  at the top of the sidebar, not nested)

**Each build:**

1. Create the folder `libhmk/keyboards/gd-keypad/`

2. Copy your `keyboard.json` into it

3. In the VS Code terminal (at the libhmk root):
   ```
   python setup.py -k gd-keypad
   ```
   This **overwrites** `platformio.ini` for your board. (Switching between boards is
   just re-running this with a different `-k`.)

4. PlatformIO reconfigures the project. Watch the status bar. If it doesn't notice:
   Ctrl+Shift+P → "Reload Window".

5. Build — click the checkmark (✓) in the bottom status bar, or PlatformIO sidebar →
   Project Tasks → gd-keypad → Build. (`pio run` in a plain terminal won't work
   unless PlatformIO is on your PATH; use PlatformIO's own terminal or the buttons.)

6. Output: `.pio/build/gd-keypad/firmware.bin`

## What errors to expect

- **Bad pin name** → build fails at `to_adc_inputs()`. Check against the channel table above.
- **Schema violation** → `scripts/validate.py` runs pre-build and reports it.
- **Wrong array length** → `_Static_assert` in `analog.c` fails with a clear message.

All three fail loudly rather than silently producing a broken binary.

## Flashing (once boards arrive)

`setup.py` already sets `upload_protocol = dfu`, so no extra config needed.

1. Hold the BOOT0 button
2. Plug in USB
3. Release
4. Either: PlatformIO Upload (→ arrow in status bar), or `pio run --target upload`,
   or the WebUSB DFU tool in a Chromium browser

First time on Windows you may need Zadig to install a driver so the DFU device
is recognized.

## Then

- Connect at [hmkconf.com](https://hmkconf.com/) to configure actuation, rapid trigger, keymap
- Tune the calibration values against real ADC readings
- Add the mechanical matrix + encoder code
