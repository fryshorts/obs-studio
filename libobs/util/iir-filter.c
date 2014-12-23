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

#include <math.h>

#include "iir-filter.h"

void iir_biquad2_calc(struct iir_biquad2 *bq, enum iir_filter_type type,
		const float fc, const float fs,
		const float q, const float gain)
{
	float v = powf(10.0f, fabs(gain) / 20);
	float k = tanf(M_PI * fc / fs);
	float n;

	switch(type) {
	case IIR_HIGH_PASS:
		n = 1.0f / (1.0f + k / q + k * k);
		bq->c_a[0] = 2 * (k * k - 1.0f) * n;
		bq->c_a[1] = (1.0f - k / q + k * k) * n;
		bq->c_b[0] = n;
		bq->c_b[1] = bq->c_b[0] * -2.0f;
		bq->c_b[2] = bq->c_b[0];
		break;
	case IIR_HIGH_SHELF:
		n = 1.0f / (1.0f + M_SQRT2 * k + k * k);
		bq->c_a[0] = 2.0f * (k * k - 1.0f) * n;
		bq->c_a[1] = (1.0f - M_SQRT2 * k + k * k) * n;
		bq->c_b[0] = (v + sqrtf(2.0f * v) * k + k * k) * n;
		bq->c_b[1] = 2.0f * (k * k - v) * n;
		bq->c_b[2] = (v - sqrtf(2.0f * v) * k + k * k) * n;
		break;
	}
}

float iir_biquad2_iterate(struct iir_biquad2 *bq, const float in)
{
	float out;

	out        = bq->mem[1] + in * bq->c_b[0];
	bq->mem[1] = bq->mem[0] + in * bq->c_b[1] - out * bq->c_a[0];
	bq->mem[0] =              in * bq->c_b[2] - out * bq->c_a[1];

	return out;
}
