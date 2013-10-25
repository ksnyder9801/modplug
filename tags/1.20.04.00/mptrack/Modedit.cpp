/*
 * ModEdit.cpp
 * -----------
 * Purpose: Song (pattern, samples, instruments) editing functions
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "mptrack.h"
#include "mainfrm.h"
#include "moddoc.h"
#include "dlg_misc.h"
#include "dlsbank.h"
#include "modsmp_ctrl.h"
#include "../common/misc_util.h"

#pragma warning(disable:4244) //"conversion from 'type1' to 'type2', possible loss of data"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// Change the number of channels.
// Return true on success.
bool CModDoc::ChangeNumChannels(CHANNELINDEX nNewChannels, const bool showCancelInRemoveDlg)
//------------------------------------------------------------------------------------------
{
	const CHANNELINDEX maxChans = m_SndFile.GetModSpecifications().channelsMax;

	if (nNewChannels > maxChans)
	{
		CString error;
		error.Format("Error: Max number of channels for this file type is %d", maxChans);
		Reporting::Warning(error);
		return false;
	}

	if (nNewChannels == GetNumChannels()) return false;

	if (nNewChannels < GetNumChannels())
	{
		// Remove channels
		UINT nChnToRemove = 0;
		CHANNELINDEX nFound = 0;

		//nNewChannels = 0 means user can choose how many channels to remove
		if(nNewChannels > 0)
		{
			nChnToRemove = GetNumChannels() - nNewChannels;
			nFound = nChnToRemove;
		} else
		{
			nChnToRemove = 0;
			nFound = GetNumChannels();
		}
		
		CRemoveChannelsDlg rem(&m_SndFile, nChnToRemove, showCancelInRemoveDlg);
		CheckUsedChannels(rem.m_bKeepMask, nFound);
		if (rem.DoModal() != IDOK) return false;

		// Removing selected channels
		return RemoveChannels(rem.m_bKeepMask);
	} else
	{
		// Increasing number of channels
		BeginWaitCursor();
		vector<CHANNELINDEX> channels(nNewChannels, CHANNELINDEX_INVALID);
		for(CHANNELINDEX nChn = 0; nChn < GetNumChannels(); nChn++)
		{
			channels[nChn] = nChn;
		}

		const bool success = (ReArrangeChannels(channels) == nNewChannels);
		if(success)
		{
			SetModified();
			UpdateAllViews(NULL, HINT_MODTYPE);
		}
		return success;
	}
}


// To remove all channels whose index corresponds to false in the keepMask vector.
// Return true on success.
bool CModDoc::RemoveChannels(const vector<bool> &keepMask)
//--------------------------------------------------------
{
	CHANNELINDEX nRemainingChannels = 0;
	//First calculating how many channels are to be left
	for(CHANNELINDEX nChn = 0; nChn < GetNumChannels(); nChn++)
	{
		if(keepMask[nChn]) nRemainingChannels++;
	}
	if(nRemainingChannels == GetNumChannels() || nRemainingChannels < m_SndFile.GetModSpecifications().channelsMin)
	{
		CString str;	
		if(nRemainingChannels == GetNumChannels())
			str = "No channels chosen to be removed.";
		else
			str = "No removal done - channel number is already at minimum.";
		Reporting::Information(str, "Remove Channels");
		return false;
	}

	BeginWaitCursor();
	// Create new channel order, with only channels from m_bChnMask left.
	vector<CHANNELINDEX> channels(nRemainingChannels, 0);
	CHANNELINDEX i = 0;
	for(CHANNELINDEX nChn = 0; nChn < GetNumChannels(); nChn++)
	{
		if(keepMask[nChn])
		{
			channels[i++] = nChn;
		}
	}
	const bool success = (ReArrangeChannels(channels) == nRemainingChannels);
	if(success)
	{
		SetModified();
		UpdateAllViews(NULL, HINT_MODTYPE);
	}
	EndWaitCursor();
	return success;

}


// Base code for adding, removing, moving and duplicating channels. Returns new number of channels on success, CHANNELINDEX_INVALID otherwise.
// The new channel vector can contain CHANNELINDEX_INVALID for adding new (empty) channels.
CHANNELINDEX CModDoc::ReArrangeChannels(const vector<CHANNELINDEX> &newOrder, const bool createUndoPoint)
//-------------------------------------------------------------------------------------------------------
{
	//newOrder[i] tells which current channel should be placed to i:th position in
	//the new order, or if i is not an index of current channels, then new channel is
	//added to position i. If index of some current channel is missing from the
	//newOrder-vector, then the channel gets removed.

	const CHANNELINDEX nRemainingChannels = static_cast<CHANNELINDEX>(newOrder.size());

	if(nRemainingChannels > m_SndFile.GetModSpecifications().channelsMax || nRemainingChannels < m_SndFile.GetModSpecifications().channelsMin) 	
	{
		CString str;
		str.Format(GetStrI18N(_TEXT("Can't apply change: Number of channels should be between %u and %u.")), m_SndFile.GetModSpecifications().channelsMin, m_SndFile.GetModSpecifications().channelsMax);
		Reporting::Error(str , "ReArrangeChannels");
		return CHANNELINDEX_INVALID;
	}

	if(m_SndFile.Patterns.Size() == 0)
	{
		// Nothing to do
		return GetNumChannels();
	}

	CriticalSection cs;
	if(createUndoPoint)
	{
		PrepareUndoForAllPatterns(true);
	}

	for(PATTERNINDEX nPat = 0; nPat < m_SndFile.Patterns.Size(); nPat++) 
	{
		if(m_SndFile.Patterns.IsValidPat(nPat))
		{
			ModCommand *oldPatData = m_SndFile.Patterns[nPat];
			ModCommand *newPatData = CPattern::AllocatePattern(m_SndFile.Patterns[nPat].GetNumRows(), nRemainingChannels);
			if(!newPatData)
			{
				cs.Leave();
				Reporting::Error("ERROR: Pattern allocation failed in ReArrangeChannels(...)");
				return CHANNELINDEX_INVALID;
			}
			ModCommand *tmpdest = newPatData;
			for(ROWINDEX nRow = 0; nRow < m_SndFile.Patterns[nPat].GetNumRows(); nRow++) //Scrolling rows
			{
				for(CHANNELINDEX nChn = 0; nChn < nRemainingChannels; nChn++, tmpdest++) //Scrolling channels.
				{
					if(newOrder[nChn] < GetNumChannels()) //Case: getting old channel to the new channel order.
						*tmpdest = *m_SndFile.Patterns[nPat].GetpModCommand(nRow, newOrder[nChn]);
					else //Case: figure newOrder[k] is not the index of any current channel, so adding a new channel.
						*tmpdest = ModCommand::Empty();

				}
			}
			m_SndFile.Patterns[nPat] = newPatData;
			CPattern::FreePattern(oldPatData);
		}
	}

	ModChannel chns[MAX_BASECHANNELS];
	ModChannelSettings settings[MAX_BASECHANNELS];
	vector<BYTE> recordStates(GetNumChannels(), 0);
	vector<bool> chnMutePendings(GetNumChannels(), false);

	for(CHANNELINDEX nChn = 0; nChn < GetNumChannels(); nChn++)
	{
		settings[nChn] = m_SndFile.ChnSettings[nChn];
		chns[nChn] = m_SndFile.Chn[nChn];
		recordStates[nChn] = IsChannelRecord(nChn);
		chnMutePendings[nChn] = m_SndFile.m_bChannelMuteTogglePending[nChn];
	}

	ReinitRecordState();

	for(CHANNELINDEX nChn = 0; nChn < nRemainingChannels; nChn++)
	{
		if(newOrder[nChn] < GetNumChannels())
		{
			m_SndFile.ChnSettings[nChn] = settings[newOrder[nChn]];
			m_SndFile.Chn[nChn] = chns[newOrder[nChn]];
			if(recordStates[newOrder[nChn]] == 1) Record1Channel(nChn, true);
			if(recordStates[newOrder[nChn]] == 2) Record2Channel(nChn, true);
			m_SndFile.m_bChannelMuteTogglePending[nChn] = chnMutePendings[newOrder[nChn]];
		}
		else
		{
			m_SndFile.InitChannel(nChn);
		}
	}
	// Reset MOD panning (won't affect other module formats)
	m_SndFile.SetupMODPanning();

	m_SndFile.m_nChannels = nRemainingChannels;

	// Reset removed channels. Most notably, clear the channel name.
	for(CHANNELINDEX nChn = GetNumChannels(); nChn < MAX_BASECHANNELS; nChn++)
	{
		m_SndFile.InitChannel(nChn);
		m_SndFile.Chn[nChn].dwFlags.set(CHN_MUTE);
	}

	return GetNumChannels();
}


// Functor for rewriting instrument numbers in patterns.
struct RewriteInstrumentReferencesInPatterns
//==========================================
{
	RewriteInstrumentReferencesInPatterns(const vector<ModCommand::INSTR> &indices) : instrumentIndices(indices)
	{
	}

	void operator()(ModCommand& m)
	{
		if(!m.IsPcNote() && m.instr < instrumentIndices.size())
		{
			m.instr = instrumentIndices[m.instr];
		}
	}

	const vector<ModCommand::INSTR> &instrumentIndices;
};


// Base code for adding, removing, moving and duplicating samples. Returns new number of samples on success, SAMPLEINDEX_INVALID otherwise.
// The new sample vector can contain SAMPLEINDEX_INVALID for adding new (empty) samples.
// newOrder indices are zero-based, i.e. newOrder[0] will define the contents of the first sample slot.
SAMPLEINDEX CModDoc::ReArrangeSamples(const vector<SAMPLEINDEX> &newOrder)
//------------------------------------------------------------------------
{
	if(newOrder.size() > m_SndFile.GetModSpecifications().samplesMax)
	{
		return SAMPLEINDEX_INVALID;
	}

	CriticalSection cs;

	const SAMPLEINDEX oldNumSamples = m_SndFile.GetNumSamples();
	m_SndFile.m_nSamples = static_cast<SAMPLEINDEX>(newOrder.size());

	for(SAMPLEINDEX i = 0; i < Util::Min(GetNumSamples(), oldNumSamples); i++)
	{
		if(newOrder[i] != i + 1)
		{
			GetSampleUndo().PrepareUndo(i + 1, sundo_replace);
		}
	}

	vector<int> sampleCount(oldNumSamples + 1, 0);
	vector<ModSample> sampleHeaders(oldNumSamples + 1);
	vector<SAMPLEINDEX> newIndex(oldNumSamples + 1, SAMPLEINDEX_INVALID);	// One of the new indexes for the old sample
	vector<std::string> sampleNames(oldNumSamples + 1);

	for(size_t i = 0; i < newOrder.size(); i++)
	{
		const SAMPLEINDEX origSlot = newOrder[i];
		if(origSlot > 0 && origSlot <= oldNumSamples)
		{
			sampleCount[origSlot]++;
			sampleHeaders[origSlot] = m_SndFile.GetSample(origSlot);
			newIndex[origSlot] = i + 1;
		}
	}

	// First, delete all samples that will be removed anyway.
	for(SAMPLEINDEX i = 1; i < sampleCount.size(); i++)
	{
		if(sampleCount[i] == 0)
		{
			m_SndFile.DestroySample(i);
		}
		sampleNames[i] = m_SndFile.m_szNames[i];
	}

	// Now, create new sample list.
	for(SAMPLEINDEX i = 0; i < newOrder.size(); i++)
	{
		const SAMPLEINDEX origSlot = newOrder[i];
		if(origSlot > 0 && origSlot <= oldNumSamples)
		{
			// Copy an original sample.
			m_SndFile.GetSample(i + 1) = sampleHeaders[origSlot];
			if(--sampleCount[origSlot] > 0 && sampleHeaders[origSlot].pSample != nullptr)
			{
				// This sample slot is referenced multiple times, so we have to copy the actual sample.
				m_SndFile.GetSample(i + 1).pSample = m_SndFile.AllocateSample(m_SndFile.GetSample(i + 1).GetSampleSizeInBytes());
				memcpy(m_SndFile.GetSample(i + 1).pSample, sampleHeaders[origSlot].pSample, m_SndFile.GetSample(i + 1).GetSampleSizeInBytes());
				ctrlSmp::AdjustEndOfSample(m_SndFile.GetSample(i + 1), &m_SndFile);
			}
			strcpy(m_SndFile.m_szNames[i + 1], sampleNames[origSlot].c_str());
		} else
		{
			// Invalid sample reference.
			m_SndFile.GetSample(i + 1).Initialize(m_SndFile.GetType());
			m_SndFile.GetSample(i + 1).pSample = nullptr;
			strcpy(m_SndFile.m_szNames[i + 1], "");
		}
	}

	if(m_SndFile.GetNumInstruments())
	{
		// Instrument mode: Update sample maps.
		for(INSTRUMENTINDEX i = 0; i <= m_SndFile.GetNumInstruments(); i++)
		{
			ModInstrument *ins = m_SndFile.Instruments[i];
			if(ins == nullptr)
			{
				continue;
			}
			for(size_t note = 0; note < CountOf(ins->Keyboard); note++)
			{
				if(ins->Keyboard[note] > 0 && ins->Keyboard[note] <= oldNumSamples && newIndex[ins->Keyboard[note]] != SAMPLEINDEX_INVALID)
				{
					ins->Keyboard[note] = newIndex[ins->Keyboard[note]];
				} else
				{
					ins->Keyboard[note] = 0;
				}
			}
		}
	} else
	{
		PrepareUndoForAllPatterns();

		vector<ModCommand::INSTR> indices(newOrder.size() + 1, 0);
		for(size_t i = 0; i < newOrder.size(); i++)
		{
			indices[i + 1] = newIndex[i];
		}
		m_SndFile.Patterns.ForEachModCommand(RewriteInstrumentReferencesInPatterns(indices));
	}

	return GetNumSamples();
}


// Base code for adding, removing, moving and duplicating instruments. Returns new number of instruments on success, INSTRUMENTINDEX_INVALID otherwise.
// The new instrument vector can contain INSTRUMENTINDEX_INVALID for adding new (empty) instruments.
// newOrder indices are zero-based, i.e. newOrder[0] will define the contents of the first instrument slot.
INSTRUMENTINDEX CModDoc::ReArrangeInstruments(const vector<INSTRUMENTINDEX> &newOrder)
//------------------------------------------------------------------------------------
{
	if(newOrder.size() > m_SndFile.GetModSpecifications().instrumentsMax || GetNumInstruments() == 0)
	{
		return INSTRUMENTINDEX_INVALID;
	}

	CriticalSection cs;

	const INSTRUMENTINDEX oldNumInstruments = m_SndFile.GetNumInstruments();
	m_SndFile.m_nInstruments = static_cast<INSTRUMENTINDEX>(newOrder.size());

	vector<ModInstrument> instrumentHeaders(oldNumInstruments + 1);
	vector<SAMPLEINDEX> newIndex(oldNumInstruments + 1, INSTRUMENTINDEX_INVALID);	// One of the new indexes for the old instrument
	for(size_t i = 0; i < newOrder.size(); i++)
	{
		const INSTRUMENTINDEX origSlot = newOrder[i];
		if(origSlot > 0 && origSlot <= oldNumInstruments)
		{
			if(m_SndFile.Instruments[origSlot] != nullptr)
				instrumentHeaders[origSlot] = *m_SndFile.Instruments[origSlot];
			newIndex[origSlot] = i + 1;
		}
	}

	// Now, create new instrument list.
	for(INSTRUMENTINDEX i = 0; i < newOrder.size(); i++)
	{
		if(m_SndFile.Instruments[i + 1] == nullptr)
		{
			m_SndFile.Instruments[i + 1] = new ModInstrument();
		}

		const INSTRUMENTINDEX origSlot = newOrder[i];
		if(origSlot > 0 && origSlot <= oldNumInstruments)
		{
			// Copy an original instrument.
			*m_SndFile.Instruments[i + 1] = instrumentHeaders[origSlot];
		} else
		{
			// Invalid sample instrument.
			*m_SndFile.Instruments[i + 1] = ModInstrument();
		}
	}
	// Free unused instruments
	for(INSTRUMENTINDEX i = newOrder.size(); i < oldNumInstruments; i++)
	{
		delete m_SndFile.Instruments[i + 1];
		m_SndFile.Instruments[i + 1] = nullptr;
	}

	PrepareUndoForAllPatterns();

	vector<ModCommand::INSTR> indices(newOrder.size() + 1, 0);
	for(size_t i = 1; i < newOrder.size(); i++)
	{
		indices[i + 1] = newIndex[i];
	}
	m_SndFile.Patterns.ForEachModCommand(RewriteInstrumentReferencesInPatterns(indices));

	return GetNumInstruments();
}


// Functor for converting instrument numbers to sample numbers in the patterns
struct ConvertInstrumentsToSamplesInPatterns
//==========================================
{
	ConvertInstrumentsToSamplesInPatterns(CSoundFile *pSndFile)
	{
		this->pSndFile = pSndFile;
	}

	void operator()(ModCommand& m)
	{
		if(m.instr && !m.IsPcNote())
		{
			ModCommand::INSTR instr = m.instr, newinstr = 0;
			ModCommand::NOTE note = m.note, newnote = note;
			if(ModCommand::IsNote(note))
				note = note - NOTE_MIN;
			else
				note = NOTE_MIDDLEC - NOTE_MIN;

			if((instr < MAX_INSTRUMENTS) && (pSndFile->Instruments[instr]))
			{
				const ModInstrument *pIns = pSndFile->Instruments[instr];
				newinstr = pIns->Keyboard[note];
				newnote = pIns->NoteMap[note];
				if(newinstr >= MAX_SAMPLES) newinstr = 0;
			}
			m.instr = newinstr;
			if(m.IsNote())
			{
				m.note = newnote;
			}
		}
	}

	CSoundFile *pSndFile;
};


bool CModDoc::ConvertInstrumentsToSamples()
//-----------------------------------------
{
	if (!m_SndFile.GetNumInstruments())
	{
		return false;
	}
	m_SndFile.Patterns.ForEachModCommand(ConvertInstrumentsToSamplesInPatterns(&m_SndFile));
	return true;
}


bool CModDoc::ConvertSamplesToInstruments()
//-----------------------------------------
{
	if(GetNumInstruments() > 0)
	{
		return false;
	}

	const INSTRUMENTINDEX nInstrumentMax = m_SndFile.GetModSpecifications().instrumentsMax;
	const SAMPLEINDEX nInstruments = min(m_SndFile.GetNumSamples(), nInstrumentMax);

	for(SAMPLEINDEX smp = 1; smp <= nInstruments; smp++)
	{
		const bool muted = IsSampleMuted(smp);
		MuteSample(smp, false);

		ModInstrument *instrument = m_SndFile.AllocateInstrument(smp, smp);
		if(instrument == nullptr)
		{
			ErrorBox(IDS_ERR_OUTOFMEMORY, CMainFrame::GetMainFrame());
			return false;
		}

		InitializeInstrument(instrument);
		lstrcpyn(instrument->name, m_SndFile.m_szNames[smp], MAX_INSTRUMENTNAME);
		MuteInstrument(smp, muted);
	}

	m_SndFile.m_nInstruments = nInstruments;

	return true;

}


UINT CModDoc::RemovePlugs(const vector<bool> &keepMask)
//-----------------------------------------------------
{
	//Remove all plugins whose keepMask[plugindex] is false.
	UINT nRemoved = 0;
	const PLUGINDEX maxPlug = min(MAX_MIXPLUGINS, keepMask.size());

	for (PLUGINDEX nPlug = 0; nPlug < maxPlug; nPlug++)
	{
		SNDMIXPLUGIN* pPlug = &m_SndFile.m_MixPlugins[nPlug];		
		if (keepMask[nPlug] || !pPlug)
		{
			continue;
		}

		if (pPlug->pPluginData)
		{
			delete[] pPlug->pPluginData;
			pPlug->pPluginData = NULL;
		}
		if (pPlug->pMixPlugin)
		{
			pPlug->pMixPlugin->Release();
			pPlug->pMixPlugin=NULL;
		}
		if (pPlug->pMixState)
		{
			delete pPlug->pMixState;
		}

		MemsetZero(pPlug->Info);
		Log("Zeroing range (%d) %X - %X\n", nPlug, &(pPlug->Info),  &(pPlug->Info)+sizeof(SNDMIXPLUGININFO));
		pPlug->nPluginDataSize=0;
		pPlug->fDryRatio=0;	
		pPlug->defaultProgram=0;
		nRemoved++;
	}

	return nRemoved;
}


BOOL CModDoc::AdjustEndOfSample(UINT nSample)
//-------------------------------------------
{
	if (nSample >= MAX_SAMPLES) return FALSE;
	ModSample &sample = m_SndFile.GetSample(nSample);
	if ((!sample.nLength) || (!sample.pSample)) return FALSE;

	ctrlSmp::AdjustEndOfSample(sample, &m_SndFile);

	return TRUE;
}


PATTERNINDEX CModDoc::InsertPattern(ORDERINDEX nOrd, ROWINDEX nRows)
//------------------------------------------------------------------
{
	const PATTERNINDEX i = m_SndFile.Patterns.Insert(nRows);
	if(i == PATTERNINDEX_INVALID)
		return i;

	//Increasing orderlist size if given order is beyond current limit,
	//or if the last order already has a pattern.
	if((nOrd == m_SndFile.Order.size() ||
		m_SndFile.Order.Last() < m_SndFile.Patterns.Size() ) &&
		m_SndFile.Order.GetLength() < m_SndFile.GetModSpecifications().ordersMax)
	{
		m_SndFile.Order.Append();
	}

	for (ORDERINDEX j = 0; j < m_SndFile.Order.size(); j++)
	{
		if (m_SndFile.Order[j] == i) break;
		if (m_SndFile.Order[j] == m_SndFile.Order.GetInvalidPatIndex() && nOrd == ORDERINDEX_INVALID)
		{
			m_SndFile.Order[j] = i;
			break;
		}
		if (j == nOrd)
		{
			for (ORDERINDEX k = m_SndFile.Order.size() - 1; k > j; k--)
			{
				m_SndFile.Order[k] = m_SndFile.Order[k - 1];
			}
			m_SndFile.Order[j] = i;
			break;
		}
	}

	SetModified();
	return i;
}


SAMPLEINDEX CModDoc::InsertSample(bool bLimit)
//--------------------------------------------
{
	SAMPLEINDEX i = m_SndFile.GetNextFreeSample();

	if ((bLimit && i >= 200 && !m_SndFile.GetNumInstruments()) || i == SAMPLEINDEX_INVALID)
	{
		ErrorBox(IDS_ERR_TOOMANYSMP, CMainFrame::GetMainFrame());
		return SAMPLEINDEX_INVALID;
	}
	if (!m_SndFile.m_szNames[i][0]) strcpy(m_SndFile.m_szNames[i], "untitled");
	if (i > m_SndFile.GetNumSamples()) m_SndFile.m_nSamples = i;
	m_SndFile.GetSample(i).Initialize(m_SndFile.GetType());
	SetModified();
	return i;
}


// Insert a new instrument assigned to sample nSample or duplicate instrument nDuplicate.
// If nSample is invalid, an appropriate sample slot is selected. 0 means "no sample".
INSTRUMENTINDEX CModDoc::InsertInstrument(SAMPLEINDEX nSample, INSTRUMENTINDEX nDuplicate)
//----------------------------------------------------------------------------------------
{
	if (m_SndFile.GetModSpecifications().instrumentsMax == 0) return INSTRUMENTINDEX_INVALID;

	ModInstrument *pDup = nullptr;

	if ((nDuplicate > 0) && (nDuplicate <= m_SndFile.m_nInstruments))
	{
		pDup = m_SndFile.Instruments[nDuplicate];
	}
	if ((!m_SndFile.GetNumInstruments()) && ((m_SndFile.GetNumSamples() > 1) || (m_SndFile.GetSample(1).pSample)))
	{
		if (pDup) return INSTRUMENTINDEX_INVALID;
		ConfirmAnswer result = Reporting::Confirm("Convert existing samples to instruments first?", true);
		if (result == cnfCancel)
		{
			return INSTRUMENTINDEX_INVALID;
		}
		if (result == cnfYes)
		{
			if(!ConvertSamplesToInstruments())
			{
				return INSTRUMENTINDEX_INVALID;
			}
		}
	}
	
	const INSTRUMENTINDEX newins = m_SndFile.GetNextFreeInstrument();
	if(newins == INSTRUMENTINDEX_INVALID)
	{
		ErrorBox(IDS_ERR_TOOMANYINS, CMainFrame::GetMainFrame());
		return INSTRUMENTINDEX_INVALID;
	} else if(newins > m_SndFile.GetNumInstruments())
	{
		m_SndFile.m_nInstruments = newins;
	}

	// Determine which sample slot to use
	SAMPLEINDEX newsmp = 0;
	if (nSample < m_SndFile.GetModSpecifications().samplesMax)
	{
		// Use specified slot
		newsmp = nSample;
	} else if (!pDup)
	{
		newsmp = m_SndFile.GetNextFreeSample(newins);
		/*
		for(SAMPLEINDEX k = 1; k <= m_SndFile.GetNumSamples(); k++)
		{
			if (!m_SndFile.IsSampleUsed(k))
			{
				// Sample isn't referenced by any instrument yet, so let's use it...
				newsmp = k;
				break;
			}
		}
		if (!newsmp)
		*/
		if (newsmp > m_SndFile.GetNumSamples())
		{
			// Add a new sample
			const SAMPLEINDEX inssmp = InsertSample();
			if (inssmp != SAMPLEINDEX_INVALID) newsmp = inssmp;
		}
	}

	CriticalSection cs;

	ModInstrument *pIns = m_SndFile.AllocateInstrument(newins, newsmp);
	if(pIns == nullptr)
	{
		cs.Leave();
		ErrorBox(IDS_ERR_OUTOFMEMORY, CMainFrame::GetMainFrame());
		return INSTRUMENTINDEX_INVALID;
	}
	InitializeInstrument(pIns);

	if (pDup)
	{
		*pIns = *pDup;
		// -> CODE#0023
		// -> DESC="IT project files (.itp)"
		strcpy(m_SndFile.m_szInstrumentPath[newins - 1], m_SndFile.m_szInstrumentPath[nDuplicate - 1]);
		m_bsInstrumentModified.reset(newins - 1);
		// -! NEW_FEATURE#0023
	}

	SetModified();

	return newins;
}


