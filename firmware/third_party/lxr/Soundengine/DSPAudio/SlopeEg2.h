/*
 * SlopeEg2.h
 *
 *  Created on: 16.06.2012
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


#ifndef SLOPEEG2_H_
#define SLOPEEG2_H_



#include "lxr_port.h"


#define EG_STOPPED 	0
#define EG_A		1
#define EG_D		2
#define EG_REPEAT	3

#ifdef LXR_HARD_DOUBLE
typedef struct SLOPE_EG2_Struct
{
	LXR_DOUBLE 	attack;
	LXR_DOUBLE 	decay;
	LXR_DOUBLE 	slope;
	LXR_DOUBLE 	invSlope;
	uint8_t repeat; 				/**<number of repetitions of the attack phase*/
	uint8_t repeatCnt;				/**<a counter for the already played repeats*/
	LXR_DOUBLE 	value;
	uint8_t state;

} SlopeEg2;
#else
/* MIFM1: the FM-1 has no double precision. attack and decay are floats (the LXR-02's doubles hold float values),
 * the slopes pairs of floats (a double's value to 48 bits), value the LXR-02's double exactly: an integer in units
 * of 2^-40 (its sums of the floats above are exact in a double: here too). lxr_port.cc computes the curve from it
 * in pairs of floats */
typedef struct SLOPE_EG2_Struct
{
	float 	attack;
	float 	decay;
	float 	slope[2];
	float 	invSlope[2];
	uint8_t repeat; 				/**<number of repetitions of the attack phase*/
	uint8_t repeatCnt;				/**<a counter for the already played repeats*/
	int64_t value;
	uint8_t state;

} SlopeEg2;
#endif


void slopeEg2_init(SlopeEg2* eg);
void slopeEg2_trigger(SlopeEg2* eg);
LXR_DOUBLE slopeEg2_calc(SlopeEg2* eg);
void slopeEg2_setAttack(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setDecay(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setSlope(SlopeEg2* eg, uint8_t data2);
LXR_DOUBLE slopeEg2_calcDecay(uint8_t data2);


#endif /* SLOPEEG2_H_ */
