/*
 * InputHandler.cpp
 * ----------------
 * Purpose: Implementation of keyboard input handling, keymap loading, ...
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "afxtempl.h"
#include "CommandSet.h"
#include "inputhandler.h"
#include "Resource.h"
#include "mptrack.h"
#include "mainfrm.h"
#include <direct.h>
#include ".\inputhandler.h"
#include <Shlwapi.h>

#define TRANSITIONBIT 0x8000
#define REPEATBIT 0x4000

CInputHandler::CInputHandler(CWnd *mainframe)
//-------------------------------------------
{
	m_pMainFrm = mainframe;

	//Init CommandSet and Load defaults
	activeCommandSet = new CCommandSet();

	mpt::PathString sDefaultPath = theApp.GetConfigPath() + MPT_PATHSTRING("Keybindings.mkb");
	if(sDefaultPath.AsNative().length() > MAX_PATH - 1)
		sDefaultPath = mpt::PathString();

	const bool bNoExistingKbdFileSetting = TrackerSettings::Instance().m_szKbdFile.empty();

	// 1. Try to load keybindings from the path saved in the settings.
	// 2. If the setting doesn't exist or the loading fails, try to load from default location.
	// 3. If neither one of these worked, load default keybindings from resources.
	// 4. If there were no keybinding setting already, create a keybinding file to default location
	//    and set its path to settings.

	if (bNoExistingKbdFileSetting || !(activeCommandSet->LoadFile(TrackerSettings::Instance().m_szKbdFile)))
	{
		if (bNoExistingKbdFileSetting)
			TrackerSettings::Instance().m_szKbdFile = sDefaultPath;
		bool bSuccess = false;
		if (PathFileExistsW(sDefaultPath.AsNative().c_str()) == TRUE)
			bSuccess = activeCommandSet->LoadFile(sDefaultPath);
		if (!bSuccess)
		{
			// Load keybindings from resources.
			Log("Loading keybindings from resources\n");
			bSuccess = activeCommandSet->LoadDefaultKeymap();
			if (bSuccess && bNoExistingKbdFileSetting)
			{
				activeCommandSet->SaveFile(TrackerSettings::Instance().m_szKbdFile);
			}
		}
		if (!bSuccess)
			ErrorBox(IDS_UNABLE_TO_LOAD_KEYBINDINGS);
	}
	// We will only overwrite the default Keybindings.mkb file from now on.
	TrackerSettings::Instance().m_szKbdFile = sDefaultPath;

	//Get Keymap
	activeCommandSet->GenKeyMap(keyMap);
	SetupSpecialKeyInterception(); // Feature: use Windows keys as modifier keys, intercept special keys

	m_bBypass = false;
	modifierMask=0;
	m_bNoAltMenu = true;

}


CInputHandler::~CInputHandler()
//-----------------------------
{
	delete activeCommandSet;
}


CommandID CInputHandler::GeneralKeyEvent(InputTargetContext context, int code, WPARAM wParam, LPARAM lParam)
//----------------------------------------------------------------------------------------------------------
{
	KeyMap::const_iterator cmd = keyMap.end();
	KeyEventType keyEventType;

	if(code == HC_ACTION)
	{
		//Get the KeyEventType (key up, key down, key repeat)
		DWORD scancode = lParam >> 16;
		if((scancode & 0xC000) == 0xC000)
		{
			keyEventType = kKeyEventUp;
		} else if((scancode & 0xC000) == 0x0000)
		{
			keyEventType = kKeyEventDown;
		} else
		{
			keyEventType = kKeyEventRepeat;
		}

		// Catch modifier change (ctrl, alt, shift) - Only check on keyDown or keyUp.
		// NB: we want to catch modifiers even when the input handler is locked
		if(keyEventType == kKeyEventUp || keyEventType == kKeyEventDown)
		{
			scancode =(lParam >> 16) & 0x1FF;
			CatchModifierChange(wParam, keyEventType, scancode);
		}

		if(!InterceptSpecialKeys(wParam, lParam, true) && !IsBypassed())
		{
			// only execute command when the input handler is not locked
			// and the input is not a consequence of special key interception.
			cmd = keyMap.find(KeyCombination(context, modifierMask, wParam, keyEventType));
		}
	}
	if(code == HC_MIDI)
	{
		cmd = keyMap.find(KeyCombination(context, HOTKEYF_MIDI, wParam, kKeyEventDown));
	}

	CommandID executeCommand = kcNull;
	if(m_pMainFrm && cmd != keyMap.end())
	{
		executeCommand = cmd->second;
		if(!m_pMainFrm->SendMessage(WM_MOD_KEYCOMMAND, executeCommand, wParam))
		{
			// Command was not handled, so let Windows process it.
			return kcNull;
		}
	}

	return executeCommand;
}


CommandID CInputHandler::KeyEvent(InputTargetContext context, UINT &nChar, UINT &/*nRepCnt*/, UINT &nFlags, KeyEventType keyEventType, CWnd* pSourceWnd)
//----------------------------------------------------------------------------------------------------------------------------------------------------------
{
	if(InterceptSpecialKeys(nChar, nFlags, false))
		return kcNull;
	KeyMap::const_iterator cmd = keyMap.find(KeyCombination(context, modifierMask, nChar, keyEventType));
	CommandID executeCommand = kcNull;

	if(pSourceWnd == nullptr)
		pSourceWnd = m_pMainFrm;	//by default, send command message to main frame.

	if(pSourceWnd && cmd != keyMap.end())
	{
		executeCommand = cmd->second;
		if(!pSourceWnd->SendMessage(WM_MOD_KEYCOMMAND, executeCommand, nChar))
		{
			// Command was not handled, so let Windows process it.
			return kcNull;
		}
	}

	return executeCommand;
}