// Load default instrument values for inserting new instrument during editing
void CModDoc::InitializeInstrument(ModInstrument *pIns)
//-----------------------------------------------------
{
	pIns->nPluginVolumeHandling = CSoundFile::s_DefaultPlugVolumeHandling;
}


bool CModDoc::RemoveOrder(SEQUENCEINDEX nSeq, ORDERINDEX nOrd)
//------------------------------------------------------------
{
	if (nSeq >= m_SndFile.Order.GetNumSequences() || nOrd >= m_SndFile.Order.GetSequence(nSeq).size())
		return false;

	CriticalSection cs;

	SEQUENCEINDEX nOldSeq = m_SndFile.Order.GetCurrentSequenceIndex();
	m_SndFile.Order.SetSequence(nSeq);
	for (ORDERINDEX i = nOrd; i < m_SndFile.Order.GetSequence(nSeq).size() - 1; i++)
	{
		m_SndFile.Order[i] = m_SndFile.Order[i + 1];
	}
	m_SndFile.Order[m_SndFile.Order.GetLastIndex()] = m_SndFile.Order.GetInvalidPatIndex();
	m_SndFile.Order.SetSequence(nOldSeq);
	SetModified();

	return true;
}



bool CModDoc::RemovePattern(PATTERNINDEX nPat)
//--------------------------------------------
{
	if ((nPat < m_SndFile.Patterns.Size()) && (m_SndFile.Patterns[nPat]))
	{
		CriticalSection cs;

		m_SndFile.Patterns.Remove(nPat);
		SetModified();

		return true;
	}
	return false;
}


