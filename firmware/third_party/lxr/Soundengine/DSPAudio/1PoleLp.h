/*
 * 1PoleLp.h
 *
 *  Created on: 08.04.2012
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


#ifndef ONE_POLELP_H_
#define ONE_POLELP_H_
//-------------------------------------------------------------
#include "lxr_port.h"
//-------------------------------------------------------------
#define CALC_FILTER_FREQ(f) (1-expf(-2.f*(LXR_DOUBLE)M_PI*f/(float)REAL_FS))

typedef struct OnePoleStruct
{
	LXR_DOUBLE r;
	LXR_DOUBLE b;
} OnePoleFilter;
//-------------------------------------------------------------
void initOnePole(OnePoleFilter* filter);
float calcOnePole(OnePoleFilter* filter, LXR_DOUBLE input);
float calcOnePoleAdaptiveVolume(OnePoleFilter* filter, LXR_DOUBLE input);
void calcOnePoleBlock(OnePoleFilter* filter, int16_t* buf, const uint8_t size);
void calcOnePoleBlockFixedInput(OnePoleFilter* filter, float input,float* output, const uint8_t size);
void setOnePoleCoef(OnePoleFilter* filter,LXR_DOUBLE val);
void setOnePoleValue(OnePoleFilter* filter,LXR_DOUBLE val);
void setOnePoleFreq(OnePoleFilter* filter,LXR_DOUBLE f);

#endif /* ONE_POLELP_H_ */
