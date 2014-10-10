#define _USRDLL
#include <afxwin.h>

#include <math.h>
#include <float.h>

#include "mpt_vsti.h"

AudioEffect *createEffectInstance(audioMasterCallback audioMaster)
{
	return new MPTVSTI(audioMaster);
}

MPTVSTI::MPTVSTI(audioMasterCallback audioMaster) : AudioEffectX(audioMaster, 0, 0)	// programs, parameters
{
	//inits here!

	setNumInputs(2);
	setNumOutputs(2);
	setUniqueID('MptV');    // identify here
	DECLARE_VST_DEPRECATED(canMono) ();
	canProcessReplacing();
	//strcpy(programName, "MPTVSTi");

}

bool  MPTVSTI::getProductString(char* text) { strcpy(text, "MPT VSTi"); return true; }
bool  MPTVSTI::getVendorString(char* text)  { strcpy(text, "MPT"); return true; }
bool  MPTVSTI::getEffectName(char* name)    { strcpy(name, "VSTi"); return true; }

MPTVSTI::~MPTVSTI()
{
}

void MPTVSTI::suspend()
{
}

//--------------------------------------------------------------------------------
// process

void MPTVSTI::process(float **inputs, float **outputs, VstInt32 sampleFrames)
{
	float *in1 = inputs[0];
	float *in2 = inputs[1];
	float *out1 = outputs[0];
	float *out2 = outputs[1];

	--in1;
	--in2;
	--out1;
	--out2;
	while (--sampleFrames >= 0)
	{


		(*++in1);
		(*++in2);



		*++out1 = 0;
		*++out2 = 0;
	}

}

void MPTVSTI::processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames)
{
	float *in1 = inputs[0];
	float *in2 = inputs[1];
	float *out1 = outputs[0];
	float *out2 = outputs[1];

	--in1;
	--in2;
	--out1;
	--out2;
	while (--sampleFrames >= 0)
	{


		(*++in1);
		(*++in2);



		*++out1 = 0;
		*++out2 = 0;
	}
}


// vstplugmain.cpp without DllMain

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:VSTPluginMain")
#pragma comment(linker, "/EXPORT:main=VSTPluginMain")
#else
#pragma comment(linker, "/EXPORT:_VSTPluginMain")
#pragma comment(linker, "/EXPORT:_main=_VSTPluginMain")
#endif

//------------------------------------------------------------------------
/** Must be implemented externally. */
extern AudioEffect* createEffectInstance(audioMasterCallback audioMaster);

extern "C" {

#if defined (__GNUC__) && ((__GNUC__ >= 4) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1)))
#define VST_EXPORT	__attribute__ ((visibility ("default")))
#else
#define VST_EXPORT
#endif

	//------------------------------------------------------------------------
	/** Prototype of the export function main */
	//------------------------------------------------------------------------
	VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster)
	{
		AFX_MANAGE_STATE(AfxGetStaticModuleState());

		// Get VST Version of the Host
		if (!audioMaster(0, audioMasterVersion, 0, 0, 0, 0))
			return 0;  // old version

		// Create the AudioEffect
		AudioEffect* effect = createEffectInstance(audioMaster);
		if (!effect)
			return 0;

		
			CWinApp *pApp = AfxGetApp();

		CWnd *pWnd = pApp->GetMainWnd();
		pWnd->ShowWindow(SW_SHOW);

		// Return the VST AEffect structur
		return effect->getAeffect();
	}

	// support for old hosts not looking for VSTPluginMain
#if (TARGET_API_MAC_CARBON && __ppc__)
	VST_EXPORT AEffect* main_macho(audioMasterCallback audioMaster) { return VSTPluginMain(audioMaster); }
#elif WIN32
	VST_EXPORT AEffect* MAIN(audioMasterCallback audioMaster) { return VSTPluginMain(audioMaster); }
#elif BEOS
	VST_EXPORT AEffect* main_plugin(audioMasterCallback audioMaster) { return VSTPluginMain(audioMaster); }
#endif

} // extern "C"