bool CModDoc::RemoveSample(SAMPLEINDEX nSmp)
//------------------------------------------
{
	if ((nSmp) && (nSmp <= m_SndFile.GetNumSamples()))
	{
		CriticalSection cs;

		m_SndFile.DestroySample(nSmp);
		m_SndFile.m_szNames[nSmp][0] = 0;
		while ((m_SndFile.GetNumSamples() > 1)
			&& (!m_SndFile.m_szNames[m_SndFile.GetNumSamples()][0])
			&& (!m_SndFile.GetSample(m_SndFile.GetNumSamples()).pSample))
		{
			m_SndFile.m_nSamples--;
		}
		SetModified();

		return true;
	}
	return false;
}


bool CModDoc::RemoveInstrument(INSTRUMENTINDEX nIns)
//--------------------------------------------------
{
	if ((nIns) && (nIns <= m_SndFile.GetNumInstruments()) && (m_SndFile.Instruments[nIns]))
	{
		bool instrumentsLeft = false;

		if(m_SndFile.DestroyInstrument(nIns, askDeleteAssociatedSamples))
		{
			CriticalSection cs;
			if (nIns == m_SndFile.m_nInstruments) m_SndFile.m_nInstruments--;
			for (UINT i=1; i<MAX_INSTRUMENTS; i++) if (m_SndFile.Instruments[i]) instrumentsLeft = true;
			if (!instrumentsLeft) m_SndFile.m_nInstruments = 0;
			SetModified();

			return true;
		}
	}
	return false;
}


