/*
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "gd_keypad.h"

#include "hardware/hardware.h"
#include "matrix.h"

#include "at32f402_405.h"

_Static_assert(NUM_KEYS == 12, "GD Keypad expects num_keys = 12");

//--------------------------------------------------------------------+
// Pin map
//--------------------------------------------------------------------+
//
// Diodes point toward the rows (current flows column -> row), so we drive a
// row LOW and read the columns with pull-ups. A pressed key pulls its column
// low through the forward-biased diode.

#define GD_NUM_ROWS 3
#define GD_NUM_COLS 2
#define GD_NUM_MECH (GD_NUM_ROWS * GD_NUM_COLS)

// ROW 0 = PB0, ROW 1 = PC5, ROW 2 = PC4
static gpio_type *const row_ports[GD_NUM_ROWS] = {GPIOB, GPIOC, GPIOC};
static const uint16_t row_pins[GD_NUM_ROWS] = {
    GPIO_PINS_0,
    GPIO_PINS_5,
    GPIO_PINS_4,
};

// COLUMN 0 = PA6, COLUMN 1 = PA5
static gpio_type *const col_ports[GD_NUM_COLS] = {GPIOA, GPIOA};
static const uint16_t col_pins[GD_NUM_COLS] = {
    GPIO_PINS_6,
    GPIO_PINS_5,
};

// ENCODER A = PF4, ENCODER B = PF5, ENCODER S1 = PA3
#define GD_ENC_A_PORT GPIOF
#define GD_ENC_A_PIN GPIO_PINS_4
#define GD_ENC_B_PORT GPIOF
#define GD_ENC_B_PIN GPIO_PINS_5
#define GD_ENC_SW_PORT GPIOA
#define GD_ENC_SW_PIN GPIO_PINS_3

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

// Debounced state of the six mechanical keys plus the encoder switch
#define GD_NUM_DEBOUNCED (GD_NUM_MECH + 1)
#define GD_IDX_ENC_SW GD_NUM_MECH

static bool sw_stable[GD_NUM_DEBOUNCED];
static bool sw_last_raw[GD_NUM_DEBOUNCED];
static uint32_t sw_last_change[GD_NUM_DEBOUNCED];

// Quadrature decoder. Index is (previous_state << 2) | current_state, where
// state is (A << 1) | B. Zero entries are no-ops or invalid double
// transitions, which we ignore rather than guess at.
static const int8_t enc_lut[16] = {
    0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0,
};

static uint8_t enc_prev_state;
static int32_t enc_accum;

// Deadline for the currently held virtual rotate key, 0 when idle
static uint32_t enc_pulse_until;
static uint8_t enc_pulse_key;

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+

/**
 * @brief Short busy-wait to let a row line settle after being driven
 *
 * The column pull-ups are weak (tens of kohm), so the line needs a moment to
 * discharge through a pressed key before we sample it.
 */
static void gd_settle(void) {
  for (volatile uint32_t i = 0; i < 32; i++) {
  }
}

/**
 * @brief Debounce one switch and publish it to the key matrix
 *
 * @param idx Index into the debounce arrays
 * @param raw Raw reading, true when pressed
 * @param key Key index to write in key_matrix
 */
static void gd_debounce(uint32_t idx, bool raw, uint8_t key) {
  if (raw != sw_last_raw[idx]) {
    // Reading changed, restart the debounce window
    sw_last_raw[idx] = raw;
    sw_last_change[idx] = timer_read();
  } else if ((raw != sw_stable[idx]) &&
             (timer_elapsed(sw_last_change[idx]) >= GD_DEBOUNCE_MS)) {
    // Reading has been consistent for long enough, accept it
    sw_stable[idx] = raw;
  }

  key_matrix[key].is_pressed = sw_stable[idx];
}

//--------------------------------------------------------------------+
// Init
//--------------------------------------------------------------------+