// Feature: use Windows keys as modifier keys, intercept special keys
bool CInputHandler::InterceptSpecialKeys(UINT nChar, UINT nFlags, bool generateMsg)
//---------------------------------------------------------------------------------
{
	KeyEventType keyEventType = GetKeyEventType(HIWORD(nFlags));
	enum { VK_NonExistentKey = VK_F24+1 };

	if(nChar == VK_NonExistentKey)
	{
		return true;
	} else if(m_bInterceptWindowsKeys && (nChar == VK_LWIN || nChar == VK_RWIN))
	{
		if(keyEventType == kKeyEventDown)
		{
			INPUT inp[2];
			inp[0].type = inp[1].type = INPUT_KEYBOARD;
			inp[0].ki.time = inp[1].ki.time = 0;
			inp[0].ki.dwExtraInfo = inp[1].ki.dwExtraInfo = 0;
			inp[0].ki.wVk = inp[1].ki.wVk = VK_NonExistentKey;
			inp[0].ki.wScan = inp[1].ki.wScan = 0;
			inp[0].ki.dwFlags = 0;
			inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
			SendInput(2, inp, sizeof(INPUT));
		}
	}

	if((nChar == VK_NUMLOCK && m_bInterceptNumLock)
		|| (nChar == VK_CAPITAL && m_bInterceptCapsLock)
		|| (nChar == VK_SCROLL && m_bInterceptScrollLock))
	{
		if(GetMessageExtraInfo() == 0xC0FFEE)
		{
			SetMessageExtraInfo(0);
			return true;
		} else if(keyEventType == kKeyEventDown && generateMsg)
		{
			// Prevent keys from lighting up by simulating a second press.
			INPUT inp[2];
			inp[0].type = inp[1].type = INPUT_KEYBOARD;
			inp[0].ki.time = inp[1].ki.time = 0;
			inp[0].ki.dwExtraInfo = inp[1].ki.dwExtraInfo = 0xC0FFEE;
			inp[0].ki.wVk = inp[1].ki.wVk = static_cast<WORD>(nChar);
			inp[0].ki.wScan = inp[1].ki.wScan = 0;
			inp[0].ki.dwFlags = KEYEVENTF_KEYUP;
			inp[1].ki.dwFlags = 0;
			SendInput(2, inp, sizeof(INPUT));
		}
	}
	return false;
};


void CInputHandler::SetupSpecialKeyInterception()
//-----------------------------------------------
{
	m_bInterceptWindowsKeys = m_bInterceptNumLock = m_bInterceptCapsLock = m_bInterceptScrollLock = false;
	for(KeyMap::const_iterator i = keyMap.begin(); i != keyMap.end(); i++)
	{
		ASSERT(i->second != kcNull);
		if(i->first.Modifier() == HOTKEYF_EXT) m_bInterceptWindowsKeys = true;
		if(i->first.KeyCode() == VK_NUMLOCK) m_bInterceptNumLock = true;
		if(i->first.KeyCode() == VK_CAPITAL) m_bInterceptCapsLock = true;
		if(i->first.KeyCode() == VK_SCROLL) m_bInterceptScrollLock = true;
	}
};