bool CModDoc::MoveOrder(ORDERINDEX nSourceNdx, ORDERINDEX nDestNdx, bool bUpdate, bool bCopy, SEQUENCEINDEX nSourceSeq, SEQUENCEINDEX nDestSeq)
//---------------------------------------------------------------------------------------------------------------------------------------------
{
	if (max(nSourceNdx, nDestNdx) >= m_SndFile.Order.size()) return false;
	if (nDestNdx >= m_SndFile.GetModSpecifications().ordersMax) return false;

	if(nSourceSeq == SEQUENCEINDEX_INVALID) nSourceSeq = m_SndFile.Order.GetCurrentSequenceIndex();
	if(nDestSeq == SEQUENCEINDEX_INVALID) nDestSeq = m_SndFile.Order.GetCurrentSequenceIndex();
	if (max(nSourceSeq, nDestSeq) >= m_SndFile.Order.GetNumSequences()) return false;
	PATTERNINDEX nSourcePat = m_SndFile.Order.GetSequence(nSourceSeq)[nSourceNdx];

	// save current working sequence
	SEQUENCEINDEX nWorkingSeq = m_SndFile.Order.GetCurrentSequenceIndex();

	// Delete source
	if (!bCopy)
	{
		m_SndFile.Order.SetSequence(nSourceSeq);
		m_SndFile.Order.Remove(nSourceNdx, nSourceNdx);
		if (nSourceNdx < nDestNdx && nSourceSeq == nDestSeq) nDestNdx--;
	}
	// Insert at dest
	m_SndFile.Order.SetSequence(nDestSeq);
	m_SndFile.Order.Insert(nDestNdx, 1, nSourcePat);

	if (bUpdate)
	{
		UpdateAllViews(NULL, HINT_MODSEQUENCE, NULL);
	}

	m_SndFile.Order.SetSequence(nWorkingSeq);
	return true;
}


