/*
 * random.h
 *
 *  Created on: 07.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 *
 * Modified for MIFM1 (2026): ported to the FM-1, see MIFM1.md in this directory.
 */


#ifndef RANDOM_H_
#define RANDOM_H_

#include "lxr_port.h"

/* MIFM1: an oscillator's own xorshift instead of the STM32's random number generator */
static inline uint32_t GetRngValue(uint32_t* state)
{
	uint32_t x = *state ? *state : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

#endif /* RANDOM_H_ */
