#pragma once

#include "include/vstsdk2.4/public.sdk/source/vst2.x/audioeffect.h"
#include "include/vstsdk2.4/public.sdk/source/vst2.x/audioeffectx.h"

class MPTVSTI : public AudioEffectX
{
public:
	MPTVSTI(audioMasterCallback audioMaster);
	~MPTVSTI();

	virtual void process(float **inputs, float **outputs, VstInt32 sampleFrames);
	virtual void processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames);
	virtual void suspend();

	virtual bool getEffectName(char *name);
	virtual bool getVendorString(char *text);
	virtual bool getProductString(char *text);
	virtual VstInt32 getVendorVersion() { return 1000; }


};

