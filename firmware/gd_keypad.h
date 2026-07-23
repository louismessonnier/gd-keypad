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

#pragma once

#include "common.h"

//--------------------------------------------------------------------+
// Key index map
//--------------------------------------------------------------------+
//
//   0 .. 2   Hall-effect jump keys  (driven by matrix.c from the ADC)
//   3 .. 8   Mechanical matrix      (this module)
//   9        Encoder push switch    (this module)
//  10        Encoder rotate CW      (this module, pulsed)
//  11        Encoder rotate CCW     (this module, pulsed)

#define GD_KEY_MECH_FIRST 3
#define GD_KEY_ENC_SW 9
#define GD_KEY_ENC_CW 10
#define GD_KEY_ENC_CCW 11

//--------------------------------------------------------------------+
// Tuning
//--------------------------------------------------------------------+

#if !defined(GD_DEBOUNCE_MS)
// Debounce window for the mechanical switches and the encoder push switch.
#define GD_DEBOUNCE_MS 5
#endif

#if !defined(GD_ENC_STEPS_PER_DETENT)
// Quadrature transitions per physical detent.
//
// For the Alps EC11E15244G1 (30 detents / 15 pulses per revolution) this
// should be 2. If the knob emits two keypresses per click, raise it; if it
// takes two clicks to emit one keypress, lower it.
#define GD_ENC_STEPS_PER_DETENT 2
#endif

#if !defined(GD_ENC_PULSE_MS)
// How long a virtual rotate key is held down. Must be long enough for
// layout_task() to observe both the press and the release edge.
#define GD_ENC_PULSE_MS 12
#endif

//--------------------------------------------------------------------+
// API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the mechanical matrix and rotary encoder GPIO
 *
 * Call once from main(), after matrix_init().
 *
 * @return None
 */
void gd_keypad_init(void);

/**
 * @brief Scan the mechanical matrix and rotary encoder
 *
 * Writes key_matrix[GD_KEY_MECH_FIRST .. NUM_KEYS-1].is_pressed.
 *
 * Must be called from the main loop AFTER matrix_scan() and BEFORE
 * layout_task(). matrix_scan() writes is_pressed for every key including
 * ours (from meaningless ADC data), so we have to run after it; layout_task()
 * consumes is_pressed, so we have to run before it.
 *
 * @return None
 */
void gd_keypad_task(void);