BOOL CModDoc::ExpandPattern(PATTERNINDEX nPattern)
//------------------------------------------------
{
	ROWINDEX numRows;

	if(!m_SndFile.Patterns.IsValidPat(nPattern)
		|| (numRows = m_SndFile.Patterns[nPattern].GetNumRows()) > m_SndFile.GetModSpecifications().patternRowsMax / 2)
	{
		return false;
	}

	BeginWaitCursor();
	GetPatternUndo().PrepareUndo(nPattern, 0, 0, GetNumChannels(), numRows);
	bool success = m_SndFile.Patterns[nPattern].Expand();
	EndWaitCursor();

	if(success)
	{
		SetModified();
		UpdateAllViews(NULL, HINT_PATTERNDATA | (nPattern << HINT_SHIFT_PAT), NULL);
	} else
	{
		GetPatternUndo().RemoveLastUndoStep();
	}
	return success;
}


BOOL CModDoc::ShrinkPattern(PATTERNINDEX nPattern)
//------------------------------------------------
{
	ROWINDEX numRows;

	if(!m_SndFile.Patterns.IsValidPat(nPattern)
		|| (numRows = m_SndFile.Patterns[nPattern].GetNumRows()) < m_SndFile.GetModSpecifications().patternRowsMin * 2)
	{
		return false;
	}

	BeginWaitCursor();
	GetPatternUndo().PrepareUndo(nPattern, 0, 0, GetNumChannels(), numRows);
	bool success = m_SndFile.Patterns[nPattern].Shrink();
	EndWaitCursor();

	if(success)
	{
		SetModified();
		UpdateAllViews(NULL, HINT_PATTERNDATA | (nPattern << HINT_SHIFT_PAT), NULL);
	} else
	{
		GetPatternUndo().RemoveLastUndoStep();
	}
	return success;
}


