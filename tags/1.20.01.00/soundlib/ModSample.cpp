/*
 * ModSample.h
 * -----------
 * Purpose: Module Sample header class and helpers
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Sndfile.h"
#include "ModSample.h"


// Translate sample properties between two given formats.
void ModSample::Convert(MODTYPE fromType, MODTYPE toType)
//-------------------------------------------------------
{
	// Convert between frequency and transpose values if necessary.
	if ((!(toType & (MOD_TYPE_MOD | MOD_TYPE_XM))) && (fromType & (MOD_TYPE_MOD | MOD_TYPE_XM)))
	{
		nC5Speed = CSoundFile::TransposeToFrequency(RelativeTone, nFineTune);
		RelativeTone = 0;
		nFineTune = 0;
	} else if((toType & (MOD_TYPE_MOD | MOD_TYPE_XM)) && (!(fromType & (MOD_TYPE_MOD | MOD_TYPE_XM))))
	{
		CSoundFile::FrequencyToTranspose(this);
		if(toType & MOD_TYPE_MOD)
		{
			RelativeTone = 0;
		}
	}

	// No ping-pong loop, panning and auto-vibrato for MOD / S3M samples
	if(toType & (MOD_TYPE_MOD | MOD_TYPE_S3M))
	{
		uFlags &= ~(CHN_PINGPONGLOOP | CHN_PANNING);

		nVibDepth = 0;
		nVibRate = 0;
		nVibSweep = 0;
		nVibType = VIB_SINE;
	}

	// No sustain loops for MOD/S3M/XM
	if(toType & (MOD_TYPE_MOD | MOD_TYPE_XM | MOD_TYPE_S3M))
	{
		// Sustain loops - convert to normal loops
		if((uFlags & CHN_SUSTAINLOOP) != 0)
		{
			// We probably overwrite a normal loop here, but since sustain loops are evaluated before normal loops, this is just correct.
			nLoopStart = nSustainStart;
			nLoopEnd = nSustainEnd;
			uFlags |= CHN_LOOP;
			if(uFlags & CHN_PINGPONGSUSTAIN)
			{
				uFlags |= CHN_PINGPONGLOOP;
			} else
			{
				uFlags &= ~CHN_PINGPONGLOOP;
			}
		}
		nSustainStart = nSustainEnd = 0;
		uFlags &= ~(CHN_SUSTAINLOOP|CHN_PINGPONGSUSTAIN);
	}

	// All XM samples have default panning, and XM's autovibrato settings are rather limited.
	if(toType & MOD_TYPE_XM)
	{
		if(!(uFlags & CHN_PANNING))
		{
			uFlags |= CHN_PANNING;
			nPan = 128;
		}

		LimitMax(nVibDepth, BYTE(15));
		LimitMax(nVibRate, BYTE(63));
	}


	// Autovibrato sweep setting is inverse in XM (0 = "no sweep") and IT (0 = "no vibrato")
	if(((fromType & MOD_TYPE_XM) && (toType & (MOD_TYPE_IT | MOD_TYPE_MPT))) || ((toType & MOD_TYPE_XM) && (fromType & (MOD_TYPE_IT | MOD_TYPE_MPT))))
	{
		if(nVibRate != 0 && nVibDepth != 0)
		{
			nVibSweep = 255 - nVibSweep;
		}
	}
}