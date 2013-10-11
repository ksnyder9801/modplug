/*
 * Load_gdm.cpp
 * ------------
 * Purpose: GDM (BWSB Soundsystem) module loader
 * Notes  : This code is partly based on zilym's original code / specs (which are utterly wrong :P).
 *          Thanks to the MenTaLguY for gdm.txt and ajs for gdm2s3m and some hints.
 *
 *          Hint 1: Most (all?) of the unsupported features were not supported in 2GDM / BWSB either.
 *          Hint 2: Files will be played like their original formats would be played in MPT, so no
 *          BWSB quirks including crashes and freezes are supported. :-P
 * Authors: Johannes Schultz
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"

#pragma pack(push, 1)

// GDM File Header
struct GDMFileHeader
{
	// Header magic bytes
	enum HeaderMagic
	{
		magicGDM_ = 0xFE4D4447,
		magicGMFS = 0x53464D47,
	};

	uint32 magic;					// ID: 'GDM�'
	char   songTitle[32];			// Music's title
	char   songMusician[32];		// Name of music's composer
	char   dosEOF[3];				// 13, 10, 26
	uint32 magic2;					// ID: 'GMFS'
	uint8  formatMajorVer;			// Format major version
	uint8  formatMinorVer;			// Format minor version
	uint16 trackerID;				// Composing Tracker ID code (00 = 2GDM)
	uint8  trackerMajorVer;			// Tracker's major version
	uint8  trackerMinorVer;			// Tracker's minor version
	uint8  panMap[32];				// 0-Left to 15-Right, 255-N/U
	uint8  masterVol;				// Range: 0...64
	uint8  tempo;					// Initial music tempo (6)
	uint8  bpm;						// Initial music BPM (125)
	uint16 originalFormat;			// Original format ID:
		// 1-MOD, 2-MTM, 3-S3M, 4-669, 5-FAR, 6-ULT, 7-STM, 8-MED
		// (versions of 2GDM prior to v1.15 won't set this correctly)

	uint32 orderOffset;
	uint8  lastOrder;				// Number of orders in module - 1
	uint32 patternOffset;
	uint8  lastPattern;				// Number of patterns in module - 1
	uint32 sampleHeaderOffset;
	uint32 sampleDataOffset;
	uint8  lastSample;				// Number of samples in module - 1
	uint32 messageTextOffset;		// Offset of song message
	uint32 messageTextLength;
	uint32 scrollyScriptOffset;		// Offset of scrolly script (huh?)
	uint16 scrollyScriptLength;
	uint32 textGraphicOffset;		// Offset of text graphic (huh?)
	uint16 textGraphicLength;

	// Convert all multi-byte numeric values to current platform's endianness or vice versa.
	void ConvertEndianness()
	{
		SwapBytesLE(magic);
		SwapBytesLE(magic2);
		SwapBytesLE(trackerID);
		SwapBytesLE(originalFormat);
		SwapBytesLE(orderOffset);
		SwapBytesLE(patternOffset);
		SwapBytesLE(sampleHeaderOffset);
		SwapBytesLE(sampleDataOffset);
		SwapBytesLE(messageTextOffset);
		SwapBytesLE(messageTextLength);
		SwapBytesLE(messageTextOffset);
		SwapBytesLE(messageTextLength);
		SwapBytesLE(scrollyScriptOffset);
		SwapBytesLE(scrollyScriptLength);
		SwapBytesLE(textGraphicOffset);
		SwapBytesLE(textGraphicLength);
	}
};

// GDM Sample Header
struct GDMSampleHeader
{
	enum SampleFlags
	{
		smpLoop		= 0x01,
		smp16Bit	= 0x02,		// 16-Bit samples are not handled correctly by 2GDM (not implemented)
		smpVolume	= 0x04,
		smpPanning	= 0x08,
		smpLZW		= 0x10,		// LZW-compressed samples are not implemented in 2GDM
		smpStereo	= 0x20,		// Stereo samples are not handled correctly by 2GDM (not implemented)
	};

	char   name[32];		// sample's name
	char   fileName[12];	// sample's filename
	uint8  emsHandle;		// useless
	uint32 length;			// length in bytes
	uint32 loopBegin;		// loop start in samples
	uint32 loopEnd;			// loop end in samples
	uint8  flags;			// misc. flags
	uint16 c4Hertz;			// frequency
	uint8  volume;			// default volume
	uint8  panning;			// default pan

	// Convert all multi-byte numeric values to current platform's endianness or vice versa.
	void ConvertEndianness()
	{
		SwapBytesLE(length);
		SwapBytesLE(loopBegin);
		SwapBytesLE(loopEnd);
		SwapBytesLE(c4Hertz);
	}
};

#pragma pack(pop)

bool CSoundFile::ReadGDM(FileReader &file)
//----------------------------------------
{
	file.Rewind();
	GDMFileHeader fileHeader;
	if(!file.ReadConvertEndianness(fileHeader))
	{
		return false;
	}

	// Is it a valid GDM file?
	if(fileHeader.magic != GDMFileHeader::magicGDM_
		|| fileHeader.dosEOF[0] != 13 || fileHeader.dosEOF[1] != 10 || fileHeader.dosEOF[2] != 26
		|| fileHeader.magic2 != GDMFileHeader::magicGMFS
		|| fileHeader.formatMajorVer != 1 || fileHeader.formatMinorVer != 0)
	{
		return false;
	}

	// 1-MOD, 2-MTM, 3-S3M, 4-669, 5-FAR, 6-ULT, 7-STM, 8-MED
	static const MODTYPE gdmFormatOrigin[] =
	{
		MOD_TYPE_NONE, MOD_TYPE_MOD, MOD_TYPE_MTM, MOD_TYPE_S3M, MOD_TYPE_669, MOD_TYPE_FAR, MOD_TYPE_ULT, MOD_TYPE_STM, MOD_TYPE_MED
	};

	m_nType = gdmFormatOrigin[fileHeader.originalFormat % CountOf(gdmFormatOrigin)];
	if(m_nType == MOD_TYPE_NONE)
	{
		return false;
	}

	// Song name
	MemsetZero(m_szNames);
	StringFixer::ReadString<StringFixer::maybeNullTerminated>(m_szNames[0], fileHeader.songTitle);

	// Read channel pan map... 0...15 = channel panning, 16 = surround channel, 255 = channel does not exist
	m_nChannels = 32;
	for(CHANNELINDEX i = 0; i < 32; i++)
	{
		if(fileHeader.panMap[i] < 16)
		{
			ChnSettings[i].nPan = min((fileHeader.panMap[i] * 16) + 8, 256);
		}
		else if(fileHeader.panMap[i] == 16)
		{
			ChnSettings[i].nPan = 128;
			ChnSettings[i].dwFlags |= CHN_SURROUND;
		}
		else if(fileHeader.panMap[i] == 0xFF)
		{
			m_nChannels = i;
			break;
		}
	}

	m_nDefaultGlobalVolume = min(fileHeader.masterVol * 4, 256);
	m_nDefaultSpeed = fileHeader.tempo;
	m_nDefaultTempo = fileHeader.bpm;
	m_nRestartPos = 0; // Not supported in this format, so use the default value
	m_nSamplePreAmp = 48; // Dito
	m_nVSTiVolume = 48; // Dito

	// Read orders
	if(file.Seek(fileHeader.orderOffset))
	{
		Order.ReadAsByte(file, fileHeader.lastOrder + 1);
	}

	// Read samples
	if(!file.Seek(fileHeader.sampleHeaderOffset))
	{
		return false;
	}

	m_nSamples = fileHeader.lastSample + 1;

	// Sample headers
	for(SAMPLEINDEX smp = 1; smp <= m_nSamples; smp++)
	{
		GDMSampleHeader gdmSample;
		if(!file.ReadConvertEndianness(gdmSample))
		{
			break;
		}

		StringFixer::ReadString<StringFixer::maybeNullTerminated>(m_szNames[smp], gdmSample.name);
		StringFixer::ReadString<StringFixer::maybeNullTerminated>(Samples[smp].filename, gdmSample.fileName);

		Samples[smp].nC5Speed = gdmSample.c4Hertz;
		Samples[smp].nGlobalVol = 256;	// Not supported in this format

		Samples[smp].nLength = gdmSample.length; // in bytes

		// Sample format
		if(gdmSample.flags & GDMSampleHeader::smp16Bit)
		{
			Samples[smp].uFlags |= CHN_16BIT;
			Samples[smp].nLength /= 2;
		}

		Samples[smp].nLoopStart = min(gdmSample.loopBegin, Samples[smp].nLength);	// in samples
		Samples[smp].nLoopEnd = min(gdmSample.loopEnd - 1, Samples[smp].nLength);	// dito
		Samples[smp].FrequencyToTranspose();	// set transpose + finetune for mod files

		// Fix transpose + finetune for some rare cases where transpose is not C-5 (e.g. sample 4 in wander2.gdm)
		if(m_nType == MOD_TYPE_MOD)
		{
			while(Samples[smp].RelativeTone != 0)
			{
				if(Samples[smp].RelativeTone > 0)
				{
					Samples[smp].RelativeTone -= 1;
					Samples[smp].nFineTune += 128;
				}
				else
				{
					Samples[smp].RelativeTone += 1;
					Samples[smp].nFineTune -= 128;
				}
			}
		}

		Samples[smp].uFlags = 0;
		if(gdmSample.flags & GDMSampleHeader::smpLoop) Samples[smp].uFlags |= CHN_LOOP; // Loop sample

		if(gdmSample.flags & GDMSampleHeader::smpVolume)
		{
			// Default volume is used... 0...64, 255 = no default volume
			Samples[smp].nVolume = min(gdmSample.volume, 64) * 4;
		} else
		{
			Samples[smp].nVolume = 256;
		}

		if(gdmSample.flags & GDMSampleHeader::smpPanning)
		{
			// Default panning is used
			Samples[smp].uFlags |= CHN_PANNING;
			// 0...15, 16 = surround (not supported), 255 = no default panning
			Samples[smp].nPan = (gdmSample.panning > 15) ? 128 : min((gdmSample.panning * 16) + 8, 256);
		} else
		{
			Samples[smp].nPan = 128;
		}
	}

	// Read sample data
	if(file.Seek(fileHeader.sampleDataOffset))
	{
		for(SAMPLEINDEX smp = 1; smp <= GetNumSamples(); smp++)
		{
			SampleIO(
				(Samples[smp].uFlags & CHN_16BIT) ? SampleIO::_16bit : SampleIO::_8bit,
				SampleIO::mono,
				SampleIO::littleEndian,
				SampleIO::unsignedPCM)
				.ReadSample(Samples[smp], file);
		}
	}

	// Read patterns
	Patterns.ResizeArray(max(MAX_PATTERNS, fileHeader.lastPattern + 1));

	const CModSpecifications &modSpecs = GetModSpecifications(GetBestSaveFormat());

	// We'll start at position patternsOffset and decode all patterns
	file.Seek(fileHeader.patternOffset);
	for(PATTERNINDEX pat = 0; pat <= fileHeader.lastPattern; pat++)
	{
		if(!file.CanRead(2))
		{
			break;
		}

		// Read pattern length *including* the two "length" bytes
		uint16 patternLength = file.ReadUint16LE();

		if(patternLength <= 2)
		{
			// Huh, no pattern data present?
			continue;
		}
		FileReader chunk = file.GetChunk(patternLength - 2);

		if(!chunk.IsValid() || Patterns.Insert(pat, 64))
		{
			break;
		}

		enum
		{
			rowDone		= 0,		// Advance to next row
			channelMask	= 0x1F,		// Mask for retrieving channel information
			noteFlag	= 0x20,		// Note / instrument information present
			effectFlag	= 0x40,		// Effect information present
			effectMask	= 0x1F,		// Mask for retrieving effect command
			effectDone	= 0x20,		// Last effect in this channel
		};

		for(ROWINDEX row = 0; row < 64; row++)
		{
			PatternRow rowBase = Patterns[pat].GetRow(row);

			uint8 channelByte;
			// If channel byte is zero, advance to next row.
			while((channelByte = chunk.ReadUint8()) != rowDone)
			{
				CHANNELINDEX channel = channelByte & channelMask;
				if(channel >= m_nChannels) break; // Better safe than sorry!

				ModCommand &m = rowBase[channel];

				if(channelByte & noteFlag)
				{
					// Note and sample follows
					uint8 noteByte = chunk.ReadUint8();
					uint8 noteSample = chunk.ReadUint8();

					if(noteByte)
					{
						noteByte = (noteByte & 0x7F) - 1; // This format doesn't have note cuts
						if(noteByte < 0xF0) noteByte = (noteByte & 0x0F) + 12 * (noteByte >> 4) + 13;
						m.note = noteByte;
					}
					m.instr = noteSample;
				}

				if(channelByte & effectFlag)
				{
					// Effect(s) follow(s)
					m.command = CMD_NONE;
					m.volcmd = VOLCMD_NONE;

					while(chunk.BytesLeft())
					{
						uint8 effByte = chunk.ReadUint8();
						uint8 paramByte = chunk.ReadUint8();

						// We may want to restore the old command in some cases.
						const ModCommand oldCmd = m;
						m.command = effByte & effectMask;
						m.param = paramByte;

						// Effect translation LUT
						static const uint8 gdmEffTrans[] =
						{
							CMD_NONE, CMD_PORTAMENTOUP, CMD_PORTAMENTODOWN, CMD_TONEPORTAMENTO,
							CMD_VIBRATO, CMD_TONEPORTAVOL, CMD_VIBRATOVOL, CMD_TREMOLO,
							CMD_TREMOR, CMD_OFFSET, CMD_VOLUMESLIDE, CMD_POSITIONJUMP,
							CMD_VOLUME, CMD_PATTERNBREAK, CMD_MODCMDEX, CMD_SPEED,
							CMD_ARPEGGIO, CMD_NONE /* set internal flag */, CMD_RETRIG, CMD_GLOBALVOLUME,
							CMD_FINEVIBRATO, CMD_NONE, CMD_NONE, CMD_NONE,
							CMD_NONE, CMD_NONE, CMD_NONE, CMD_NONE,
							CMD_NONE, CMD_NONE, CMD_S3MCMDEX, CMD_TEMPO,
						};
						STATIC_ASSERT(CountOf(gdmEffTrans) == 0x20);

						// Translate effect
						if(m.command < CountOf(gdmEffTrans))
						{
							m.command = gdmEffTrans[m.command];
						} else
						{
							m.command = CMD_NONE;
						}

						// Fix some effects
						switch(m.command)
						{
						case CMD_PORTAMENTOUP:
							if(m.param >= 0xE0)
								m.param = 0xDF;
							break;

						case CMD_PORTAMENTODOWN:
							if(m.param >= 0xE0)
								m.param = 0xDF;
							break;

						case CMD_TONEPORTAVOL:
							if(m.param & 0xF0)
								m.param &= 0xF0;
							break;

						case CMD_VIBRATOVOL:
							if(m.param & 0xF0)
								m.param &= 0xF0;
							break;

						case CMD_VOLUME:
							m.param = min(m.param, 64);
							if(modSpecs.HasVolCommand(VOLCMD_VOLUME))
							{
								m.volcmd = VOLCMD_VOLUME;
								m.vol = m.param;
								// Don't destroy old command, if there was one.
								m.command = oldCmd.command;
								m.param = oldCmd.param;
							}
							break;

						case CMD_MODCMDEX:
							if(!modSpecs.HasVolCommand(CMD_MODCMDEX))
							{
								m.ExtendedMODtoS3MEffect();
							}
							break;

						case CMD_RETRIG:
							if(!modSpecs.HasCommand(CMD_RETRIG) && modSpecs.HasCommand(CMD_MODCMDEX))
							{
								// Retrig in "MOD style"
								m.command = CMD_MODCMDEX;
								m.param = 0x90 | (m.param & 0x0F);
							}
							break;

						case CMD_S3MCMDEX:
							// Some really special commands
							switch(m.param >> 4)
							{
							case 0x0:
								switch(m.param & 0x0F)
								{
								case 0x0:	// Surround Off
									m.command = CMD_S3MCMDEX;
									m.param = 0x90;
									break;
								case 0x1:	// Surround On
									m.command = CMD_PANNING8;
									m.param = 0xA4;
									break;
								case 0x2:	// Set normal loop - not implemented in BWSB or 2GDM.
								case 0x3:	// Set bidi loop - dito
									m.command = CMD_NONE;
									break;
								case 0x4:	// Play sample forwards
									m.command = CMD_S3MCMDEX;
									m.param = 0x9E;
									break;
								case 0x5:	// Play sample backwards
									m.command = CMD_S3MCMDEX;
									m.param = 0x9F;
									break;
								case 0x6:	// Monaural sample - also not implemented.
								case 0x7:	// Stereo sample - dito
								case 0x8:	// Stop sample on end - dito
								case 0x9:	// Loop sample on end - dito
								default:
									m.command = CMD_NONE;
									break;
								}
								break;

							case 0x8:		// 4-Bit Panning
								if(!modSpecs.HasCommand(CMD_S3MCMDEX))
								{
									m.command = CMD_MODCMDEX;
								}
								break;

							case 0xD:	// Adjust frequency (increment in hz) - also not implemented.
							default:
								m.command = CMD_NONE;
								break;
							}
							break;

						case 0x1F:
							m.command = CMD_TEMPO;
							break;
						}

						// Move pannings to volume column - should never happen
						if(m.command == CMD_S3MCMDEX && ((m.param >> 4) == 0x8) && m.volcmd == VOLCMD_NONE)
						{
							m.volcmd = VOLCMD_PANNING;
							m.vol = ((m.param & 0x0F) * 64 + 8) / 15;
							m.command = oldCmd.command;
							m.param = oldCmd.param;
						}

						if(!(effByte & effectDone)) break; // no other effect follows
					}

				}

			}
		}
	}

	// Read song comments
	if(fileHeader.messageTextLength > 0 && file.Seek(fileHeader.messageTextOffset))
	{
		ReadMessage(file, fileHeader.messageTextLength, leAutodetect);
	}

	return true;

}