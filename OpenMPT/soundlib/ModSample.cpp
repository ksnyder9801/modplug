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
#include "modsmp_ctrl.h"

#include <cmath>


OPENMPT_NAMESPACE_BEGIN


// Translate sample properties between two given formats.
void ModSample::Convert(MODTYPE fromType, MODTYPE toType)
//-------------------------------------------------------
{
	// Convert between frequency and transpose values if necessary.
	if((!(toType & (MOD_TYPE_MOD | MOD_TYPE_XM))) && (fromType & (MOD_TYPE_MOD | MOD_TYPE_XM)))
	{
		TransposeToFrequency();
		RelativeTone = 0;
		nFineTune = 0;
	} else if((toType & (MOD_TYPE_MOD | MOD_TYPE_XM)) && (!(fromType & (MOD_TYPE_MOD | MOD_TYPE_XM))))
	{
		FrequencyToTranspose();
	}

	// No ping-pong loop, panning and auto-vibrato for MOD / S3M samples
	if(toType & (MOD_TYPE_MOD | MOD_TYPE_S3M))
	{
		uFlags.reset(CHN_PINGPONGLOOP | CHN_PANNING);

		nVibDepth = 0;
		nVibRate = 0;
		nVibSweep = 0;
		nVibType = VIB_SINE;

		RelativeTone = 0;
	}

	// No global volume sustain loops for MOD/S3M/XM
	if(toType & (MOD_TYPE_MOD | MOD_TYPE_XM | MOD_TYPE_S3M))
	{
		nGlobalVol = 64;
		// Sustain loops - convert to normal loops
		if(uFlags[CHN_SUSTAINLOOP])
		{
			// We probably overwrite a normal loop here, but since sustain loops are evaluated before normal loops, this is just correct.
			nLoopStart = nSustainStart;
			nLoopEnd = nSustainEnd;
			uFlags.set(CHN_LOOP);
			uFlags.set(CHN_PINGPONGLOOP, uFlags[CHN_PINGPONGSUSTAIN]);
		}
		nSustainStart = nSustainEnd = 0;
		uFlags.reset(CHN_SUSTAINLOOP|CHN_PINGPONGSUSTAIN);
	}

	// All XM samples have default panning, and XM's autovibrato settings are rather limited.
	if(toType & MOD_TYPE_XM)
	{
		if(!uFlags[CHN_PANNING])
		{
			uFlags.set(CHN_PANNING);
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
	// Convert incompatible autovibrato types
	if(toType == MOD_TYPE_IT && nVibType == VIB_RAMP_UP)
	{
		nVibType = VIB_RAMP_DOWN;
	} else if(toType == MOD_TYPE_XM && nVibType == VIB_RANDOM)
	{
		nVibType = VIB_SINE;
	}
}


// Initialize sample slot with default values.
void ModSample::Initialize(MODTYPE type)
//--------------------------------------
{
	nLength = 0;
	nLoopStart = nLoopEnd = 0;
	nSustainStart = nSustainEnd = 0;
	nC5Speed = 8363;
	nPan = 128;
	nVolume = 256;
	nGlobalVol = 64;
	uFlags.reset(CHN_PANNING | CHN_SUSTAINLOOP | CHN_LOOP | CHN_PINGPONGLOOP | CHN_PINGPONGSUSTAIN);
	if(type == MOD_TYPE_XM)
	{
		uFlags.set(CHN_PANNING);
	}
	RelativeTone = 0;
	nFineTune = 0;
	nVibType = VIB_SINE;
	nVibSweep = 0;
	nVibDepth = 0;
	nVibRate = 0;
	filename[0] = '\0';
}


// Returns sample rate of the sample.
uint32 ModSample::GetSampleRate(const MODTYPE type) const
//-------------------------------------------------------
{
	uint32 rate;
	if(type & (MOD_TYPE_MOD | MOD_TYPE_XM))
		rate = TransposeToFrequency(RelativeTone, nFineTune);
	else
		rate = nC5Speed;
	return (rate > 0) ? rate : 8363;
}


// Allocate sample based on a ModSample's properties.
// Returns number of bytes allocated, 0 on failure.
size_t ModSample::AllocateSample()
//--------------------------------
{
	FreeSample();

	if((pSample = AllocateSample(nLength, GetBytesPerSample())) == nullptr)
	{
		return 0;
	} else
	{
		return GetSampleSizeInBytes();
	}
}


// Allocate sample memory. On sucess, a pointer to the silenced sample buffer is returned. On failure, nullptr is returned.
void *ModSample::AllocateSample(SmpLength numSamples, size_t bytesPerSample)
//--------------------------------------------------------------------------
{
	const size_t allocSize = GetRealSampleBufferSize(numSamples, bytesPerSample);

	if(allocSize != 0)
	{
		char *p = new (std::nothrow) char[allocSize];
		if(p != nullptr)
		{
			memset(p, 0, allocSize);
			return p + (InterpolationMaxLookahead * MaxSamplingPointSize);
		}
	}
	return nullptr;
}


// Compute sample buffer size in bytes, including any overhead introduced by pre-computed loops and such. Returns 0 if sample is too big.
size_t ModSample::GetRealSampleBufferSize(SmpLength numSamples, size_t bytesPerSample)
//------------------------------------------------------------------------------------
{
	// Number of required lookahead samples:
	// * 1x InterpolationMaxLookahead samples before the actual sample start. NOTE: This is currently hardcoded to 16!
	// * 1x InterpolationMaxLookahead samples of silence after the sample end (if normal loop end == sample end, this can be optimized out).
	// * 2x InterpolationMaxLookahead before the loop point (because we start at InterpolationMaxLookahead before the loop point and will look backwards from there as well)
	// * 2x InterpolationMaxLookahead after the loop point (for wrap-around)
	// * 4x InterpolationMaxLookahead for the sustain loop (same as the two points above)
	
	const SmpLength maxSize = Util::MaxValueOfType(numSamples);
	const SmpLength lookaheadBufferSize = 16 + (1 + 4 + 4) * InterpolationMaxLookahead;

	if(numSamples > maxSize || lookaheadBufferSize > maxSize - numSamples)
	{
		return 0;
	}
	numSamples += lookaheadBufferSize;

	if(maxSize / bytesPerSample < numSamples)
	{
		return 0;
	}

	return numSamples * bytesPerSample;
}


void ModSample::FreeSample()
//--------------------------
{
	FreeSample(pSample);
	pSample = nullptr;
}


void ModSample::FreeSample(void *samplePtr)
//-----------------------------------------
{
	if(samplePtr)
	{
		delete[] (((char *)samplePtr) - (InterpolationMaxLookahead * MaxSamplingPointSize));
	}
}


// Set loop points and update loop wrap-around buffer
void ModSample::SetLoop(SmpLength start, SmpLength end, bool enable, bool pingpong, CSoundFile &sndFile)
//------------------------------------------------------------------------------------------------------
{
	nLoopStart = start;
	nLoopEnd = end;
	LimitMax(nLoopEnd, nLength);
	if(nLoopStart < nLoopEnd)
	{
		uFlags.set(CHN_LOOP, enable);
		uFlags.set(CHN_PINGPONGLOOP, pingpong && enable);
	} else
	{
		nLoopStart = nLoopEnd = 0;
		uFlags.reset(CHN_LOOP | CHN_PINGPONGLOOP);
	}
	PrecomputeLoops(sndFile, true);
}


// Set sustain loop points and update loop wrap-around buffer
void ModSample::SetSustainLoop(SmpLength start, SmpLength end, bool enable, bool pingpong, CSoundFile &sndFile)
//-------------------------------------------------------------------------------------------------------------
{
	nSustainStart = start;
	nSustainEnd = end;
	LimitMax(nLoopEnd, nLength);
	if(nSustainStart < nSustainEnd)
	{
		uFlags.set(CHN_SUSTAINLOOP, enable);
		uFlags.set(CHN_PINGPONGSUSTAIN, pingpong && enable);
	} else
	{
		nSustainStart = nSustainEnd = 0;
		uFlags.reset(CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN);
	}
	PrecomputeLoops(sndFile, true);
}


void ModSample::PrecomputeLoops(CSoundFile &sndFile, bool updateChannels)
//-----------------------------------------------------------------------
{
	ctrlSmp::PrecomputeLoops(*this, sndFile, updateChannels);
}


// Remove loop points if they're invalid.
void ModSample::SanitizeLoops()
//-----------------------------
{
	LimitMax(nSustainEnd, nLength);
	LimitMax(nLoopEnd, nLength);
	if(nSustainStart >= nSustainEnd)
	{
		nSustainStart = nSustainEnd = 0;
		uFlags.reset(CHN_SUSTAINLOOP | CHN_PINGPONGSUSTAIN);
	}
	if(nLoopStart >= nLoopEnd)
	{
		nLoopStart = nLoopEnd = 0;
		uFlags.reset(CHN_LOOP | CHN_PINGPONGLOOP);
	}
}


/////////////////////////////////////////////////////////////
// Transpose <-> Frequency conversions

uint32 ModSample::TransposeToFrequency(int transpose, int finetune)
//-----------------------------------------------------------------
{
	return Util::Round<uint32>(std::pow(2.0f, (transpose * 128.0f + finetune) * (1.0f / (12.0f * 128.0f))) * 8363.0f);
}


void ModSample::TransposeToFrequency()
//------------------------------------
{
	nC5Speed = TransposeToFrequency(RelativeTone, nFineTune);
}


int ModSample::FrequencyToTranspose(uint32 freq)
//----------------------------------------------
{
	const float inv_log_2 = 1.44269504089f; // 1.0f/std::log(2.0f)
	return Util::Round<int>(std::log(freq * (1.0f / 8363.0f)) * (12.0f * 128.0f * inv_log_2));
}


void ModSample::FrequencyToTranspose()
//------------------------------------
{
	int f2t;
	if(nC5Speed)
		f2t = FrequencyToTranspose(nC5Speed);
	else
		f2t = 0;
	int transpose = f2t >> 7;
	int finetune = f2t & 0x7F;	//0x7F == 111 1111
	if(finetune > 80)			// XXX Why is this 80?
	{
		transpose++;
		finetune -= 128;
	}
	Limit(transpose, -127, 127);
	RelativeTone = static_cast<int8>(transpose);
	nFineTune = static_cast<int8>(finetune);
}


OPENMPT_NAMESPACE_END
