/*
 * Snare.c
 *
 *  Created on: 17.04.2012
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



#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/squareRootLut.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "lxr_port.h"
#include "lxr_port.h"
#include "lxr_port.h"
#include "lxr_port.h"
//instance of the snare voice
INDTCMZ SnareVoice snareVoice[LXR_VOICES];

//---------------------------------------------------
void Snare_setPan(const uint8_t v, const uint8_t pan)
{
	//snareVoice[v].panL = squareRootLut[127-pan];
	//snareVoice[v].panR = squareRootLut[pan];
	snareVoice[v].pan = pan;
}
//---------------------------------------------------
void Snare_init(const uint8_t v)
{
	initOnePole(&snareVoice[v].FaderVolumeFilter);
	setOnePoleCoef(&snareVoice[v].FaderVolumeFilter,0.1);

	SnapEg_init(&snareVoice[v].snapEg);
	Snare_setPan(v,0);
	snareVoice[v].vol = 0.8f;

	//snareVoice[v].panModifier = 1.f;

	snareVoice[v].noiseOsc.freq = 440;
	snareVoice[v].noiseOsc.waveform = 1;
	snareVoice[v].noiseOsc.fmMod = 0;
	snareVoice[v].noiseOsc.midiFreq = 70<<8 | 64;
	snareVoice[v].noiseOsc.pitchMod = 1.0f;
	snareVoice[v].noiseOsc.modNodeValue = 1;

	snareVoice[v].osc.freq = 440;
	snareVoice[v].osc.waveform = 1;
	snareVoice[v].osc.fmMod = 0;
	snareVoice[v].osc.midiFreq = 70<<8 | 64;
	snareVoice[v].osc.modNodeValue = 1;

	setDistortionShape(&snareVoice[v].distortion, 0.f);

	snareVoice[v].volumeMod = 1;

	transient_init(&snareVoice[v].transGen);

	DecayEg_init(&snareVoice[v].oscPitchEg);
	snareVoice[v].egPitchModAmount = 0.5f;

	slopeEg2_init(&snareVoice[v].oscVolEg);



	SVF_init(&snareVoice[v].filter);

	lfo_init(&snareVoice[v].lfo);
}
//---------------------------------------------------
void Snare_updatePan(const uint8_t v)
{
	snareVoice[v].triggeredPanL = sqrtf((127-snareVoice[v].pan)/127.f);
	snareVoice[v].triggeredPanR = sqrtf(snareVoice[v].pan/127.f);
}
//---------------------------------------------------
void Snare_trigger(const uint8_t v, const uint8_t vel, const uint8_t note)
{
	lfo_retrigger(3);
	//update velocity modulation
	modNode_updateValue(&velocityModulators[3],vel/127.f);

	float offset = 1;
	if(snareVoice[v].transGen.waveform==1) //offset mode
	{
		offset -= snareVoice[v].transGen.volume;
	}
	if(snareVoice[v].osc.waveform == SINE)
		snareVoice[v].osc.phase = (0x3ff<<20)*offset;//voiceArray[voiceNr].osc.startPhase ;
	else if(snareVoice[v].osc.waveform > SINE && snareVoice[v].osc.waveform <= REC)
		snareVoice[v].osc.phase = (0xff<<22)*offset;
	else
		snareVoice[v].osc.phase = 0;

	DecayEg_trigger(&snareVoice[v].oscPitchEg);
	slopeEg2_trigger(&snareVoice[v].oscVolEg);
	snareVoice[v].velo = vel/127.f;

	osc_setBaseNote(&snareVoice[v].osc,note);
	//TODO noise muss mit transponiert werden

	transient_trigger(&snareVoice[v].transGen);

	SnapEg_trigger(&snareVoice[v].snapEg);

	Snare_updatePan(v);
}
//---------------------------------------------------
IN_ITCM_RAM void Snare_calcAsync(const uint8_t v)
{
	//add modulation eg to osc freq (1 = no change. a+eg = original freq + modulation
	const float egPitchVal = DecayEg_calc(&snareVoice[v].oscPitchEg);
	const float pitchEgValue = egPitchVal*snareVoice[v].egPitchModAmount;
	snareVoice[v].osc.pitchMod = 1+pitchEgValue;

	//calc the osc  vol eg
	snareVoice[v].egValueOscVol = slopeEg2_calc(&snareVoice[v].oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/*
	if(trigger_isGateModeOn())
	{
		if(!snareVoice[v].egValueOscVol) {
			//trigger_triggerVoice(TRIGGER_4, TRIGGER_OFF);
			//voiceControl_noteOff(TRIGGER_4);
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(snareVoice[v].transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&snareVoice[v].snapEg, snareVoice[v].transGen.pitch);
		snareVoice[v].osc.pitchMod += snapVal*snareVoice[v].transGen.volume;;
	}

	osc_setFreq(&snareVoice[v].osc);
	osc_setFreq(&snareVoice[v].noiseOsc);


}
//---------------------------------------------------
void Snare_calcSyncBlock(const uint8_t v, int16_t* buf, const uint8_t size)
{
	int16_t transBuf[size];
	int16_t oscBuf[size];

	calcNoiseBlock(&snareVoice[v].noiseOsc,buf,size,0.9f);
#if USE_REVERB
	if(fxBus.fxType != FX_REVERB)
	{
#endif
		SVF_calcBlockZDF(&snareVoice[v].filter,snareVoice[v].filterType,buf,size);
#if USE_REVERB
	}
	else
	{
		SVF_calcBlockCheapZDF(&snareVoice[v].filter,snareVoice[v].filterType,buf,size);
	}
#endif




	//calc next osc sample
	calcNextOscSampleBlock(&snareVoice[v].osc,oscBuf,size,(1.f-snareVoice[v].mix), snareVoice[v].pwm);
	// apply filter to synthesized sound as well here if desired, or combine code for more efficiency
	//SVF_calcBlockZDF(&snareVoice[v].filter,snareVoice[v].filterType,transBuf,size);


	//calc transient sample
	transient_calcBlock(&snareVoice[v].transGen,transBuf,size);


	uint8_t j;
	if(snareVoice[v].volumeMod)
	{
		snareVoice[v].combinedVolume = snareVoice[v].vol * snareVoice[v].velo * snareVoice[v].egValueOscVol ;
		//const float vol = ;

		for(j=0;j<size;j++)
		{

#if 1
			//float LXR_FMAF(float a, float b, float c);
			//result = a × b + c
			float tmp = LXR_FMAF(buf[j], snareVoice[v].mix, oscBuf[j]);
			//add transient sample
			//tmp	= __QADD16(transBuf[j],(int16_t)tmp);
			tmp	+= transBuf[j];
			buf[j] =   snareVoice[v].combinedVolume  * tmp * calcOnePole( &snareVoice[v].FaderVolumeFilter, adc_getFaderVolume(3));

#else

			//add filter to buffer
			buf[j] *= snareVoice[v].mix;
			buf[j] = (__QADD16(buf[j],transBuf[j])); //transBuf here is mod osc

			buf[j] *=  vol;
#endif
		}
	}
	else
	{
		snareVoice[v].combinedVolume = snareVoice[v].vol * snareVoice[v].egValueOscVol;
		//const float vol = snareVoice[v].combinedVolume * ;

		for(j=0;j<size;j++)
		{

#if 1
			float tmp = LXR_FMAF(buf[j], snareVoice[v].mix, oscBuf[j]);
			//add transient sample
			//tmp	= __QADD16(transBuf[j],(int16_t)tmp);
			tmp	+= transBuf[j];
			buf[j] =  snareVoice[v].combinedVolume * tmp * calcOnePole( &snareVoice[v].FaderVolumeFilter, adc_getFaderVolume(3));
#else
			//add filter to buffer
			buf[j] *= snareVoice[v].mix;
			buf[j] = (__QADD16(buf[j],transBuf[j]));
			buf[j] *=  vol;
#endif
		}
	}

	calcDistBlock(&snareVoice[v].distortion,buf,size);
}
//------------------------------------------------------------------------
