/*
 * CymbalVoice.c
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


#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/squareRootLut.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "lxr_port.h"
#include "Soundengine/config.h"
#include "lxr_port.h"
#include "lxr_port.h"
#include "lxr_port.h"

INDTCMZ CymbalVoice cymbalVoice[LXR_VOICES];
//---------------------------------------------------
void Cymbal_setPan(const uint8_t v, const uint8_t pan)
{
	//cymbalVoice[v].panL = squareRootLut[127-pan];
	//cymbalVoice[v].panR = squareRootLut[pan];
	cymbalVoice[v].pan = pan;
}
//---------------------------------------------------

void Cymbal_init(const uint8_t v)
{

	initOnePole(&cymbalVoice[v].FaderVolumeFilter);
	setOnePoleCoef(&cymbalVoice[v].FaderVolumeFilter,0.1);

	SnapEg_init(&cymbalVoice[v].snapEg);
	Cymbal_setPan(v,0);
	cymbalVoice[v].vol = 0.8f;

	//cymbalVoice[v].panModifier = 1.f;

	transient_init(&cymbalVoice[v].transGen);

	cymbalVoice[v].fmModAmount1 = 0.5f;
	cymbalVoice[v].fmModAmount2 = 0.5f;

	setDistortionShape(&cymbalVoice[v].distortion, 0.f);

	cymbalVoice[v].modOsc.freq = 440;
	cymbalVoice[v].modOsc.waveform = SINE;
	cymbalVoice[v].modOsc.fmMod = 0;
	cymbalVoice[v].modOsc.midiFreq = 70<<8 | 64;
	cymbalVoice[v].modOsc.pitchMod = 1.0f;
	cymbalVoice[v].modOsc.modNodeValue = 1;

	cymbalVoice[v].modOsc2.freq = 440;
	cymbalVoice[v].modOsc2.waveform = NOISE;//SINE;
	cymbalVoice[v].modOsc2.fmMod = 0;
	cymbalVoice[v].modOsc2.midiFreq = 70<<8| 64;
	cymbalVoice[v].modOsc2.pitchMod = 1.0f;
	cymbalVoice[v].modOsc2.modNodeValue = 1;

	cymbalVoice[v].osc.freq = 440;
	cymbalVoice[v].osc.waveform = 1;
	cymbalVoice[v].osc.fmMod = 1;
	cymbalVoice[v].osc.midiFreq = 70<<8 | 64;
	cymbalVoice[v].osc.pitchMod = 1.0f;
	cymbalVoice[v].osc.modNodeValue = 1;

	cymbalVoice[v].volumeMod = 1;

	slopeEg2_init(&cymbalVoice[v].oscVolEg);

	SVF_init(&cymbalVoice[v].filter);

	lfo_init(&cymbalVoice[v].lfo);

}

//---------------------------------------------------
void Cymbal_updatePan(const uint8_t v)
{
	cymbalVoice[v].triggeredPanL = sqrtf((127-cymbalVoice[v].pan)/127.f);
	cymbalVoice[v].triggeredPanR = sqrtf(cymbalVoice[v].pan/127.f);
}
//---------------------------------------------------
void Cymbal_trigger(const uint8_t v, const uint8_t vel, const uint8_t note)
{
	lfo_retrigger(4);
	//update velocity modulation
	modNode_updateValue(&velocityModulators[4],vel/127.f);

	float offset = 1;
	if(cymbalVoice[v].transGen.waveform==1) //offset mode
	{
		offset -= cymbalVoice[v].transGen.volume;
	}
	if(cymbalVoice[v].osc.waveform == SINE)
		cymbalVoice[v].osc.phase = (0x3ff<<20)*offset;//voiceArray[voiceNr].osc.startPhase ;
	else if(cymbalVoice[v].osc.waveform > SINE && cymbalVoice[v].osc.waveform <= REC)
		cymbalVoice[v].osc.phase = (0xff<<22)*offset;
	else
		cymbalVoice[v].osc.phase = 0;

	cymbalVoice[v].modOsc.phase = 0;
	cymbalVoice[v].modOsc2.phase = 0;

	osc_setBaseNote(&cymbalVoice[v].osc,note);
	osc_setBaseNote(&cymbalVoice[v].modOsc,note);
	osc_setBaseNote(&cymbalVoice[v].modOsc2,note);

	slopeEg2_trigger(&cymbalVoice[v].oscVolEg);
	cymbalVoice[v].velo = vel/127.f;

	transient_trigger(&cymbalVoice[v].transGen);

	SnapEg_trigger(&cymbalVoice[v].snapEg);

	Cymbal_updatePan(v);
}
//---------------------------------------------------
IN_ITCM_RAM void Cymbal_calcAsync(const uint8_t v)
{
	//calc the osc  vol eg
	cymbalVoice[v].egValueOscVol = slopeEg2_calc(&cymbalVoice[v].oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/*
	if(trigger_isGateModeOn())
	{
		if(!cymbalVoice[v].egValueOscVol) {
			trigger_triggerVoice(TRIGGER_5, TRIGGER_OFF);
			voiceControl_noteOff(TRIGGER_5);
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(cymbalVoice[v].transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&cymbalVoice[v].snapEg,cymbalVoice[v].transGen.pitch);
		cymbalVoice[v].osc.pitchMod = 1 + snapVal*cymbalVoice[v].transGen.volume;
	}

	//update osc phaseInc
	osc_setFreq(&cymbalVoice[v].osc);
	osc_setFreq(&cymbalVoice[v].modOsc);
	osc_setFreq(&cymbalVoice[v].modOsc2);


}
//---------------------------------------------------
void Cymbal_calcSyncBlock(const uint8_t v, int16_t* buf, const uint8_t size)
{
		int16_t mod[size];
		int16_t mod2[size];
		//calc next mod osc sample
		calcNextOscSampleBlock(&cymbalVoice[v].modOsc,mod,size,cymbalVoice[v].fmModAmount1, cymbalVoice[v].pwm);
		calcNextOscSampleBlock(&cymbalVoice[v].modOsc2,mod2,size,cymbalVoice[v].fmModAmount2, cymbalVoice[v].pwm);

		//combine both mod oscs to 1 modulation signal
		bufferTool_addBuffersSaturating(mod,mod2,size);

		calcNextOscSampleFmBlock(&cymbalVoice[v].osc,mod,buf,size,1.f, cymbalVoice[v].pwm) ;
#if USE_REVERB
		if(fxBus.fxType != FX_REVERB)
		{
#endif
			SVF_calcBlockZDF(&cymbalVoice[v].filter,cymbalVoice[v].filterType,buf,size);

#if USE_REVERB
		}
		else
		{
			SVF_calcBlockCheapZDF(&cymbalVoice[v].filter,cymbalVoice[v].filterType,buf,size);
		}
#endif

		//calc transient sample
		transient_calcBlock(&cymbalVoice[v].transGen,mod,size);

		uint8_t j;
		if(cymbalVoice[v].volumeMod)
		{
			cymbalVoice[v].combinedVolume = cymbalVoice[v].vol *cymbalVoice[v].velo *  cymbalVoice[v].egValueOscVol ;
			//const float vol = ;

#if 1
			lxr_qadd16_block(buf, mod, size);

			for(j=0;j<size;j++)
			{
				//add volume
				buf[j] *=  cymbalVoice[v].combinedVolume * calcOnePole( &cymbalVoice[v].FaderVolumeFilter, adc_getFaderVolume(4));
			}
#else
			for(j=0;j<size;j++)
			{
				//add filter to buffer
				buf[j] = (__QADD16(buf[j],mod[j])) ;
				buf[j] *=  vol;
			}
#endif
		}
		else
		{
			cymbalVoice[v].combinedVolume = cymbalVoice[v].vol * cymbalVoice[v].egValueOscVol;
			//const float vol = cymbalVoice[v].combinedVolume ;

#if 1
			lxr_qadd16_block(buf, mod, size);

			for(j=0;j<size;j++)
			{
				//add volume
				buf[j] *=  cymbalVoice[v].combinedVolume * calcOnePole( &cymbalVoice[v].FaderVolumeFilter, adc_getFaderVolume(4));
			}
#else
			for(j=0;j<size;j++)
			{
				//add filter to buffer
				buf[j] = (__QADD16(buf[j],mod[j])) ;
				buf[j] *=  vol;
			}
#endif
		}
		calcDistBlock(&cymbalVoice[v].distortion,buf,size);
}
//---------------------------------------------------

