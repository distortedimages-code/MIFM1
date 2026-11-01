/*
 * wavetable.h
 *
 *  Created on: 01.04.2012
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


#ifndef WAVETABLES_H_
#define WAVETABLES_H_

#include "lxr_port.h"
#include <stdio.h>

#define TABLESIZE 4096
#define TABLES 128

extern const int16_t sine_table[TABLESIZE+1];

/*the midi note numbers for the 11 wavetables are
0 - 0	- 8.1758Hz
1 - 12	- 16.3516Hz
2 - 24	- 32.7032
3 - 36	- 65.4064
4 - 48	- 130.813
5 - 60	- 261.626
6 - 72	- 523.251
7 - 84	- 1046.5
8 - 96	- 2093
9 - 108	- 4186.01
10 - 120- 8372.02
 */

/* MIFM1: the three tables (11 rows of 1024 each) in one array, in the order wavetable.c defines them, as
 * the FM-1's resource region keeps them (tools/split_tables.py): the interpolation at a row's last sample
 * reads one past it, the next row's first (past a table's last row, the next table's first; past the
 * last one, a copy of its last row's first sample) */
extern const int16_t lxr_wave_tables[3 * 11 * 1024 + 1];
#define sawTable ((const int16_t (*)[1024])(lxr_wave_tables))
#define recTable ((const int16_t (*)[1024])(lxr_wave_tables + 11 * 1024))
#define triTable ((const int16_t (*)[1024])(lxr_wave_tables + 22 * 1024))

#endif /* WAVETABLES_H_ */