//Deal with Modifier keypresses. Private surouting used above.
bool CInputHandler::CatchModifierChange(WPARAM wParam, KeyEventType keyEventType, int /*scancode*/)
//-------------------------------------------------------------------------------------------------
{
	UINT tempModifierMask = 0;
	switch(wParam)
	{
		case VK_CONTROL:
/*			if (m_bDistinguishControls)
			{
				if		(scancode == SC_LCONTROL)	tempModifierMask |= LControl;
				else if (scancode == SC_RCONTROL)	tempModifierMask |= RControl;
				break;
			}
			else
			{
*/				tempModifierMask |= HOTKEYF_CONTROL;
				break;
//			}

		case VK_SHIFT:
/*			if (m_bDistinguishShifts)
			{
				if		(scancode == SC_LSHIFT)		tempModifierMask |= LShift;
				else if (scancode == SC_RSHIFT)		tempModifierMask |= RShift;
				break;
			}
			else
			{
*/				tempModifierMask |= HOTKEYF_SHIFT;
				break;
//			}

		case VK_MENU:
/*			if (m_bDistinguishAlts)
			{
				if		(scancode == SC_LALT)		{tempModifierMask |= LAlt;	break;}
				else if	(scancode == SC_RALT)		{tempModifierMask |= RAlt;	break;}
			}
			else
			{
*/				tempModifierMask |= HOTKEYF_ALT;
				break;
//			}
		case VK_LWIN: case VK_RWIN: // Feature: use Windows keys as modifier keys
				tempModifierMask |= HOTKEYF_EXT;
				break;
	}

	if (tempModifierMask)	//This keypress just changed the modifier mask
	{
		if (keyEventType == kKeyEventDown)
		{
			modifierMask |= tempModifierMask;
#ifdef _DEBUG
			LogModifiers(modifierMask);
#endif
		}
		if (keyEventType == kKeyEventUp)
			modifierMask &= ~tempModifierMask;

		return true;
	}

	return false;
}


// Translate MIDI messages to shortcut commands
CommandID CInputHandler::HandleMIDIMessage(InputTargetContext context, uint32 message)
//------------------------------------------------------------------------------------
{
	if(MIDIEvents::GetTypeFromEvent(message) == MIDIEvents::evControllerChange && MIDIEvents::GetDataByte2FromEvent(message) != 0)
	{
		// Only capture MIDI CCs for now. Some controllers constantly send some MIDI CCs with value 0
		// (e.g. the Roland D-50 sends CC123 whenenver releasing note keys), so we will ignore those.
		return GeneralKeyEvent(context, HC_MIDI, MIDIEvents::GetDataByte1FromEvent(message), 0);
	}
	return kcNull;
}


int CInputHandler::GetKeyListSize(CommandID cmd)
//----------------------------------------------
{
	return activeCommandSet->GetKeyListSize(cmd);
}


//-------------------------------------------------------------
//----------------------- Misc
//--------------------------------------------------------------



/*
void CInputHandler::GenCommandList(const KeyMap &km, CommandStruct coms[], )
{

}
*/

void CInputHandler::LogModifiers(UINT mask)
//-----------------------------------------
{
	Log("----------------------------------\n");
	if (mask & HOTKEYF_CONTROL) Log("Ctrl On"); else Log("Ctrl --");
	if (mask & HOTKEYF_SHIFT)   Log("\tShft On"); else Log("\tShft --");
	if (mask & HOTKEYF_ALT)     Log("\tAlt  On\n"); else Log("\tAlt  --\n");
	if (mask & HOTKEYF_EXT)     Log("\tWin  On\n"); else Log("\tWin  --\n"); // Feature: use Windows keys as modifier keys
}


KeyEventType CInputHandler::GetKeyEventType(UINT nFlags)
//------------------------------------------------------
{
	if (nFlags & TRANSITIONBIT)
	{
		// Key released
		return kKeyEventUp;
	} else
	{
		if (nFlags & REPEATBIT)
		{
			// Key repeated
			return kKeyEventRepeat;
		} else
		{
			// New key down
			return kKeyEventDown;
		}
	}
}