/////////////////////////////////////////////////////////////////////////////////////////
// Copy/Paste envelope

static LPCSTR pszEnvHdr = "Modplug Tracker Envelope\r\n";
static LPCSTR pszEnvFmt = "%d,%d,%d,%d,%d,%d,%d,%d\r\n";

bool CModDoc::CopyEnvelope(UINT nIns, enmEnvelopeTypes nEnv)
//----------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();
	HANDLE hCpy;
	CHAR s[4096];
	ModInstrument *pIns;
	DWORD dwMemSize;

	if ((nIns < 1) || (nIns > m_SndFile.m_nInstruments) || (!m_SndFile.Instruments[nIns]) || (!pMainFrm)) return false;
	BeginWaitCursor();
	pIns = m_SndFile.Instruments[nIns];
	if(pIns == nullptr) return false;
	
	const InstrumentEnvelope &env = pIns->GetEnvelope(nEnv);

	// We don't want to copy empty envelopes
	if(env.nNodes == 0)
	{
		return false;
	}

	strcpy(s, pszEnvHdr);
	wsprintf(s + strlen(s), pszEnvFmt, env.nNodes, env.nSustainStart, env.nSustainEnd, env.nLoopStart, env.nLoopEnd,
		env.dwFlags[ENV_SUSTAIN] ? 1 : 0, env.dwFlags[ENV_LOOP] ? 1 : 0, env.dwFlags[ENV_CARRY] ? 1 : 0);
	for (UINT i = 0; i < env.nNodes; i++)
	{
		if (strlen(s) >= sizeof(s)-32) break;
		wsprintf(s+strlen(s), "%d,%d\r\n", env.Ticks[i], env.Values[i]);
	}

	//Writing release node
	if(strlen(s) < sizeof(s) - 32)
		wsprintf(s+strlen(s), "%u\r\n", env.nReleaseNode);

	dwMemSize = strlen(s)+1;
	if ((pMainFrm->OpenClipboard()) && ((hCpy = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, dwMemSize))!=NULL))
	{
		EmptyClipboard();
		LPBYTE p = (LPBYTE)GlobalLock(hCpy);
		memcpy(p, s, dwMemSize);
		GlobalUnlock(hCpy);
		SetClipboardData (CF_TEXT, (HANDLE)hCpy);
		CloseClipboard();
	}
	EndWaitCursor();
	return true;
}


