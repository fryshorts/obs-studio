/*
Copyright (C) 2014 by Leonhard Oelke <leonhard@in-verted.de>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "obs.h"

/**
 * @file
 * @brief Reusable IIR filter implementation
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IIR filter types
 */
enum iir_filter_type {
	IIR_HIGH_PASS,
	IIR_HIGH_SHELF
};

/**
 * @brief Structure for a direct form II biquad filter
 */
typedef struct iir_biquad2 {
	float mem[2];
	float c_a[2];
	float c_b[3];
} iir_biquad2_t;

/**
 * @brief Calculate and set coefficients for a biquad filter
 * @param bq pointer to the biquad structure
 * @param type type of filter
 * @param fc target frequency
 * @param fs sample rate
 * @param q
 * @param gain
 */
EXPORT void iir_biquad2_calc(struct iir_biquad2 *bq, enum iir_filter_type type,
		const float fc, const float fs,
		const float q, const float gain);

/**
 * @brief Iterate the filter
 */
EXPORT float iir_biquad2_iterate(struct iir_biquad2 *bq, const float in);

#ifdef __cplusplus
}
#endif
