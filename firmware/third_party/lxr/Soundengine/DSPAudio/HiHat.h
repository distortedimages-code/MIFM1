/*
 * HiHat.h
 *
 *  Created on: 18.04.2012
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


#ifndef HIHAT_H_
#define HIHAT_H_

#include "lxr_port.h"
#include "Soundengine/DSPAudio/Oscillator.h"
#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/DSPAudio/transientGenerator.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/distortion.h"
#include "Soundengine/DSPAudio/snapEg.h"
#include "Soundengine/DSPAudio/1PoleLp.h"

//#define NUM_HAT 1

typedef struct HiHatStruct
{

	OscInfo 	osc;		// the tonal oscillator
	OscInfo 	modOsc;		// the mod osc1
	OscInfo 	modOsc2;	// the mod osc2
	float		fmModAmount1;
	float		fmModAmount2;

	float	 	vol;		// volume of the voice
	//float		panL;		// [0:1]
	//float		panR;		// [0:1]
	uint8_t pan;
	float 	triggeredPanL; //only updated with the latest pan value when triggered
	float 	triggeredPanR;
	//float 		panModifier;

	float 		velo;

	int32_t		noiseSample;

	ResonantFilter filter;
	uint8_t		filterType;

	TransientGenerator transGen;

	float		decayClosed;
	float		decayOpen;

	Distortion distortion;

	SlopeEg2		oscVolEg;
	float 		egValueOscVol;
	Lfo 		lfo;
	SnapEg snapEg;
	uint8_t volumeMod;	//modulate volume by midi velocity if 1
	uint8_t isOpen;		//is 1 if urrent playing voice is open hihat, 0 for closed

	float combinedVolume;
	OnePoleFilter FaderVolumeFilter;

	float pwm;

} HiHatVoice;

extern HiHatVoice hatVoice[LXR_VOICES];

//initialize all the parameters to sane values
void HiHat_init(const uint8_t v);

void HiHat_trigger(const uint8_t v, uint8_t vel, uint8_t isOpen, const uint8_t note);
void HiHat_updatePan(const uint8_t v);
void HiHat_calcSyncBlock(const uint8_t v, int16_t* buf, const uint8_t size);

/** calculate envelopes etc (all 16 samples */
void HiHat_calcAsync(const uint8_t v);

//-0.5 = left 0=both max 0.5=right*/
void HiHat_setPan(const uint8_t v, const uint8_t pan);

#endif /* HIHAT_H_ */
