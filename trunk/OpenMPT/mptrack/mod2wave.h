/*
 * mod2wave.h
 * ----------
 * Purpose: Module to WAV conversion (dialog + conversion code).
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "StreamEncoder.h"
#include "Settings.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Direct To Disk Recording


struct StoredTags
{
	Setting<std::wstring> artist;
	Setting<std::wstring> album;
	Setting<std::wstring> trackno;
	Setting<std::wstring> year;
	Setting<std::wstring> url;

	Setting<std::wstring> genre;

	StoredTags(SettingsContainer &conf);

};


struct CWaveConvertSettings
{
	std::vector<EncoderFactoryBase*> EncoderFactories;
	std::vector<MPT_SHARED_PTR<Encoder::Settings> > EncoderSettings;

	Setting<std::string> EncoderName;
	std::size_t EncoderIndex;

	SampleFormat FinalSampleFormat;

	StoredTags storedTags;
	FileTags Tags;

	bool Normalize;
	bool SilencePlugBuffers;

	std::size_t FindEncoder(const std::string &name) const;
	void SelectEncoder(std::size_t index);
	EncoderFactoryBase *GetEncoderFactory() const;
	const Encoder::Traits *GetTraits() const;
	Encoder::Settings &GetEncoderSettings() const;
	CWaveConvertSettings(SettingsContainer &conf, const std::vector<EncoderFactoryBase*> &encFactories);
};

//================================
class CWaveConvert: public CDialog
//================================
{
public:
	CWaveConvertSettings m_Settings;
	const Encoder::Traits *encTraits;
	CSoundFile *m_pSndFile;
	uint64 m_dwFileLimit;
	DWORD m_dwSongLimit;
	bool m_bSelectPlay, m_bHighQuality, m_bGivePlugsIdleTime;
	ORDERINDEX m_nMinOrder, m_nMaxOrder, m_nNumOrders;
	int loopCount;

	CComboBox m_CbnFileType, m_CbnSampleRate, m_CbnChannels, m_CbnSampleFormat;
	CSpinButtonCtrl m_SpinLoopCount, m_SpinMinOrder, m_SpinMaxOrder;

	bool m_bChannelMode;		// Render by channel
	bool m_bInstrumentMode;	// Render by instrument

	CEdit m_EditTitle, m_EditAuthor, m_EditURL, m_EditAlbum, m_EditYear;
	CComboBox m_CbnGenre;
	CEdit m_EditGenre;

private:
	void OnShowEncoderInfo();
	void FillFileTypes();
	void FillSamplerates();
	void FillChannels();
	void FillFormats();
	void FillTags();

	void LoadTags();

	void SaveEncoderSettings();
	void SaveTags();

public:
	CWaveConvert(CWnd *parent, ORDERINDEX minOrder, ORDERINDEX maxOrder, ORDERINDEX numOrders, CSoundFile *sndfile, const std::vector<EncoderFactoryBase*> &encFactories);

public:
	void UpdateDialog();
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange *pDX);
	virtual void OnOK();
	afx_msg void OnCheck1();
	afx_msg void OnCheck2();
	afx_msg void OnCheckChannelMode();
	afx_msg void OnCheckInstrMode();
	afx_msg void OnFileTypeChanged();
	afx_msg void OnSamplerateChanged();
	afx_msg void OnChannelsChanged();
	afx_msg void OnFormatChanged();
	afx_msg void OnPlayerOptions(); //rewbs.resamplerConf
	DECLARE_MESSAGE_MAP()
};


//==================================
class CDoWaveConvert: public CDialog
//==================================
{
public:
	const CWaveConvertSettings &m_Settings;
	CSoundFile *m_pSndFile;
	const char *m_lpszFileName;
	DWORD m_dwFileLimit, m_dwSongLimit;
	UINT m_nMaxPatterns;
	bool m_bAbort, m_bGivePlugsIdleTime;

public:
	CDoWaveConvert(CSoundFile *sndfile, const char *fname, const CWaveConvertSettings &settings, CWnd *parent = NULL)
		: CDialog(IDD_PROGRESS, parent)
		, m_Settings(settings)
		{ m_pSndFile = sndfile; 
		  m_lpszFileName = fname; 
		  m_bAbort = false; 
		  m_dwFileLimit = m_dwSongLimit = 0; 
		  m_nMaxPatterns = 0; }
	BOOL OnInitDialog();
	void OnCancel() { m_bAbort = true; }
	afx_msg void OnButton1();
	DECLARE_MESSAGE_MAP()
};


