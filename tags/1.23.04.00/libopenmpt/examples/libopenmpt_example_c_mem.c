/*
 * libopenmpt_example_c_mem.c
 * --------------------------
 * Purpose: libopenmpt C API simple example
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libopenmpt/libopenmpt.h>

#include <portaudio.h>

#define BUFFERSIZE 480
#define SAMPLERATE 48000

static int16_t left[BUFFERSIZE];
static int16_t right[BUFFERSIZE];
static int16_t* const buffers[2] = {left,right};

int main(int argc,char* argv[]){
	FILE* file = 0;
	size_t size = 0;
	void* data = 0;
	openmpt_module* mod = 0;
	size_t count = 0;
	PaStream* stream = 0;
	PaStreamParameters streamparameters;
	memset(&streamparameters,0,sizeof(PaStreamParameters));
	(void)argc;
	file = fopen(argv[1],"rb");
	fseek(file,0,SEEK_END);
	size = ftell(file);
	fseek(file,0,SEEK_SET);
	data = malloc(size);
	size = fread(data,1,size,file);
	fclose(file);
	mod = openmpt_module_create_from_memory(data,size,NULL,NULL,NULL);
	free(data);
	Pa_Initialize();
	streamparameters.device = Pa_GetDefaultOutputDevice();
	streamparameters.channelCount = 2;
	streamparameters.sampleFormat = paInt16|paNonInterleaved;
	streamparameters.suggestedLatency = Pa_GetDeviceInfo(streamparameters.device)->defaultHighOutputLatency;
	Pa_OpenStream(&stream,NULL,&streamparameters,SAMPLERATE,paFramesPerBufferUnspecified,0,NULL,NULL);
	Pa_StartStream(stream);
	while(1){
		count = openmpt_module_read_stereo(mod,SAMPLERATE,BUFFERSIZE,left,right);
		if(count==0){
			break;
		}
		Pa_WriteStream(stream,buffers,count);
	}
	Pa_StopStream(stream);
	Pa_CloseStream(stream);
	Pa_Terminate();
	openmpt_module_destroy(mod);
	return 0;
}