bool CInputHandler::SelectionPressed()
//------------------------------------
{
	bool result=false;
	int nSelectionKeys = activeCommandSet->GetKeyListSize(kcSelect);
	KeyCombination key;

	for (int k=0; k<nSelectionKeys; k++)
	{
		key = activeCommandSet->GetKey(kcSelect, k);
		if (modifierMask & key.Modifier())
		{
			result=true;
			break;
		}
	}
	return result;
}


bool CInputHandler::ShiftPressed()
//--------------------------------
{
	return (modifierMask & HOTKEYF_SHIFT);
}


bool CInputHandler::CtrlPressed()
//-------------------------------
{
	return ((modifierMask & HOTKEYF_CONTROL) != 0);
}


bool CInputHandler::AltPressed()
//------------------------------
{
	return ((modifierMask & HOTKEYF_ALT) != 0);
}


void CInputHandler::Bypass(bool b)
//--------------------------------
{
	m_bBypass=b;
}


bool CInputHandler::IsBypassed()
//------------------------------
{
	return m_bBypass;
}


WORD CInputHandler::GetModifierMask()
//-----------------------------------
{
	return (WORD)modifierMask;
}


void CInputHandler::SetModifierMask(WORD mask)
//--------------------------------------------
{
	modifierMask=mask;
}


CString CInputHandler::GetKeyTextFromCommand(CommandID c)
//-------------------------------------------------------
{
	return activeCommandSet->GetKeyTextFromCommand(c, 0);
}


#define FILENEW 1
#define MAINVIEW 59392