void gd_keypad_init(void) {
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);

  // Rows: push-pull outputs, idle high
  for (uint32_t i = 0; i < GD_NUM_ROWS; i++) {
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = row_pins[i];
    gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(row_ports[i], &gpio_init_struct);

    gpio_bits_write(row_ports[i], row_pins[i], TRUE);
  }

  // Columns: inputs with pull-ups
  for (uint32_t i = 0; i < GD_NUM_COLS; i++) {
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = col_pins[i];
    gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_pull = GPIO_PULL_UP;
    gpio_init(col_ports[i], &gpio_init_struct);
  }

  // Encoder A/B and the push switch: inputs with pull-ups. The encoder's
  // common pin and the switch's second pin are tied to ground on the PCB.
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GD_ENC_A_PIN | GD_ENC_B_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(GPIOF, &gpio_init_struct);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GD_ENC_SW_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(GD_ENC_SW_PORT, &gpio_init_struct);

  // Seed the debounce state from the current readings so we do not report a
  // spurious press on the first scan.
  const uint32_t now = timer_read();
  for (uint32_t i = 0; i < GD_NUM_DEBOUNCED; i++) {
    sw_stable[i] = false;
    sw_last_raw[i] = false;
    sw_last_change[i] = now;
  }

  // Seed the quadrature state so the first read is not treated as a step
  const uint8_t a =
      (gpio_input_data_bit_read(GD_ENC_A_PORT, GD_ENC_A_PIN) == SET) ? 1u : 0u;
  const uint8_t b =
      (gpio_input_data_bit_read(GD_ENC_B_PORT, GD_ENC_B_PIN) == SET) ? 1u : 0u;
  enc_prev_state = (uint8_t)((a << 1) | b);
  enc_accum = 0;

  enc_pulse_until = 0;
  enc_pulse_key = 0;

  for (uint32_t i = GD_KEY_MECH_FIRST; i < NUM_KEYS; i++)
    key_matrix[i].is_pressed = false;
}

//--------------------------------------------------------------------+
// Task
//--------------------------------------------------------------------+

void gd_keypad_task(void) {
  //---------------- Mechanical matrix ----------------

  for (uint32_t r = 0; r < GD_NUM_ROWS; r++) {
    gpio_bits_write(row_ports[r], row_pins[r], FALSE);
    gd_settle();

    for (uint32_t c = 0; c < GD_NUM_COLS; c++) {
      // Pressed pulls the column low
      const bool raw =
          (gpio_input_data_bit_read(col_ports[c], col_pins[c]) == RESET);
      const uint32_t idx = r * GD_NUM_COLS + c;

      gd_debounce(idx, raw, (uint8_t)(GD_KEY_MECH_FIRST + idx));
    }

    gpio_bits_write(row_ports[r], row_pins[r], TRUE);
  }

  //---------------- Encoder push switch ----------------

  const bool sw_raw =
      (gpio_input_data_bit_read(GD_ENC_SW_PORT, GD_ENC_SW_PIN) == RESET);
  gd_debounce(GD_IDX_ENC_SW, sw_raw, GD_KEY_ENC_SW);

  //---------------- Encoder rotation ----------------

  const uint8_t a =
      (gpio_input_data_bit_read(GD_ENC_A_PORT, GD_ENC_A_PIN) == SET) ? 1u : 0u;
  const uint8_t b =
      (gpio_input_data_bit_read(GD_ENC_B_PORT, GD_ENC_B_PIN) == SET) ? 1u : 0u;
  const uint8_t state = (uint8_t)((a << 1) | b);

  if (state != enc_prev_state) {
    const uint8_t lut_idx = (uint8_t)((enc_prev_state << 2) | state);
    enc_accum += enc_lut[lut_idx];
    enc_prev_state = state;
  }

  // Release a finished pulse before starting a new one, so consecutive steps
  // always produce a distinct release edge for layout_task() to see.
  if (enc_pulse_until != 0) {
    if (timer_elapsed(enc_pulse_until) < UINT32_MAX / 2) {
      key_matrix[enc_pulse_key].is_pressed = false;
      enc_pulse_until = 0;
    } else {
      key_matrix[enc_pulse_key].is_pressed = true;
    }
  } else if (enc_accum >= GD_ENC_STEPS_PER_DETENT) {
    enc_accum -= GD_ENC_STEPS_PER_DETENT;
    enc_pulse_key = GD_KEY_ENC_CW;
    enc_pulse_until = timer_read() + GD_ENC_PULSE_MS;
    key_matrix[enc_pulse_key].is_pressed = true;
  } else if (enc_accum <= -GD_ENC_STEPS_PER_DETENT) {
    enc_accum += GD_ENC_STEPS_PER_DETENT;
    enc_pulse_key = GD_KEY_ENC_CCW;
    enc_pulse_until = timer_read() + GD_ENC_PULSE_MS;
    key_matrix[enc_pulse_key].is_pressed = true;
  } else {
    key_matrix[GD_KEY_ENC_CW].is_pressed = false;
    key_matrix[GD_KEY_ENC_CCW].is_pressed = false;
  }
}