bool CModDoc::PasteEnvelope(UINT nIns, enmEnvelopeTypes nEnv)
//-----------------------------------------------------------
{
	CMainFrame *pMainFrm = CMainFrame::GetMainFrame();

	if ((nIns < 1) || (nIns > m_SndFile.m_nInstruments) || (!m_SndFile.Instruments[nIns]) || (!pMainFrm)) return false;
	BeginWaitCursor();
	if (!pMainFrm->OpenClipboard())
	{
		EndWaitCursor();
		return false;
	}
	HGLOBAL hCpy = ::GetClipboardData(CF_TEXT);
	LPCSTR p;
	if ((hCpy) && ((p = (LPSTR)GlobalLock(hCpy)) != NULL))
	{
		ModInstrument *pIns = m_SndFile.Instruments[nIns];

		UINT susBegin = 0, susEnd = 0, loopBegin = 0, loopEnd = 0, bSus = 0, bLoop = 0, bCarry = 0, nPoints = 0, releaseNode = ENV_RELEASE_NODE_UNSET;
		DWORD dwMemSize = GlobalSize(hCpy), dwPos = strlen(pszEnvHdr);
		if ((dwMemSize > dwPos) && (!_strnicmp(p, pszEnvHdr, dwPos - 2)))
		{
			sscanf(p + dwPos, pszEnvFmt, &nPoints, &susBegin, &susEnd, &loopBegin, &loopEnd, &bSus, &bLoop, &bCarry);
			while ((dwPos < dwMemSize) && (p[dwPos] != '\r') && (p[dwPos] != '\n')) dwPos++;

			nPoints = min(nPoints, m_SndFile.GetModSpecifications().envelopePointsMax);
			if (susEnd >= nPoints) susEnd = 0;
			if (susBegin > susEnd) susBegin = susEnd;
			if (loopEnd >= nPoints) loopEnd = 0;
			if (loopBegin > loopEnd) loopBegin = loopEnd;

			InstrumentEnvelope &env = pIns->GetEnvelope(nEnv);

			env.nNodes = nPoints;
			env.nSustainStart = susBegin;
			env.nSustainEnd = susEnd;
			env.nLoopStart = loopBegin;
			env.nLoopEnd = loopEnd;
			env.nReleaseNode = releaseNode;
			env.dwFlags.set(ENV_LOOP, bLoop != 0);
			env.dwFlags.set(ENV_SUSTAIN, bSus != 0);
			env.dwFlags.set(ENV_CARRY, bCarry != 0);
			env.dwFlags.set(ENV_ENABLED, nPoints > 0);

			int oldn = 0;
			for (UINT i=0; i<nPoints; i++)
			{
				while ((dwPos < dwMemSize) && ((p[dwPos] < '0') || (p[dwPos] > '9'))) dwPos++;
				if (dwPos >= dwMemSize) break;
				int n1 = atoi(p+dwPos);
				while ((dwPos < dwMemSize) && (p[dwPos] != ',')) dwPos++;
				while ((dwPos < dwMemSize) && ((p[dwPos] < '0') || (p[dwPos] > '9'))) dwPos++;
				if (dwPos >= dwMemSize) break;
				int n2 = atoi(p+dwPos);
				if ((n1 < oldn) || (n1 > ENVELOPE_MAX_LENGTH)) n1 = oldn + 1;
				env.Ticks[i] = (WORD)n1;
				env.Values[i] = (BYTE)n2;
				oldn = n1;
				while ((dwPos < dwMemSize) && (p[dwPos] != '\r') && (p[dwPos] != '\n')) dwPos++;
				if (dwPos >= dwMemSize) break;
			}

			//Read releasenode information.
			if(dwPos < dwMemSize)
			{
				BYTE r = static_cast<BYTE>(atoi(p + dwPos));
				if(r == 0 || r >= nPoints || !m_SndFile.GetModSpecifications().hasReleaseNode)
					r = ENV_RELEASE_NODE_UNSET;
				env.nReleaseNode = r;
			}
		}
		GlobalUnlock(hCpy);
		CloseClipboard();
		SetModified();
		UpdateAllViews(NULL, (nIns << HINT_SHIFT_INS) | HINT_ENVELOPE, NULL);
	}
	EndWaitCursor();
	return true;
}