CString CInputHandler::GetMenuText(UINT id)
//-----------------------------------------
{
	const TCHAR *s;
	CommandID c = kcNull;

	switch(id)
	{
		case FILENEW:				s = _T("&New"); c = kcFileNew; break;
		case ID_FILE_OPEN:			s = _T("&Open..."); c = kcFileOpen; break;
		case ID_FILE_OPENTEMPLATE:	return "Open &Template";
		case ID_FILE_CLOSE:			s = _T("&Close"); c = kcFileClose; break;
		case ID_FILE_CLOSEALL:		s = _T("C&lose All"); c = kcFileCloseAll; break;
		case ID_FILE_SAVE:			s = _T("&Save"); c = kcFileSave; break;
		case ID_FILE_SAVE_AS:		s = _T("Save &As..."); c = kcFileSaveAs; break;
		case ID_FILE_SAVEASTEMPLATE:s = _T("Sa&ve as Template"); c = kcFileSaveTemplate; break;
		case ID_FILE_SAVEASWAVE:	s = _T("Export as lossless (&Wave, FLAC)..."); c = kcFileSaveAsWave; break;
		case ID_FILE_SAVEASMP3:		s = _T("Export as lossy (Opus, Vorbis, M&P3)..."); c = kcFileSaveAsMP3; break;
		case ID_FILE_SAVEMIDI:		s = _T("Export as M&IDI..."); c = kcFileSaveMidi; break;
		case ID_FILE_SAVECOMPAT:	s = _T("Compatibility &Export..."); c = kcFileExportCompat; break;
		case ID_IMPORT_MIDILIB:		s = _T("Import &MIDI Library..."); c = kcFileImportMidiLib; break;
		case ID_ADD_SOUNDBANK:		s = _T("Add Sound &Bank..."); c = kcFileAddSoundBank; break;

		case ID_PLAYER_PLAY:		s = _T("Pause / &Resume"); c = kcPlayPauseSong; break;
		case ID_PLAYER_PLAYFROMSTART:	s = _T("&Play from Start"); c = kcPlaySongFromStart; break;
		case ID_PLAYER_STOP:		s = _T("&Stop"); c = kcStopSong; break;
		case ID_PLAYER_PAUSE:		s = _T("P&ause"); c = kcPauseSong; break;
		case ID_MIDI_RECORD:		s = _T("&MIDI Record"); c = kcMidiRecord; break;
		case ID_ESTIMATESONGLENGTH:	s = _T("&Estimate Song Length"); c = kcEstimateSongLength; break;
		case ID_APPROX_BPM:			s = _T("Approx. real &BPM"); c = kcApproxRealBPM; break;

		case ID_EDIT_UNDO:			s = _T("&Undo"); c = kcEditUndo; break;
		case ID_EDIT_REDO:			s = _T("&Redo"); c = kcEditRedo; break;
		case ID_EDIT_CUT:			s = _T("Cu&t"); c = kcEditCut; break;
		case ID_EDIT_COPY:			s = _T("&Copy"); c = kcEditCopy; break;
		case ID_EDIT_PASTE:			s = _T("&Paste"); c = kcEditPaste; break;
		case ID_EDIT_SELECT_ALL:	s = _T("Select &All"); c = kcEditSelectAll; break;
		case ID_EDIT_CLEANUP:		s = _T("C&leanup"); break;
		case ID_EDIT_FIND:			s = _T("&Find / Replace"); c = kcEditFind; break;
		case ID_EDIT_FINDNEXT:		s = _T("Find &Next"); c = kcEditFindNext; break;
		case ID_EDIT_GOTO_MENU:		s = _T("&Goto"); c = kcPatternGoto; break;
		case ID_EDIT_SPLITKEYBOARDSETTINGS:	s = _T("Split &Keyboard Settings"); c = kcShowSplitKeyboardSettings; break;
			// "Paste Special" sub menu
		case ID_EDIT_PASTE_SPECIAL:	s = _T("&Mix Paste"); c = kcEditMixPaste; break;
		case ID_EDIT_MIXPASTE_ITSTYLE:	s = _T("M&ix Paste (IT Style)"); c = kcEditMixPasteITStyle; break;
		case ID_EDIT_PASTEFLOOD:	s = _T("Paste Fl&ood"); c = kcEditPasteFlood; break;
		case ID_EDIT_PUSHFORWARDPASTE:	s = _T("&Push Forward Paste (Insert)"); c = kcEditPushForwardPaste; break;

		case ID_VIEW_GLOBALS:		s = _T("&General"); c = kcViewGeneral; break;
		case ID_VIEW_SAMPLES:		s = _T("&Samples"); c = kcViewSamples; break;
		case ID_VIEW_PATTERNS:		s = _T("&Patterns"); c = kcViewPattern; break;
		case ID_VIEW_INSTRUMENTS:	s = _T("&Instruments"); c = kcViewInstruments; break;
		case ID_VIEW_COMMENTS:		s = _T("&Comments"); c = kcViewComments; break;
		case ID_VIEW_GRAPH:			s = _T("G&raph"); c = kcViewGraph; break; //rewbs.graph
		case MAINVIEW:				s = _T("&Main"); c = kcViewMain; break;
		case IDD_TREEVIEW:			s = _T("&Tree"); c = kcViewTree; break;
		case ID_VIEW_OPTIONS:		s = _T("S&etup..."); c = kcViewOptions; break;
		case ID_HELPSHOW:			s = _T("&Help..."); c = kcHelp; break;
		case ID_PLUGIN_SETUP:		s = _T("Pl&ugin Manager..."); c = kcViewAddPlugin; break;
		case ID_CHANNEL_MANAGER:	s = _T("Ch&annel Manager..."); c = kcViewChannelManager; break;
		case ID_CLIPBOARD_MANAGER:	s = _T("C&lipboard Manager..."); c = kcToggleClipboardManager; break;
		case ID_VIEW_SONGPROPERTIES:s = _T("Song P&roperties..."); c = kcViewSongProperties; break; //rewbs.graph
		case ID_VIEW_MIDIMAPPING:	s = _T("&MIDI Mapping..."); c = kcViewMIDImapping; break;
		case ID_VIEW_EDITHISTORY:	s = _T("Edit &History..."); c = kcViewEditHistory; break;
		// Help submenu:
		case ID_EXAMPLE_MODULES:	return _T("&Example Modules");

		default: return _T("Unknown Item.");
	}

	return s + CString(_T("\t")) + GetKeyTextFromCommand(c);
}


