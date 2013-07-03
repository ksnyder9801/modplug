/*
 * WAVTools.cpp
 * ------------
 * Purpose: Definition of WAV file structures and helper functions
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"
#include "WAVTools.h"


///////////////////////////////////////////////////////////
// WAV Reading


WAVReader::WAVReader(FileReader &inputFile) : file(inputFile)
//-----------------------------------------------------------
{
	file.Rewind();

	RIFFHeader fileHeader;
	isDLS = false;
	if(!file.ReadConvertEndianness(fileHeader)
		|| (fileHeader.magic != RIFFHeader::idRIFF && fileHeader.magic != RIFFHeader::idLIST)
		|| (fileHeader.type != RIFFHeader::idWAVE && fileHeader.type != RIFFHeader::idwave))
	{
		return;
	}

	isDLS = (fileHeader.magic == RIFFHeader::idLIST);

	ChunkReader::ChunkList<RIFFChunk> chunks = file.ReadChunks<RIFFChunk>(2);

	if(chunks.size() >= 4
		&& chunks[1].GetHeader().GetID() == RIFFChunk::iddata
		&& chunks[1].GetHeader().GetLength() % 2u != 0
		&& chunks[2].GetHeader().GetLength() == 0
		&& chunks[3].GetHeader().GetID() == RIFFChunk::id____)
	{
		// Houston, we have a problem: Old versions of (Open)MPT didn't write RIFF padding bytes. -_-
		// Luckily, the only RIFF chunk with an odd size those versions would ever write would be the "data" chunk
		// (which contains the sample data), and its size is only odd iff the sample has an odd length and is in
		// 8-Bit mono format. In all other cases, the sample size (and thus the chunk size) is even.

		// And we're even more lucky: The versions of (Open)MPT in question will always write a relatively small
		// (smaller than 256 bytes) "smpl" chunk after the "data" chunk. This means that after an unpadded sample,
		// we will always read "mpl?" (? being the length of the "smpl" chunk) as the next chunk magic. The first two
		// 32-Bit members of the "smpl" chunk are always zero in our case, so we are going to read a chunk length of 0
		// next and the next chunk magic, which will always consist of four zero bytes. Hooray! We just checked for those
		// four zero bytes and can be pretty confident that we should not have applied padding.
		file.Seek(sizeof(RIFFHeader));
		chunks = file.ReadChunks<RIFFChunk>(1);
	}

	// Read format chunk
	FileReader formatChunk = chunks.GetChunk(RIFFChunk::idfmt_);
	if(!formatChunk.ReadConvertEndianness(formatInfo))
	{
		return;
	}
	if(formatInfo.format == WAVFormatChunk::fmtExtensible)
	{
		WAVFormatChunkExtension extFormat;
		if(!formatChunk.ReadConvertEndianness(extFormat))
		{
			return;
		}
		formatInfo.format = extFormat.subFormat;
	}

	// Read sample data
	sampleData = chunks.GetChunk(RIFFChunk::iddata);

	if(!sampleData.IsValid())
	{
		// The old IMA ADPCM loader code looked for the "pcm " chunk instead of the "data" chunk...
		// Dunno why (Windows XP's audio recorder saves IMA ADPCM files with a "data" chunk), but we will just look for both.
		sampleData = chunks.GetChunk(RIFFChunk::idpcm_);
	}

	// "fact" chunk should contain sample length of compressed samples.
	sampleLength = chunks.GetChunk(RIFFChunk::idfact).ReadUint32LE();

	if((formatInfo.format != WAVFormatChunk::fmtIMA_ADPCM || sampleLength == 0) && GetSampleSize() != 0)
	{
		// Some samples have an incorrect blockAlign / sample size set (e.g. it's 8 in SQUARE.WAV while it should be 1), so let's better not trust this value.
		sampleLength = sampleData.GetLength() / GetSampleSize();
	}

	// Check for loop points, texts, etc...
	FindMetadataChunks(chunks);

	// DLS bank chunk
	wsmpChunk = chunks.GetChunk(RIFFChunk::idwsmp);
}


void WAVReader::FindMetadataChunks(ChunkReader::ChunkList<RIFFChunk> &chunks)
//---------------------------------------------------------------------------
{
	// Read sample loop points
	smplChunk = chunks.GetChunk(RIFFChunk::idsmpl);

	// Read text chunks
	ChunkReader listChunk = chunks.GetChunk(RIFFChunk::idLIST);
	if(listChunk.ReadMagic("INFO"))
	{
		infoChunk = listChunk.ReadChunks<RIFFChunk>(2);
	}

	// Read MPT sample information
	xtraChunk = chunks.GetChunk(RIFFChunk::idxtra);
}


void WAVReader::ApplySampleSettings(ModSample &sample, char (&sampleName)[MAX_SAMPLENAME])
//----------------------------------------------------------------------------------------
{
	// Read sample name
	FileReader textChunk = infoChunk.GetChunk(RIFFChunk::idINAM);
	if(textChunk.IsValid())
	{
		textChunk.ReadString<mpt::String::nullTerminated>(sampleName, textChunk.GetLength());
	}
	if(isDLS)
	{
		// DLS sample -> sample filename
		mpt::String::Copy(sample.filename, sampleName);
	}

	// Read software name
	const bool isOldMPT = infoChunk.GetChunk(RIFFChunk::idISFT).ReadMagic("Modplug Tracker");
	
	// Convert loops
	WAVSampleInfoChunk sampleInfo;
	smplChunk.Rewind();
	if(smplChunk.ReadConvertEndianness(sampleInfo))
	{
		WAVSampleLoop loopData;
		if(sampleInfo.numLoops > 1 && smplChunk.ReadConvertEndianness(loopData))
		{
			// First loop: Sustain loop
			loopData.ApplyToSample(sample.nSustainStart, sample.nSustainEnd, sample.nLength, sample.uFlags, CHN_SUSTAINLOOP, CHN_PINGPONGSUSTAIN, isOldMPT);
		}
		// First loop (if only one loop is present) or second loop (if more than one loop is present): Normal sample loop
		if(smplChunk.ReadConvertEndianness(loopData))
		{
			loopData.ApplyToSample(sample.nLoopStart, sample.nLoopEnd, sample.nLength, sample.uFlags, CHN_LOOP, CHN_PINGPONGLOOP, isOldMPT);
		}
		sample.SanitizeLoops();
	}

	// Read MPT extra info
	WAVExtraChunk mptInfo;
	xtraChunk.Rewind();
	if(xtraChunk.ReadConvertEndianness(mptInfo))
	{
		if(mptInfo.flags & WAVExtraChunk::setPanning) sample.uFlags.set(CHN_PANNING);

		sample.nPan = std::min(mptInfo.defaultPan, uint16(256));
		sample.nVolume = std::min(mptInfo.defaultVolume, uint16(256));
		sample.nGlobalVol = std::min(mptInfo.globalVolume, uint16(64));
		sample.nVibType = mptInfo.vibratoType;
		sample.nVibSweep = mptInfo.vibratoSweep;
		sample.nVibDepth = mptInfo.vibratoDepth;
		sample.nVibRate = mptInfo.vibratoRate;

		if(xtraChunk.CanRead(MAX_SAMPLENAME))
		{
			// Name present (clipboard only)
			xtraChunk.ReadString<mpt::String::nullTerminated>(sampleName, MAX_SAMPLENAME);
			xtraChunk.ReadString<mpt::String::nullTerminated>(sample.filename, xtraChunk.BytesLeft());
		}
	}
}


// Apply WAV loop information to a mod sample.
void WAVSampleLoop::ApplyToSample(SmpLength &start, SmpLength &end, SmpLength sampleLength, FlagSet<ChannelFlags, uint16> &flags, ChannelFlags enableFlag, ChannelFlags bidiFlag, bool mptLoopFix) const
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
	if(loopEnd == 0)
	{
		// Some WAV files seem to have loops going from 0 to 0... We should ignore those.
		return;
	}
	start = std::min(static_cast<SmpLength>(loopStart), sampleLength);
	end = Clamp(static_cast<SmpLength>(loopEnd), start, sampleLength);
	if(!mptLoopFix && end < sampleLength)
	{
		// RIFF loop end points are inclusive - old versions of MPT didn't consider this.
		end++;
	}

	flags.set(enableFlag);
	if(loopType == loopBidi)
	{
		flags.set(bidiFlag);
	}
}


// Convert internal loop information into a WAV loop.
void WAVSampleLoop::ConvertToWAV(SmpLength start, SmpLength end, bool bidi)
//-------------------------------------------------------------------------
{
	identifier = 0;
	loopType = bidi ? loopBidi : loopForward;
	loopStart = start;
	// Loop ends are *inclusive* in the RIFF standard, while they're *exclusive* in OpenMPT.
	if(end > start)
	{
		loopEnd = end - 1;
	} else
	{
		loopEnd = start;
	}
	fraction = 0;
	playCount = 0;
}


///////////////////////////////////////////////////////////
// WAV Writing


// Output to file: Initialize with filename.
WAVWriter::WAVWriter(const char *filename) : f(nullptr), memory(nullptr), memSize(0)
//----------------------------------------------------------------------------------
{
	Open(filename);
}


// Output to clipboard: Initialize with pointer to memory and size of reserved memory.
WAVWriter::WAVWriter(void *mem, size_t size) : f(nullptr), memory(static_cast<uint8 *>(mem)), memSize(size)
//---------------------------------------------------------------------------------------------------------
{
	Init();
}


WAVWriter::~WAVWriter()
//---------------------
{
	Finalize();
}


// Open a file for writing.
void WAVWriter::Open(const char *filename)
//----------------------------------------
{
	f = fopen(filename, "w+b");
	Init();
}


// Reset all file variables.
void WAVWriter::Init()
//--------------------
{
	chunkStartPos = 0;
	position = 0;
	totalSize = 0;

	// Skip file header for now
	Seek(sizeof(RIFFHeader));
}


// Finalize the file by closing the last open chunk and updating the file header. Returns total size of file.
size_t WAVWriter::Finalize()
//--------------------------
{
	FinalizeChunk();

	RIFFHeader fileHeader;
	fileHeader.magic = RIFFHeader::idRIFF;
	fileHeader.length = static_cast<uint32>(totalSize - 8);
	fileHeader.type = RIFFHeader::idWAVE;
	fileHeader.ConvertEndianness();

	Seek(0);
	Write(fileHeader);

	if(f != nullptr)
	{
#ifdef _DEBUG
		fseek(f, 0, SEEK_END);
		size_t realSize = static_cast<size_t>(ftell(f));
		ASSERT(totalSize == realSize);
#endif
		fclose(f);
	}

	f = nullptr;
	memory = nullptr;

	return totalSize;
}


// Write a new chunk header to the file.
void WAVWriter::StartChunk(RIFFChunk::id_type id)
//-----------------------------------------------
{
	FinalizeChunk();

	chunkStartPos = position;
	chunkHeader.id = id;
	Skip(sizeof(chunkHeader));
}


// End current chunk by updating the chunk header and writing a padding byte if necessary.
void WAVWriter::FinalizeChunk()
//-----------------------------
{
	if(chunkStartPos != 0)
	{
		const size_t chunkSize = position - (chunkStartPos + sizeof(RIFFChunk));
		chunkHeader.length = chunkSize;
		chunkHeader.ConvertEndianness();

		size_t curPos = position;
		Seek(chunkStartPos);
		Write(chunkHeader);

		Seek(curPos);
		if((chunkSize % 2u) != 0)
		{
			// Write padding
			uint8 padding = 0;
			Write(padding);
		}

		chunkStartPos = 0;
	}
}


// Seek to a position in file.
void WAVWriter::Seek(size_t pos)
//------------------------------
{
	position = pos;
	totalSize = std::max(totalSize, position);

	if(f != nullptr)
	{
		fseek(f, position, SEEK_SET);
	}
}


// Write some data to the file.
void WAVWriter::Write(const void *data, size_t numBytes)
//------------------------------------------------------
{
	if(f != nullptr)
	{
		fwrite(data, numBytes, 1, f);
	} else if(memory != nullptr)
	{
		if(position <= memSize && numBytes <= memSize - position)
		{
			memcpy(memory + position, data, numBytes);
		} else
		{
			// Should never happen - did we calculate a wrong memory size?
			ASSERT(false);
		}
	}
	position += numBytes;
	totalSize = std::max(totalSize, position);
}


// Write the WAV format to the file.
void WAVWriter::WriteFormat(uint32 sampleRate, uint16 bitDepth, uint16 numChannels, WAVFormatChunk::SampleFormats encoding)
//-------------------------------------------------------------------------------------------------------------------------
{
	StartChunk(RIFFChunk::idfmt_);
	WAVFormatChunk wavFormat;

	bool extensible = (numChannels > 2);

	wavFormat.format = static_cast<uint16>(extensible ? WAVFormatChunk::fmtExtensible : encoding);
	wavFormat.numChannels = numChannels;
	wavFormat.sampleRate = sampleRate;
	wavFormat.blockAlign = (bitDepth * numChannels + 7) / 8;
	wavFormat.byteRate = wavFormat.sampleRate * wavFormat.blockAlign;
	wavFormat.bitsPerSample = bitDepth;

	wavFormat.ConvertEndianness();
	Write(wavFormat);

	if(extensible)
	{
		WAVFormatChunkExtension extFormat;
		extFormat.size = sizeof(WAVFormatChunkExtension) - sizeof(uint16);
		extFormat.validBitsPerSample = bitDepth;
		switch(numChannels)
		{
		case 1:
			extFormat.channelMask = 0x0004;	// FRONT_CENTER
			break;
		case 2:
			extFormat.channelMask = 0x0003;	// FRONT_LEFT | FRONT_RIGHT
			break;
		case 3:
			extFormat.channelMask = 0x0103;	// FRONT_LEFT | FRONT_RIGHT | BACK_CENTER
			break;
		case 4:
			extFormat.channelMask = 0x0033;	// FRONT_LEFT | FRONT_RIGHT | BACK_LEFT | BACK_RIGHT
			break;
		default:
			extFormat.channelMask = 0;
			break;
		}
		extFormat.subFormat = static_cast<uint16>(encoding);
		const uint8 guid[] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
		MemCopy<uint8[14]>(extFormat.guid, guid);

		extFormat.ConvertEndianness();
		Write(extFormat);
	}
}


// Write text tags to the file.
void WAVWriter::WriteMetatags(const Metatags &tags)
//-------------------------------------------------
{
	StartChunk(RIFFChunk::idLIST);
	const char info[] = { 'I', 'N', 'F', 'O' };
	WriteArray(info);

	for(Metatags::const_iterator iter = tags.begin(); iter != tags.end(); iter++)
	{
		const size_t length = iter->text.length() + 1;
		if(length == 1)
		{
			continue;
		}

		RIFFChunk chunk;
		chunk.id = static_cast<uint32>(iter->id);
		chunk.length = length;
		chunk.ConvertEndianness();
		Write(chunk);
		Write(iter->text.c_str(), length);

		if((length % 2u) != 0)
		{
			uint8 padding = 0;
			Write(padding);
		}
	}
}


// Write a sample loop information chunk to the file.
void WAVWriter::WriteLoopInformation(const ModSample &sample)
//-----------------------------------------------------------
{
	if(!sample.uFlags[CHN_LOOP | CHN_SUSTAINLOOP])
	{
		return;
	}

	StartChunk(RIFFChunk::idsmpl);
	WAVSampleInfoChunk info;

	uint32 sampleRate = sample.nC5Speed;
	if(sampleRate == 0)
	{
		sampleRate = ModSample::TransposeToFrequency(sample.RelativeTone, sample.nFineTune);
	}

	info.ConvertToWAV(sampleRate);

	// Set up loops
	WAVSampleLoop loops[2];
	if(sample.uFlags[CHN_SUSTAINLOOP])
	{
		loops[info.numLoops++].ConvertToWAV(sample.nSustainStart, sample.nSustainEnd, sample.uFlags[CHN_PINGPONGSUSTAIN]);
	}
	if(sample.uFlags[CHN_LOOP])
	{
		loops[info.numLoops++].ConvertToWAV(sample.nLoopStart, sample.nLoopEnd, sample.uFlags[CHN_PINGPONGLOOP]);
	}

	info.ConvertEndianness();
	Write(info);
	for(size_t i = 0; i < info.numLoops; i++)
	{
		loops[i].ConvertEndianness();
		Write(loops[i]);
	}
}


// Write MPT's sample information chunk to the file.
void WAVWriter::WriteExtraInformation(const ModSample &sample, MODTYPE modType, const char *sampleName)
//-----------------------------------------------------------------------------------------------------
{
	StartChunk(RIFFChunk::idxtra);
	WAVExtraChunk mptInfo;

	mptInfo.ConvertToWAV(sample, modType);
	mptInfo.ConvertEndianness();
	Write(mptInfo);

	if(sampleName != nullptr)
	{
		// Write sample name (clipboard only)
		char name[MAX_SAMPLENAME];
		mpt::String::Write<mpt::String::nullTerminated>(name, sampleName, MAX_SAMPLENAME);
		WriteArray(name);

		char filename[MAX_SAMPLEFILENAME];
		mpt::String::Write<mpt::String::nullTerminated>(filename, sample.filename);
		WriteArray(filename);
	}
}
