/*
 * HiHat.c
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



#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/squareRootLut.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "lxr_port.h"
#include "lxr_port.h"
#include "lxr_port.h"
#include "lxr_port.h"

INDTCMZ HiHatVoice hatVoice[LXR_VOICES];

//---------------------------------------------------
void HiHat_setPan(const uint8_t v, const uint8_t pan)
{
	//hatVoice[v].panL = squareRootLut[127-pan];
	//hatVoice[v].panR = squareRootLut[pan];
	hatVoice[v].pan = pan;
}
//---------------------------------------------------

void HiHat_init(const uint8_t v)
{
	initOnePole(&hatVoice[v].FaderVolumeFilter);
	setOnePoleCoef(&hatVoice[v].FaderVolumeFilter,0.1);

	SnapEg_init(&hatVoice[v].snapEg);
	HiHat_setPan(v,0);
	hatVoice[v].vol = 0.8f;

	//hatVoice[v].panModifier = 1.f;

	transient_init(&hatVoice[v].transGen);

	hatVoice[v].fmModAmount1 = 0.5f;
	hatVoice[v].fmModAmount2 = 0.5f;

	setDistortionShape(&hatVoice[v].distortion, 0.f);

	hatVoice[v].modOsc.freq = 440;
	hatVoice[v].modOsc.waveform = SINE;
	hatVoice[v].modOsc.fmMod = 0;
	hatVoice[v].modOsc.midiFreq = 70<<8 | 64 ;
	hatVoice[v].modOsc.pitchMod = 1.0f;
	hatVoice[v].modOsc.modNodeValue = 1;

	hatVoice[v].modOsc2.freq = 440;
	hatVoice[v].modOsc2.waveform = NOISE;//SINE;
	hatVoice[v].modOsc2.fmMod = 0;
	hatVoice[v].modOsc2.midiFreq = 70<<8 | 64;
	hatVoice[v].modOsc2.pitchMod = 1.0f;
	hatVoice[v].modOsc2.modNodeValue = 1;

	hatVoice[v].osc.freq = 440;
	hatVoice[v].osc.waveform = 1;
	hatVoice[v].osc.fmMod = 1;
	hatVoice[v].osc.midiFreq = 70<<8 | 64;
	hatVoice[v].osc.pitchMod = 1.0f;
	hatVoice[v].osc.modNodeValue = 1;

	hatVoice[v].volumeMod = 1;

	slopeEg2_init(&hatVoice[v].oscVolEg);

	SVF_init(&hatVoice[v].filter);

	lfo_init(&hatVoice[v].lfo);
}

//---------------------------------------------------
void HiHat_updatePan(const uint8_t v)
{
	hatVoice[v].triggeredPanL = sqrtf((127-hatVoice[v].pan)/127.f);
	hatVoice[v].triggeredPanR = sqrtf(hatVoice[v].pan/127.f);
}
//---------------------------------------------------
void HiHat_trigger(const uint8_t v, uint8_t vel, uint8_t isOpen, const uint8_t note)
{
	lfo_retrigger(5);

	//update velocity modulation
	modNode_updateValue(&velocityModulators[5],vel/127.f);

	float offset = 1;
	if(hatVoice[v].transGen.waveform==1) //offset mode
	{
		offset -= hatVoice[v].transGen.volume;
	}
	if(hatVoice[v].osc.waveform == SINE)
		hatVoice[v].osc.phase = (0x3ff<<20)*offset;//voiceArray[voiceNr].osc.startPhase ;
	else if(hatVoice[v].osc.waveform > SINE && hatVoice[v].osc.waveform <= REC)
		hatVoice[v].osc.phase = (0xff<<22)*offset;
	else
		hatVoice[v].osc.phase = 0;

	hatVoice[v].modOsc.phase = 0;
	hatVoice[v].modOsc2.phase = 0;

	osc_setBaseNote(&hatVoice[v].osc,note);
	osc_setBaseNote(&hatVoice[v].modOsc,note);
	osc_setBaseNote(&hatVoice[v].modOsc2,note);

	hatVoice[v].isOpen = isOpen;
	hatVoice[v].oscVolEg.decay = isOpen?hatVoice[v].decayOpen:hatVoice[v].decayClosed;

	slopeEg2_trigger(&hatVoice[v].oscVolEg);
	hatVoice[v].velo = vel/127.f;
	transient_trigger(&hatVoice[v].transGen);

	SnapEg_trigger(&hatVoice[v].snapEg);

	HiHat_updatePan(v);
}
//---------------------------------------------------
IN_ITCM_RAM void HiHat_calcAsync(const uint8_t v)
{
	//calc the osc  vol eg
	hatVoice[v].egValueOscVol = slopeEg2_calc(&hatVoice[v].oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/*
	if(trigger_isGateModeOn())
	{
		if(!hatVoice[v].egValueOscVol)
		{
			if(hatVoice[v].isOpen)
			{
				//trigger_triggerVoice(TRIGGER_7, TRIGGER_OFF);
				voiceControl_noteOff(TRIGGER_7);
			} else {
				//trigger_triggerVoice(TRIGGER_6, TRIGGER_OFF);
				voiceControl_noteOff(TRIGGER_6);
			}
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(hatVoice[v].transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&hatVoice[v].snapEg, hatVoice[v].transGen.pitch);
		hatVoice[v].osc.pitchMod = 1 + snapVal*hatVoice[v].transGen.volume;
	}

	osc_setFreq(&hatVoice[v].osc);
	osc_setFreq(&hatVoice[v].modOsc);
	osc_setFreq(&hatVoice[v].modOsc2);


}
//---------------------------------------------------
void HiHat_calcSyncBlock(const uint8_t v, int16_t* buf, const uint8_t size)
{
	//2 buffers for the mod oscs
	int16_t mod1[size],mod2[size];
	//calc next mod osc samples, scaled with mod amount
	calcNextOscSampleBlock(&hatVoice[v].modOsc,mod1,size, hatVoice[v].fmModAmount1, hatVoice[v].pwm);
	calcNextOscSampleBlock(&hatVoice[v].modOsc2,mod2,size,  hatVoice[v].fmModAmount2, hatVoice[v].pwm);

	//combine both mod oscs to 1 modulation signal
	bufferTool_addBuffersSaturating(mod1,mod2,size);

	calcNextOscSampleFmBlock(&hatVoice[v].osc,mod1,buf,size,0.5f, hatVoice[v].pwm) ;

#if USE_REVERB
	if(fxBus.fxType != FX_REVERB)
	{
#endif
		SVF_calcBlockZDF(&hatVoice[v].filter,hatVoice[v].filterType,buf,size);

#if USE_REVERB
	}
	else
	{
		SVF_calcBlockCheapZDF(&hatVoice[v].filter,hatVoice[v].filterType,buf,size);
	}
#endif
	//calc transient sample
	transient_calcBlock(&hatVoice[v].transGen,mod1,size);

	uint8_t j;
	if(hatVoice[v].volumeMod)
	{
		hatVoice[v].combinedVolume = hatVoice[v].vol * hatVoice[v].velo * hatVoice[v].egValueOscVol; ;
		//const float vol =

#if 1
			lxr_qadd16_block(buf, mod1, size);

			for(j=0;j<size;j++)
			{
				//add volume
				buf[j] *=  hatVoice[v].combinedVolume * calcOnePole( &hatVoice[v].FaderVolumeFilter, adc_getFaderVolume(5));
			}
#else
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] = __QADD16(buf[j],mod1[j]);
			buf[j] *= vol;
		}
#endif
	}
	else
	{
		hatVoice[v].combinedVolume = hatVoice[v].vol * hatVoice[v].egValueOscVol;
		//const float vol = hatVoice[v].combinedVolume ;
#if 1
			lxr_qadd16_block(buf, mod1, size);

			for(j=0;j<size;j++)
			{
				//add volume
				buf[j] *=  hatVoice[v].combinedVolume * calcOnePole( &hatVoice[v].FaderVolumeFilter, adc_getFaderVolume(5));
			}
#else
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] = __QADD16(buf[j],mod1[j]);

			buf[j] *= vol;
		}
#endif
	}

	calcDistBlock(&hatVoice[v].distortion,buf,size);
}
//---------------------------------------------------