void CInputHandler::UpdateMainMenu()
//----------------------------------
{
	CMenu *pMenu = (CMainFrame::GetMainFrame())->GetMenu();
	if (!pMenu) return;

#define UPDATEMENU(id) pMenu->ModifyMenu(id, MF_BYCOMMAND | MF_STRING, id, GetMenuText(id));
	pMenu->GetSubMenu(0)->ModifyMenu(0, MF_BYPOSITION | MF_STRING, 0, GetMenuText(FILENEW));
	UPDATEMENU(ID_FILE_OPEN);
	UPDATEMENU(ID_FILE_CLOSE);
	UPDATEMENU(ID_FILE_SAVE);
	UPDATEMENU(ID_FILE_SAVE_AS);
	UPDATEMENU(ID_FILE_SAVEASWAVE);
	UPDATEMENU(ID_FILE_SAVEASMP3);
	UPDATEMENU(ID_FILE_SAVEMIDI);
	UPDATEMENU(ID_FILE_SAVECOMPAT);
	UPDATEMENU(ID_IMPORT_MIDILIB);
	UPDATEMENU(ID_ADD_SOUNDBANK);

	UPDATEMENU(ID_PLAYER_PLAY);
	UPDATEMENU(ID_PLAYER_PLAYFROMSTART);
	UPDATEMENU(ID_PLAYER_STOP);
	UPDATEMENU(ID_PLAYER_PAUSE);
	UPDATEMENU(ID_MIDI_RECORD);
	UPDATEMENU(ID_ESTIMATESONGLENGTH);
	UPDATEMENU(ID_APPROX_BPM);

	UPDATEMENU(ID_EDIT_UNDO);
	UPDATEMENU(ID_EDIT_REDO);
	UPDATEMENU(ID_EDIT_CUT);
	UPDATEMENU(ID_EDIT_COPY);
	UPDATEMENU(ID_EDIT_PASTE);
	UPDATEMENU(ID_EDIT_PASTE_SPECIAL);
	UPDATEMENU(ID_EDIT_MIXPASTE_ITSTYLE);
	UPDATEMENU(ID_EDIT_PASTEFLOOD);
	UPDATEMENU(ID_EDIT_PUSHFORWARDPASTE);
	UPDATEMENU(ID_EDIT_SELECT_ALL);
	UPDATEMENU(ID_EDIT_FIND);
	UPDATEMENU(ID_EDIT_FINDNEXT);
	UPDATEMENU(ID_EDIT_GOTO_MENU);
	UPDATEMENU(ID_EDIT_SPLITKEYBOARDSETTINGS);

	UPDATEMENU(ID_VIEW_GLOBALS);
	UPDATEMENU(ID_VIEW_SAMPLES);
	UPDATEMENU(ID_VIEW_PATTERNS);
	UPDATEMENU(ID_VIEW_INSTRUMENTS);
	UPDATEMENU(ID_VIEW_COMMENTS);
	UPDATEMENU(MAINVIEW);
	UPDATEMENU(IDD_TREEVIEW);
	UPDATEMENU(ID_VIEW_OPTIONS);
	UPDATEMENU(ID_PLUGIN_SETUP);
	UPDATEMENU(ID_CHANNEL_MANAGER);
	UPDATEMENU(ID_CLIPBOARD_MANAGER);
	UPDATEMENU(ID_VIEW_SONGPROPERTIES);
	UPDATEMENU(ID_VIEW_MIDIMAPPING);
	UPDATEMENU(ID_HELPSHOW);
#undef UPDATEMENU
}


void CInputHandler::SetNewCommandSet(CCommandSet *newSet)
//-------------------------------------------------------
{
	activeCommandSet->Copy(newSet);
	activeCommandSet->GenKeyMap(keyMap);
	SetupSpecialKeyInterception(); // Feature: use Windows keys as modifier keys, intercept special keys
	UpdateMainMenu();
}


bool CInputHandler::SetEffectLetters(const CModSpecifications &modSpecs)
//----------------------------------------------------------------------
{
	Log("Changing command set.\n");
	bool retval = activeCommandSet->QuickChange_SetEffects(modSpecs);
	activeCommandSet->GenKeyMap(keyMap);
	return retval;
}


bool CInputHandler::isKeyPressHandledByTextBox(DWORD key)
//-------------------------------------------------------
{

	//Alpha-numerics (only shift or no modifier):
	if(!CtrlPressed() &&  !AltPressed()
		&&  ((key>='A'&&key<='Z') || (key>='0'&&key<='9') ||
		 key==VK_DIVIDE  || key==VK_MULTIPLY || key==VK_SPACE || key==VK_RETURN ||
		 key==VK_CAPITAL || (key>=VK_OEM_1 && key<=VK_OEM_3) || (key>=VK_OEM_4 && key<=VK_OEM_8)))
		return true;

	//navigation (any modifier):
	if(key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
		key == VK_HOME || key == VK_END || key == VK_DELETE || key == VK_INSERT || key == VK_BACK)
		return true;

	//Copy paste etc..
	if(CMainFrame::GetInputHandler()->GetModifierMask()==HOTKEYF_CONTROL &&
		(key == 'Y' || key == 'Z' || key == 'X' ||  key == 'C' || key == 'V' || key == 'A'))
		return true;

	return false;
}