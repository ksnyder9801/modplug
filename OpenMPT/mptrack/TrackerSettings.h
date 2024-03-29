/*
 * TrackerSettings.h
 * -----------------
 * Purpose: Header file for application setting handling.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "../soundlib/MixerSettings.h"
#include "../soundlib/Resampler.h"
#include "../soundlib/SampleFormat.h"
#include "../sounddsp/EQ.h"
#include "../sounddsp/DSP.h"
#include "../sounddsp/Reverb.h"
#include "../sounddev/SoundDevice.h"
#include "StreamEncoder.h"
#include "../common/version.h"
#include "Settings.h"

#include <bitset>


OPENMPT_NAMESPACE_BEGIN


/////////////////////////////////////////////////////////////////////////
// Default directories

enum Directory
{
	DIR_MODS = 0,
	DIR_SAMPLES,
	DIR_INSTRUMENTS,
	DIR_PLUGINS,
	DIR_PLUGINPRESETS,
	DIR_EXPORT,
	DIR_TUNING,
	DIR_TEMPLATE_FILES_USER,
	NUM_DIRS
};


// User-defined colors
enum
{
	MODCOLOR_BACKNORMAL = 0,
	MODCOLOR_TEXTNORMAL,
	MODCOLOR_BACKCURROW,
	MODCOLOR_TEXTCURROW,
	MODCOLOR_BACKSELECTED,
	MODCOLOR_TEXTSELECTED,
	MODCOLOR_SAMPLE,
	MODCOLOR_BACKPLAYCURSOR,
	MODCOLOR_TEXTPLAYCURSOR,
	MODCOLOR_BACKHILIGHT,
	MODCOLOR_NOTE,
	MODCOLOR_INSTRUMENT,
	MODCOLOR_VOLUME,
	MODCOLOR_PANNING,
	MODCOLOR_PITCH,
	MODCOLOR_GLOBALS,
	MODCOLOR_ENVELOPES,
	MODCOLOR_VUMETER_LO,
	MODCOLOR_VUMETER_MED,
	MODCOLOR_VUMETER_HI,
	MODCOLOR_SEPSHADOW,
	MODCOLOR_SEPFACE,
	MODCOLOR_SEPHILITE,
	MODCOLOR_BLENDCOLOR,
	MODCOLOR_DODGY_COMMANDS,
	MAX_MODCOLORS,
	// Internal color codes (not saved to color preset files)
	MODCOLOR_2NDHIGHLIGHT,
	MODCOLOR_DEFAULTVOLUME,
	MAX_MODPALETTECOLORS
};


// Pattern Setup (contains also non-pattern related settings)
// Feel free to replace the deprecated flags by new flags, but be sure to
// update TrackerSettings::TrackerSettings() as well.
#define PATTERN_PLAYNEWNOTE			0x01		// play new notes while recording
#define PATTERN_LARGECOMMENTS		0x02		// use large font in comments
#define PATTERN_STDHIGHLIGHT		0x04		// enable primary highlight (measures)
#define PATTERN_SMALLFONT			0x08		// use small font in pattern editor
#define PATTERN_CENTERROW			0x10		// always center active row
#define PATTERN_WRAP				0x20		// wrap around cursor in editor
#define PATTERN_EFFECTHILIGHT		0x40		// effect syntax highlighting
#define PATTERN_HEXDISPLAY			0x80		// display row number in hex
#define PATTERN_FLATBUTTONS			0x100		// flat toolbar buttons
#define PATTERN_CREATEBACKUP		0x200		// create .bak files when saving
#define PATTERN_SINGLEEXPAND		0x400		// single click to expand tree
#define PATTERN_PLAYEDITROW			0x800		// play all notes on the current row while entering notes
#define PATTERN_NOEXTRALOUD			0x1000		// no loud samples in sample editor
#define PATTERN_DRAGNDROPEDIT		0x2000		// enable drag and drop editing
#define PATTERN_2NDHIGHLIGHT		0x4000		// activate secondary highlight (beats)
#define PATTERN_MUTECHNMODE			0x8000		// ignore muted channels
#define PATTERN_SHOWPREVIOUS		0x10000		// show prev/next patterns
#define PATTERN_CONTSCROLL			0x20000		// continous pattern scrolling
#define PATTERN_KBDNOTEOFF			0x40000		// Record note-off events
#define PATTERN_FOLLOWSONGOFF		0x80000		// follow song off by default
#define PATTERN_MIDIRECORD			0x100000	// MIDI Record on by default
#define PATTERN_NOCLOSEDIALOG		0x200000	// Don't use OpenMPT's custom close dialog with a list of saved files when closing the main window
#define PATTERN_DBLCLICKSELECT		0x400000	// Double-clicking pattern selects whole channel
#define PATTERN_OLDCTXMENUSTYLE		0x800000	// Hide pattern context menu entries instead of greying them out.
#define PATTERN_SYNCMUTE			0x1000000	// maintain sample sync on mute
#define PATTERN_AUTODELAY			0x2000000	// automatically insert delay commands in pattern when entering notes
#define PATTERN_NOTEFADE			0x4000000	// alt. note fade behaviour when entering notes
#define PATTERN_OVERFLOWPASTE		0x8000000	// continue paste in the next pattern instead of cutting off
#define PATTERN_SHOWDEFAULTVOLUME	0x10000000	// if there is no volume command next to note+instr, display the sample's default volume.
#define PATTERN_RESETCHANNELS		0x20000000	// reset channels when looping
#define PATTERN_LIVEUPDATETREE		0x40000000	// update active sample / instr icons in treeview
#define PATTERN_SYNCSAMPLEPOS		0x80000000	// sync sample positions when seeking


// Midi Setup
#define MIDISETUP_RECORDVELOCITY			0x01	// Record MIDI velocity
#define MIDISETUP_TRANSPOSEKEYBOARD			0x02	// Apply transpose value to MIDI Notes
#define MIDISETUP_MIDITOPLUG				0x04	// Pass MIDI messages to plugins
#define MIDISETUP_MIDIVOL_TO_NOTEVOL		0x08	// Combine MIDI volume to note velocity
#define MIDISETUP_RECORDNOTEOFF				0x10	// Record MIDI Note Off to pattern
#define MIDISETUP_RESPONDTOPLAYCONTROLMSGS	0x20	// Respond to Restart/Continue/Stop MIDI commands
#define MIDISETUP_MIDIMACROCONTROL			0x80	// Record MIDI controller changes a MIDI macro changes in pattern
#define MIDISETUP_PLAYPATTERNONMIDIIN		0x100	// Play pattern if MIDI Note is received and playback is paused


// EQ
#ifdef NEEDS_PRAGMA_PACK
#pragma pack(push, 1)
#endif
struct PACKED EQPreset
{
	char szName[12];
	UINT Gains[MAX_EQ_BANDS];
	UINT Freqs[MAX_EQ_BANDS];
};

static const EQPreset FlatEQPreset = { "Flat", {16,16,16,16,16,16}, { 125, 300, 600, 1250, 4000, 8000 } };

#ifdef NEEDS_PRAGMA_PACK
#pragma pack(pop)
#endif
STATIC_ASSERT(sizeof(EQPreset) == 60);

template<> inline SettingValue ToSettingValue(const EQPreset &val)
{
	return SettingValue(EncodeBinarySetting<EQPreset>(val), "EQPreset");
}
template<> inline EQPreset FromSettingValue(const SettingValue &val)
{
	ASSERT(val.GetTypeTag() == "EQPreset");
	return DecodeBinarySetting<EQPreset>(val.as<std::vector<char> >());
}



// Chords
struct MPTChord
{
	enum
	{
		notesPerChord = 4,
		relativeMode = 0x3F,
	};

	uint8 key;			// Base note
	uint8 notes[3];		// Additional chord notes
};

typedef MPTChord MPTChords[3 * 12];	// 3 octaves

// MIDI recording
enum RecordAftertouchOptions
{
	atDoNotRecord = 0,
	atRecordAsVolume,
	atRecordAsMacro,
};

// Sample editor preview behaviour
enum SampleEditorKeyBehaviour
{
	seNoteOffOnNewKey,
	seNoteOffOnKeyUp,
	seNoteOffOnKeyRestrike,
};

enum SampleEditorDefaultFormat
{
	dfFLAC,
	dfWAV,
	dfRAW,
};


class SampleUndoBufferSize
{
protected:
	size_t sizeByte;
	int32 sizePercent;

	void CalculateSize();

public:
	enum
	{
		defaultSize = 10,	// In percent
	};

	SampleUndoBufferSize(int32 percent = defaultSize) : sizePercent(percent) { CalculateSize(); }
	void Set(int32 percent) { sizePercent = percent; CalculateSize(); }

	int32 GetSizeInPercent() const { return sizePercent; }
	size_t GetSizeInBytes() const { return sizeByte; }
};

template<> inline SettingValue ToSettingValue(const SampleUndoBufferSize &val) { return SettingValue(val.GetSizeInPercent()); }
template<> inline SampleUndoBufferSize FromSettingValue(const SettingValue &val) { return SampleUndoBufferSize(val.as<int32>()); }


std::string IgnoredCCsToString(const std::bitset<128> &midiIgnoreCCs);
std::bitset<128> StringToIgnoredCCs(const std::string &in);

std::string SettingsModTypeToString(MODTYPE modtype);
MODTYPE SettingsStringToModType(const std::string &str);


template<> inline SettingValue ToSettingValue(const RecordAftertouchOptions &val) { return SettingValue(int32(val)); }
template<> inline RecordAftertouchOptions FromSettingValue(const SettingValue &val) { return RecordAftertouchOptions(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const SampleEditorKeyBehaviour &val) { return SettingValue(int32(val)); }
template<> inline SampleEditorKeyBehaviour FromSettingValue(const SettingValue &val) { return SampleEditorKeyBehaviour(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const MODTYPE &val) { return SettingValue(SettingsModTypeToString(val), "MODTYPE"); }
template<> inline MODTYPE FromSettingValue(const SettingValue &val) { ASSERT(val.GetTypeTag() == "MODTYPE"); return SettingsStringToModType(val.as<std::string>()); }

template<> inline SettingValue ToSettingValue(const PLUGVOLUMEHANDLING &val)
{
	return SettingValue(int32(val), "PLUGVOLUMEHANDLING");
}
template<> inline PLUGVOLUMEHANDLING FromSettingValue(const SettingValue &val)
{
	ASSERT(val.GetTypeTag() == "PLUGVOLUMEHANDLING");
	if((uint32)val.as<int32>() > PLUGIN_VOLUMEHANDLING_MAX)
	{
		return PLUGIN_VOLUMEHANDLING_IGNORE;
	}
	return static_cast<PLUGVOLUMEHANDLING>(val.as<int32>());
}

template<> inline SettingValue ToSettingValue(const std::vector<uint32> &val) { return mpt::String::Combine(val); }
template<> inline std::vector<uint32> FromSettingValue(const SettingValue &val) { return mpt::String::Split<uint32>(val); }

template<> inline SettingValue ToSettingValue(const SoundDevice::ID &val) { return SettingValue(int32(val.GetIdRaw())); }
template<> inline SoundDevice::ID FromSettingValue(const SettingValue &val) { return SoundDevice::ID::FromIdRaw(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const SampleFormat &val) { return SettingValue(int32(val.value)); }
template<> inline SampleFormat FromSettingValue(const SettingValue &val) { return SampleFormatEnum(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const SoundDevice::ChannelMapping &val) { return SettingValue(val.ToString(), "ChannelMapping"); }
template<> inline SoundDevice::ChannelMapping FromSettingValue(const SettingValue &val) { ASSERT(val.GetTypeTag() == "ChannelMapping"); return SoundDevice::ChannelMapping::FromString(val.as<std::string>()); }

template<> inline SettingValue ToSettingValue(const ResamplingMode &val) { return SettingValue(int32(val)); }
template<> inline ResamplingMode FromSettingValue(const SettingValue &val) { return ResamplingMode(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const std::bitset<128> &val)
{
	return SettingValue(IgnoredCCsToString(val), "IgnoredCCs");
}
template<> inline std::bitset<128> FromSettingValue(const SettingValue &val)
{
	ASSERT(val.GetTypeTag() == "IgnoredCCs");
	return StringToIgnoredCCs(val.as<std::string>());
}

template<> inline SettingValue ToSettingValue(const SampleEditorDefaultFormat &val)
{
	const char *format;
	switch(val)
	{
	case dfWAV:
		format = "wav";
		break;
	case dfFLAC:
	default:
		format = "flac";
		break;
	case dfRAW:
		format = "raw";
	}
	return SettingValue(format);
}
template<> inline SampleEditorDefaultFormat FromSettingValue(const SettingValue &val)
{
	std::string format = val.as<std::string>();
	for(std::string::iterator c = format.begin(); c != format.end(); c++) *c = std::string::value_type(::tolower(*c));
	if(format == "wav")
		return dfWAV;
	if(format == "raw")
		return dfRAW;
	else // if(format == "flac")
		return dfFLAC;
}

template<> inline SettingValue ToSettingValue(const SoundDevice::StopMode &val)
{
	return SettingValue(static_cast<int32>(val));
}
template<> inline SoundDevice::StopMode FromSettingValue(const SettingValue &val)
{
	return static_cast<SoundDevice::StopMode>(static_cast<int32>(val));
}


//===================
class TrackerSettings
//===================
{

private:
	SettingsContainer &conf;

public:

	// Version

	Setting<std::string> IniVersion;
	const MptVersion::VersionNum gcsPreviousVersion;
	Setting<std::wstring> gcsInstallGUID;

	// Display

	Setting<bool> m_ShowSplashScreen;
	Setting<bool> gbMdiMaximize;
	Setting<LONG> glTreeSplitRatio;
	Setting<LONG> glTreeWindowWidth;
	Setting<LONG> glGeneralWindowHeight;
	Setting<LONG> glPatternWindowHeight;
	Setting<LONG> glSampleWindowHeight;
	Setting<LONG> glInstrumentWindowHeight;
	Setting<LONG> glCommentsWindowHeight;
	Setting<LONG> glGraphWindowHeight;

	Setting<int32> gnPlugWindowX;
	Setting<int32> gnPlugWindowY;
	Setting<int32> gnPlugWindowWidth;
	Setting<int32> gnPlugWindowHeight;
	Setting<int32> gnPlugWindowLast;	// Last selected plugin ID

	Setting<uint32> gnMsgBoxVisiblityFlags;
	Setting<uint32> GUIUpdateInterval;
	CachedSetting<uint32> VuMeterUpdateInterval;

	// Misc

	Setting<bool> ShowSettingsOnNewVersion;
	Setting<bool> gbShowHackControls;
	Setting<MODTYPE> defaultModType;
	Setting<PLUGVOLUMEHANDLING> DefaultPlugVolumeHandling;
	Setting<bool> autoApplySmoothFT2Ramping;
	Setting<uint32> MiscITCompressionStereo; // Mask: bit0: IT, bit1: Compat IT, bit2: MPTM
	Setting<uint32> MiscITCompressionMono;   // Mask: bit0: IT, bit1: Compat IT, bit2: MPTM

	// Sound Settings
	
	Setting<std::vector<uint32> > m_SoundSampleRates;
	Setting<bool> m_MorePortaudio;
	Setting<bool> m_SoundSettingsOpenDeviceAtStartup;
	Setting<SoundDevice::StopMode> m_SoundSettingsStopMode;

	bool m_SoundDeviceSettingsUseOldDefaults;
	SoundDevice::ID m_SoundDeviceID_DEPRECATED;
	SoundDevice::Settings m_SoundDeviceSettingsDefaults;
	SoundDevice::Settings GetSoundDeviceSettingsDefaults() const;

	Setting<std::wstring> m_SoundDeviceIdentifier;
	Setting<bool> m_SoundDevicePreferSameTypeIfDeviceUnavailable;
	SoundDevice::Identifier GetSoundDeviceIdentifier() const;
	void SetSoundDeviceIdentifier(const SoundDevice::Identifier &identifier);
	SoundDevice::Settings GetSoundDeviceSettings(const SoundDevice::Identifier &device) const;
	void SetSoundDeviceSettings(const SoundDevice::Identifier &device, const SoundDevice::Settings &settings);

	Setting<uint32> MixerMaxChannels;
	Setting<uint32> MixerDSPMask;
	Setting<uint32> MixerFlags;
	Setting<uint32> MixerSamplerate;
	Setting<uint32> MixerOutputChannels;
	Setting<uint32> MixerPreAmp;
	Setting<uint32> MixerStereoSeparation;
	Setting<uint32> MixerVolumeRampUpMicroseconds;
	Setting<uint32> MixerVolumeRampDownMicroseconds;
	MixerSettings GetMixerSettings() const;
	void SetMixerSettings(const MixerSettings &settings);

	Setting<ResamplingMode> ResamplerMode;
	Setting<uint8> ResamplerSubMode;
	Setting<int32> ResamplerCutoffPercent;
	CResamplerSettings GetResamplerSettings() const;
	void SetResamplerSettings(const CResamplerSettings &settings);

	// MIDI Settings

	Setting<LONG> m_nMidiDevice;
	// FIXME: MIDI recording is currently done in its own callback/thread and
	// accesses settings framework from in there. Work-around the ASSERTs for
	// now by using cached settings.
	CachedSetting<uint32> m_dwMidiSetup;
	CachedSetting<RecordAftertouchOptions> aftertouchBehaviour;
	CachedSetting<uint16> midiVelocityAmp;
	CachedSetting<std::bitset<128> > midiIgnoreCCs;

	Setting<int32> midiImportSpeed;
	Setting<int32> midiImportPatternLen;

	// Pattern Editor

	Setting<bool> gbLoopSong;
	CachedSetting<UINT> gnPatternSpacing;
	CachedSetting<bool> gbPatternVUMeters;
	CachedSetting<bool> gbPatternPluginNames;
	CachedSetting<bool> gbPatternRecord;
	CachedSetting<uint32> m_dwPatternSetup;
	CachedSetting<uint32> m_nRowHighlightMeasures; // primary (measures) and secondary (beats) highlight
	CachedSetting<uint32> m_nRowHighlightBeats;	// primary (measures) and secondary (beats) highlight
	CachedSetting<ROWINDEX> recordQuantizeRows;
	CachedSetting<UINT> gnAutoChordWaitTime;
	CachedSetting<int32> orderlistMargins;
	CachedSetting<int32> rowDisplayOffset;

	// Sample Editor

	Setting<SampleUndoBufferSize> m_SampleUndoBufferSize;
	Setting<SampleEditorKeyBehaviour> sampleEditorKeyBehaviour;
	Setting<SampleEditorDefaultFormat> m_defaultSampleFormat;
	Setting<uint32> m_nFinetuneStep;	// Increment finetune by x when using spin control. Default = 25
	Setting<int32> m_FLACCompressionLevel;	// FLAC compression level for saving (0...8)
	Setting<bool> compressITI;
	Setting<bool> m_MayNormalizeSamplesOnLoad;
	Setting<bool> previewInFileDialogs;

	// Export

	Setting<bool> ExportDefaultToSoundcardSamplerate;
	StreamEncoderSettings ExportStreamEncoderSettings;

	// Effects

#ifndef NO_REVERB
	CReverbSettings m_ReverbSettings;
#endif
#ifndef NO_DSP
	CDSPSettings m_DSPSettings;
#endif
#ifndef NO_EQ
	EQPreset m_EqSettings;
	EQPreset m_EqUserPresets[4];
#endif

	// Display (Colors)

	COLORREF rgbCustomColors[MAX_MODCOLORS];

	// Paths

	mpt::PathString m_szKbdFile;

	// Default template

	Setting<mpt::PathString> defaultTemplateFile;

	Setting<uint32> mruListLength;
	std::vector<mpt::PathString> mruFiles;

	// Chords

	MPTChords Chords;

	// Plugins

	Setting<bool> bridgeAllPlugins;

	// Debug

	Setting<bool> DebugTraceEnable;
	Setting<uint32> DebugTraceSize;

public:

	TrackerSettings(SettingsContainer &conf);

	void SaveSettings();

	static void GetDefaultColourScheme(COLORREF (&colours)[MAX_MODCOLORS]);

	std::vector<uint32> GetSampleRates() const;

	static MPTChords &GetChords() { return Instance().Chords; }

	// Get settings object singleton
	static TrackerSettings &Instance();

protected:

	static std::vector<uint32> GetDefaultSampleRates();

	void FixupEQ(EQPreset *pEqSettings);

	void LoadChords(MPTChords &chords);
	void SaveChords(MPTChords &chords);

};


//======================
class TrackerDirectories
//======================
{
	friend class TrackerSettings;
private:

	// Directory Arrays (default dir + last dir)
	mpt::PathString m_szDefaultDirectory[NUM_DIRS];
	mpt::PathString m_szWorkingDirectory[NUM_DIRS];
	// Directory to INI setting translation
	static const TCHAR *m_szDirectoryToSettingsName[NUM_DIRS];

public:

	TrackerDirectories();
	~TrackerDirectories();

	// access to default + working directories
	void SetWorkingDirectory(const mpt::PathString &filenameFrom, Directory dir, bool stripFilename = false);
	mpt::PathString GetWorkingDirectory(Directory dir) const;
	void SetDefaultDirectory(const mpt::PathString &filenameFrom, Directory dir, bool stripFilename = false);
	mpt::PathString GetDefaultDirectory(Directory dir) const;

	static TrackerDirectories &Instance();

protected:

	void SetDirectory(const mpt::PathString &szFilenameFrom, Directory dir, mpt::PathString (&pDirs)[NUM_DIRS], bool bStripFilename);

};


OPENMPT_NAMESPACE_END