// Check which channels contain note data. maxRemoveCount specified how many empty channels are reported at max.
void CModDoc::CheckUsedChannels(vector<bool> &usedMask, CHANNELINDEX maxRemoveCount) const
//----------------------------------------------------------------------------------------
{
	// Checking for unused channels
	const int nChannels = GetNumChannels();
	usedMask.resize(nChannels);
	for(int iRst = nChannels - 1; iRst >= 0; iRst--)
	{
		usedMask[iRst] = !IsChannelUnused(iRst);
		if(!usedMask[iRst])
		{
			// Found enough empty channels yet?
			if((--maxRemoveCount) == 0) break;
		}
	}
}


// Check if a given channel contains note data.
bool CModDoc::IsChannelUnused(CHANNELINDEX nChn) const
//----------------------------------------------------
{
	const CHANNELINDEX nChannels = GetNumChannels();
	if(nChn >= nChannels)
	{
		return true;
	}
	for(PATTERNINDEX nPat = 0; nPat < m_SndFile.Patterns.Size(); nPat++)
	{
		if(m_SndFile.Patterns.IsValidPat(nPat))
		{
			const ModCommand *p = m_SndFile.Patterns[nPat] + nChn;
			for(ROWINDEX nRow = m_SndFile.Patterns[nPat].GetNumRows(); nRow > 0; nRow--, p += nChannels)
			{
				if(!p->IsEmpty())
				{
					return false;
				}
			}
		}
	}
	return true;
}


// Convert the module's restart position information to a pattern command.
bool CModDoc::RestartPosToPattern()
//---------------------------------
{
	bool result = false;
	GetLengthType length = m_SndFile.GetLength(eNoAdjust);
	if(length.endOrder != ORDERINDEX_INVALID && length.endRow != ROWINDEX_INVALID)
	{
		result = m_SndFile.Patterns[m_SndFile.Order[length.endOrder]].WriteEffect(EffectWriter(CMD_POSITIONJUMP, m_SndFile.m_nRestartPos).Row(length.endRow).Retry(EffectWriter::rmTryNextRow));
	}
	m_SndFile.m_nRestartPos = 0;
	return result;
}


// Convert module's default global volume to a pattern command.
bool CModDoc::GlobalVolumeToPattern()
//-----------------------------------
{
	bool result = false;
	if(m_SndFile.GetModSpecifications().HasCommand(CMD_GLOBALVOLUME))
	{
		for(ORDERINDEX i = 0; i < m_SndFile.Order.GetLength(); i++)
		{
			if(m_SndFile.Patterns[m_SndFile.Order[i]].WriteEffect(EffectWriter(CMD_GLOBALVOLUME, m_SndFile.m_nDefaultGlobalVolume * 64 / MAX_GLOBAL_VOLUME).Retry(EffectWriter::rmTryNextRow)))
			{
				result = true;
				break;
			}
		}
	}

	m_SndFile.m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME;
	return result;
}