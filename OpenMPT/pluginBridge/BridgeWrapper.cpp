/*
 * BridgeWrapper.cpp
 * -----------------
 * Purpose: VST plugin bridge wrapper (host side)
 * Notes  : (currently none)
 * Authors: Johannes Schultz (OpenMPT Devs)
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"

#ifndef NO_VST
#include "BridgeWrapper.h"
#include "misc_util.h"
#include "../mptrack/Mptrack.h"
#include "../mptrack/Vstplug.h"
#include "../mptrack/ExceptionHandler.h"
#include "../common/mptFstream.h"
#include "../common/thread.h"
#include "../common/StringFixer.h"


OPENMPT_NAMESPACE_BEGIN


// Check whether we need to load a 32-bit or 64-bit wrapper.
BridgeWrapper::BinaryType BridgeWrapper::GetPluginBinaryType(const mpt::PathString &pluginPath)
{
	BinaryType type = binUnknown;
	mpt::ifstream file(pluginPath, std::ios::in | std::ios::binary);
	if(file.is_open())
	{
		IMAGE_DOS_HEADER dosHeader;
		IMAGE_NT_HEADERS ntHeader;
		file.read(reinterpret_cast<char *>(&dosHeader), sizeof(dosHeader));
		if(dosHeader.e_magic == IMAGE_DOS_SIGNATURE)
		{
			file.seekg(dosHeader.e_lfanew);
			file.read(reinterpret_cast<char *>(&ntHeader), sizeof(ntHeader));

			ASSERT((ntHeader.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0);
			if(ntHeader.FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
				type = bin32Bit;
			else if(ntHeader.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
				type = bin64Bit;
		}
	}
	return type;
}


uint64 BridgeWrapper::GetFileVersion(const WCHAR *exePath)
{
	DWORD verHandle = 0;
	DWORD verSize = GetFileVersionInfoSizeW(exePath, &verHandle);
	uint64 result = 0;
	if(verSize != 0)
	{
		LPSTR verData = new (std::nothrow) char[verSize];
		if(verData && GetFileVersionInfoW(exePath, verHandle, verSize, verData))
		{
			UINT size = 0;
			BYTE *lpBuffer = nullptr;
			if(VerQueryValue(verData, "\\", (void **)&lpBuffer, &size) && size != 0)
			{
				VS_FIXEDFILEINFO *verInfo = (VS_FIXEDFILEINFO *)lpBuffer;
				if (verInfo->dwSignature == 0xfeef04bd)
				{
					result = (uint64(HIWORD(verInfo->dwFileVersionMS)) << 48)
					       | (uint64(LOWORD(verInfo->dwFileVersionMS)) << 32)
					       | (uint64(HIWORD(verInfo->dwFileVersionLS)) << 16)
					       | uint64(LOWORD(verInfo->dwFileVersionLS));
				}
			}
		}
		delete[] verData;
	}
	return result;
}


// Create a plugin bridge object
AEffect *BridgeWrapper::Create(const VSTPluginLib &plugin)
{
	BridgeWrapper *wrapper = new (std::nothrow) BridgeWrapper();
	BridgeWrapper *sharedInstance = nullptr;

	// Should we share instances?
	if(plugin.shareBridgeInstance)
	{
		// Well, then find some instance to share with!
		CVstPlugin *vstPlug = plugin.pPluginsList;
		while(vstPlug != nullptr)
		{
			if(vstPlug->isBridged)
			{
				sharedInstance = reinterpret_cast<BridgeWrapper *>(vstPlug->Dispatch(effVendorSpecific, kVendorOpenMPT, kGetWrapperPointer, nullptr, 0.0f));
				break;
			}
			vstPlug = vstPlug->GetNextInstance();
		}
	}

	try
	{
		if(wrapper != nullptr && wrapper->Init(plugin.dllPath, sharedInstance) && wrapper->queueMem.Good())
		{
			return &wrapper->sharedMem->effect;
		}
		delete wrapper;
		return nullptr;
	} catch(BridgeException &)
	{
		delete wrapper;
		throw;
	}
}


// Initialize and launch bridge
bool BridgeWrapper::Init(const mpt::PathString &pluginPath, BridgeWrapper *sharedInstace)
{
	static uint32_t plugId = 0;
	plugId++;
	const DWORD procId = GetCurrentProcessId();

	const std::wstring mapName = L"Local\\openmpt-" + mpt::ToWString(procId) + L"-" + mpt::ToWString(plugId);

	// Create our shared memory object.
	if(!queueMem.Create(mapName.c_str(), sizeof(SharedMemLayout))
		|| !CreateSignals(mapName.c_str()))
	{
		throw BridgeException("Could not initialize plugin bridge memory.");
	}
	sharedMem = reinterpret_cast<SharedMemLayout *>(queueMem.view);

	if(sharedInstace == nullptr)
	{
		// Create a new bridge instance
		BinaryType binType;
		if((binType = GetPluginBinaryType(pluginPath)) == binUnknown)
		{
			return false;
		}

		const mpt::PathString exeName = theApp.GetAppDirPath() + (binType == bin64Bit ? MPT_PATHSTRING("PluginBridge64.exe") : MPT_PATHSTRING("PluginBridge32.exe"));

		// First, check for validity of the bridge executable.
		static uint64 mptVersion = 0;
		if(!mptVersion)
		{
			WCHAR exePath[_MAX_PATH];
			GetModuleFileNameW(0, exePath, CountOf(exePath));
			mpt::String::SetNullTerminator(exePath);
			mptVersion = GetFileVersion(exePath);
		}
		uint64 bridgeVersion = GetFileVersion(exeName.AsNative().c_str());
		if(bridgeVersion == 0)
		{
			// Silently fail if bridge is missing.
			throw BridgeNotFoundException();
		} else if(bridgeVersion != mptVersion)
		{
			throw BridgeException("The plugin bridge version does not match your OpenMPT version.");
			return nullptr;
		}

		otherPtrSize = binType;

		// Command-line must be a modfiable string...
		wchar_t cmdLine[128];
		swprintf(cmdLine, CountOf(cmdLine), L"%s %d", mapName.c_str(), procId);

		STARTUPINFOW info;
		MemsetZero(info);
		info.cb = sizeof(info);
		PROCESS_INFORMATION processInfo;
		MemsetZero(processInfo);

		if(!CreateProcessW(exeName.AsNative().c_str(), cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &info, &processInfo))
		{
			throw BridgeException("Failed to launch plugin bridge.");
		}
		otherProcess = processInfo.hProcess;
	} else
	{
		// Re-use existing bridge instance
		otherPtrSize = sharedInstace->otherPtrSize;
		otherProcess.DuplicateFrom(sharedInstace->otherProcess);

		BridgeMessage msg;
		msg.NewInstance(mapName.c_str());
		if(!sharedInstace->SendToBridge(msg))
		{
			// Something went wrong, try a new instance
			return Init(pluginPath, nullptr);
		}
	}

	// Initialize bridge
	sharedMem->effect.object = this;
	sharedMem->effect.dispatcher = DispatchToPlugin;
	sharedMem->effect.setParameter = SetParameter;
	sharedMem->effect.getParameter = GetParameter;
	sharedMem->effect.process = Process;
	memcpy(&(sharedMem->effect.resvd2), "OMPT", 4);

	sigThreadExit.Create(true);
	sigAutomation.Create(true);

	otherThread = mpt::thread_member<BridgeWrapper, &BridgeWrapper::MessageThread>(this);

	BridgeMessage initMsg;
	initMsg.Init(pluginPath.ToWide().c_str(), MIXBUFFERSIZE, ExceptionHandler::fullMemDump);

	if(!SendToBridge(initMsg))
	{
		throw BridgeException("Could not initialize plugin bridge, it probably crashed.");
	} else if(initMsg.init.result != 1)
	{
		throw BridgeException(mpt::ToLocale(initMsg.init.str).c_str());
	} else
	{
		if(sharedMem->effect.flags & effFlagsCanReplacing) sharedMem->effect.processReplacing = ProcessReplacing;
		if(sharedMem->effect.flags & effFlagsCanDoubleReplacing) sharedMem->effect.processDoubleReplacing = ProcessDoubleReplacing;
		return true;
	}

	return false;
}


BridgeWrapper::~BridgeWrapper()
{
	SignalObjectAndWait(sigThreadExit, otherThread, INFINITE, FALSE);
}


void BridgeWrapper::MessageThread()
{
	msgThreadID = GetCurrentThreadId();

	const HANDLE objects[] = { sigToHost.send, sigToBridge.ack, otherProcess, sigThreadExit };
	DWORD result = 0;
	do
	{
		result = WaitForMultipleObjects(CountOf(objects), objects, FALSE, INFINITE);
		if(result == WAIT_OBJECT_0)
		{
			ParseNextMessage();
		} else if(result == WAIT_OBJECT_0 + 1)
		{
			// Message got answered
			for(size_t i = 0; i < CountOf(sharedMem->toBridge); i++)
			{
				BridgeMessage &msg = sharedMem->toBridge[i];
				if(InterlockedCompareExchange(&msg.header.status, MsgHeader::delivered, MsgHeader::done) == MsgHeader::done)
				{
					ackSignals[msg.header.signalID].Confirm();
				}
			}
		}
	} while(result != WAIT_OBJECT_0 + 2 && result != WAIT_OBJECT_0 + 3 && result != WAIT_FAILED);

	// Close any possible waiting queries
	for(size_t i = 0; i < CountOf(ackSignals); i++)
	{
		ackSignals[i].Send();
	}
}


// Send an arbitrary message to the bridge.
// Returns a pointer to the message, as processed by the bridge.
bool BridgeWrapper::SendToBridge(BridgeMessage &sendMsg)
{
	const bool inMsgThread = GetCurrentThreadId() == msgThreadID;
	BridgeMessage *addr = CopyToSharedMemory(sendMsg, sharedMem->toBridge);
	if(addr == nullptr)
	{
		return false;
	}
	sigToBridge.Send();

	// Wait until we get the result from the bridge.
	DWORD result;
	if(inMsgThread)
	{
		// Since this is the message thread, we must handle messages directly.
		const HANDLE objects[] = { sigToBridge.ack, sigToHost.send, otherProcess, sigThreadExit };
		do
		{
			result = WaitForMultipleObjects(CountOf(objects), objects, FALSE, INFINITE);
			if(result == WAIT_OBJECT_0)
			{
				// Message got answered
				bool done = false;
				for(size_t i = 0; i < CountOf(sharedMem->toBridge); i++)
				{
					BridgeMessage &msg = sharedMem->toBridge[i];
					if(InterlockedCompareExchange(&msg.header.status, MsgHeader::delivered, MsgHeader::done) == MsgHeader::done)
					{
						if(&msg != addr)
						{
							ackSignals[msg.header.signalID].Confirm();
						} else
						{
							// This is our message!
							addr->CopyFromSharedMemory(sendMsg);
							done = true;
						}
					}
				}
				if(done)
				{
					break;
				}
			} else if(result == WAIT_OBJECT_0 + 1)
			{
				ParseNextMessage();
			}
		} while(result != WAIT_OBJECT_0 + 2 && result != WAIT_OBJECT_0 + 3 && result != WAIT_FAILED);
		if(result == WAIT_OBJECT_0 + 2)
		{
			sigThreadExit.Trigger();
		}
	} else
	{
		// Wait until the message thread notifies us.
		Signal &ackHandle = ackSignals[addr->header.signalID];
		const HANDLE objects[] = { ackHandle.ack, ackHandle.send, otherProcess };
		result = WaitForMultipleObjects(CountOf(objects), objects, FALSE, INFINITE);
		addr->CopyFromSharedMemory(sendMsg);

		// Bridge caught an exception while processing this request
		if(sendMsg.header.type == MsgHeader::exceptionMsg)
		{
			throw BridgeException();
		}

	}

	return (result == WAIT_OBJECT_0);
}


// Receive a message from the host and translate it.
void BridgeWrapper::ParseNextMessage()
{
	ASSERT(GetCurrentThreadId() == msgThreadID);

	BridgeMessage *msg = &sharedMem->toHost[0];
	for(size_t i = 0; i < CountOf(sharedMem->toHost); i++, msg++)
	{
		if(InterlockedCompareExchange(&msg->header.status, MsgHeader::received, MsgHeader::sent) == MsgHeader::sent)
		{
			switch(msg->header.type)
			{
			case MsgHeader::dispatch:
				DispatchToHost(&msg->dispatch);
				break;

			case MsgHeader::errorMsg:
				// TODO Showing a message box here will deadlock as the main thread can be in a waiting state
				//throw BridgeErrorException(msg->error.str);
				break;
			}

			InterlockedExchange(&msg->header.status, MsgHeader::done);
			sigToHost.Confirm();
		}
	}
}


void BridgeWrapper::DispatchToHost(DispatchMsg *msg)
{
	// Various dispatch data - depending on the opcode, one of those might be used.
	std::vector<char> extraData;

	MappedMemory auxMem;

	// Content of ptr is usually stored right after the message header, ptr field indicates size.
	void *ptr = (msg->ptr != 0) ? (msg + 1) : nullptr;
	if(msg->size > sizeof(BridgeMessage))
	{
		if(!auxMem.Open(static_cast<const wchar_t *>(ptr)))
		{
			return;
		}
		ptr = auxMem.view;
	}

	switch(msg->opcode)
	{
	case audioMasterProcessEvents:
		// VstEvents* in [ptr]
		TranslateBridgeToVSTEvents(extraData, ptr);
		ptr = &extraData[0];
		break;

	case audioMasterVendorSpecific:
		if(msg->index != kVendorOpenMPT || msg->value != kUpdateProcessingBuffer)
		{
			break;
		}
		MPT_FALLTHROUGH;
	case audioMasterIOChanged:
		// Set up new processing file
		processMem.Open(reinterpret_cast<wchar_t *>(ptr));
		break;

	case audioMasterOpenFileSelector:
	case audioMasterCloseFileSelector:
		// TODO: Translate the structs
		msg->result = 0;
		return;
	}

	VstIntPtr result = CVstPluginManager::MasterCallBack(&sharedMem->effect, msg->opcode, msg->index, static_cast<VstIntPtr>(msg->value), ptr, msg->opt);
	msg->result = static_cast<int32>(result);

	// Post-fix some opcodes
	switch(msg->opcode)
	{
	case audioMasterGetTime:
		// VstTimeInfo* in [return value]
		if(msg->result != 0)
		{
			MemCopy(sharedMem->timeInfo, *FromVstPtr<VstTimeInfo>(result));
		}
		break;

	case audioMasterGetDirectory:
		// char* in [return value]
		if(msg->result != 0)
		{
			char *target = static_cast<char *>(ptr);
			strncpy(target, FromVstPtr<const char>(result), static_cast<size_t>(msg->ptr - 1));
			target[msg->ptr - 1] = 0;
		}
		break;
	}
}


VstIntPtr VSTCALLBACK BridgeWrapper::DispatchToPlugin(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	if(that == nullptr)
	{
		return 0;
	}

	std::vector<char> dispatchData(sizeof(DispatchMsg), 0);
	int64_t ptrOut = 0;
	bool copyPtrBack = false, ptrIsSize = true;
	char *ptrC = static_cast<char *>(ptr);

	switch(opcode)
	{
	case effGetProgramName:
	case effGetParamLabel:
	case effGetParamDisplay:
	case effGetParamName:
	case effString2Parameter:
	case effGetProgramNameIndexed:
	case effGetEffectName:
	case effGetErrorText:
	case effGetVendorString:
	case effGetProductString:
	case effShellGetNextPlugin:
		// Name in [ptr]
		ptrOut = 256;
		copyPtrBack = true;
		break;

	case effSetProgramName:
	case effCanDo:
		// char* in [ptr]
		ptrOut = strlen(ptrC) + 1;
		dispatchData.insert(dispatchData.end(), ptrC, ptrC + ptrOut);
		break;

	case effIdle:
		// The plugin bridge will generate these messages by itself
		break;

	case effEditGetRect:
		// ERect** in [ptr]
		ptrOut = sizeof(ERect);
		copyPtrBack = true;
		break;

	case effEditOpen:
		// HWND in [ptr] - Note: Window handles are interoperable between 32-bit and 64-bit applications in Windows (http://msdn.microsoft.com/en-us/library/windows/desktop/aa384203%28v=vs.85%29.aspx)
		ptrOut = reinterpret_cast<int64_t>(ptr);
		ptrIsSize = false;
		break;

	case effEditIdle:
		// The plugin bridge will generate these messages by itself
		return 0;

	case effGetChunk:
		// void** in [ptr] for chunk data address
		{
			static uint32_t chunkId = 0;
			const std::wstring mapName = L"Local\\openmpt-" + mpt::ToWString(GetCurrentProcessId()) + L"-chunkdata-" + mpt::ToWString(chunkId++);
			ptrOut = (mapName.length() + 1) * sizeof(wchar_t);
			PushToVector(dispatchData, *mapName.c_str(), static_cast<size_t>(ptrOut));
		}
		break;

	case effSetChunk:
		// void* in [ptr] for chunk data
		ptrOut = value;
		dispatchData.insert(dispatchData.end(), ptrC, ptrC + value);
		break;

	case effProcessEvents:
		// VstEvents* in [ptr]
		// We process in a separate memory segment to save a bridge communication message.
		{
			std::vector<char> events;
			TranslateVSTEventsToBridge(events, static_cast<VstEvents *>(ptr), that->otherPtrSize);
			if(that->eventMem.Size() < events.size())
			{
				// Resize memory
				static uint32_t chunkId = 0;
				const std::wstring mapName = L"Local\\openmpt-" + mpt::ToWString(GetCurrentProcessId()) + L"-events-" + mpt::ToWString(chunkId++);
				ptrOut = (mapName.length() + 1) * sizeof(wchar_t);
				PushToVector(dispatchData, *mapName.c_str(), static_cast<size_t>(ptrOut));
				that->eventMem.Create(mapName.c_str(), static_cast<uint32_t>(events.size() + 1024));

				opcode = effVendorSpecific;
				index = kVendorOpenMPT;
				value = kUpdateEventMemName;
			}
			memcpy(that->eventMem.view, &events[0], events.size());
		}
		if(opcode != effVendorSpecific)
		{
			return 1;
		}
		break;

	case effGetInputProperties:
	case effGetOutputProperties:
		// VstPinProperties* in [ptr]
		ptrOut = sizeof(VstPinProperties);
		copyPtrBack = true;
		break;

	case effOfflineNotify:
		// VstAudioFile* in [ptr]
		ptrOut = sizeof(VstAudioFile) * value;
		// TODO
		return 0;
		break;

	case effOfflinePrepare:
	case effOfflineRun:
		// VstOfflineTask* in [ptr]
		ptrOut = sizeof(VstOfflineTask) * value;
		// TODO
		return 0;
		break;

	case effProcessVarIo:
		// VstVariableIo* in [ptr]
		ptrOut = sizeof(VstVariableIo);
		// TODO
		return 0;
		break;

	case effSetSpeakerArrangement:
		// VstSpeakerArrangement* in [value] and [ptr]
		ptrOut = sizeof(VstSpeakerArrangement) * 2;
		PushToVector(dispatchData, *static_cast<VstSpeakerArrangement *>(ptr));
		PushToVector(dispatchData, *FromVstPtr<VstSpeakerArrangement>(value));
		break;

	case effVendorSpecific:
		if(index == kVendorOpenMPT && value == kGetWrapperPointer)
		{
			return ToVstPtr<BridgeWrapper>(that);
		}
		break;

	case effGetParameterProperties:
		// VstParameterProperties* in [ptr]
		ptrOut = sizeof(VstParameterProperties);
		copyPtrBack = true;
		break;

	case effGetMidiProgramName:
	case effGetCurrentMidiProgram:
		// MidiProgramName* in [ptr]
		ptrOut = sizeof(MidiProgramName);
		copyPtrBack = true;
		break;

	case effGetMidiProgramCategory:
		// MidiProgramCategory* in [ptr]
		ptrOut = sizeof(MidiProgramCategory);
		copyPtrBack = true;
		break;

	case effGetMidiKeyName:
		// MidiKeyName* in [ptr]
		ptrOut = sizeof(MidiKeyName);
		copyPtrBack = true;
		break;

	case effBeginSetProgram:
		that->isSettingProgram = true;
		break;

	case effEndSetProgram:
		that->isSettingProgram = false;
		if(that->sharedMem->automationQueue.pendingEvents)
		{
			that->SendAutomationQueue();
		}
		break;

	case effGetSpeakerArrangement:
		// VstSpeakerArrangement* in [value] and [ptr]
		ptrOut = sizeof(VstSpeakerArrangement) * 2;
		copyPtrBack = true;
		break;

	case effBeginLoadBank:
	case effBeginLoadProgram:
		// VstPatchChunkInfo* in [ptr]
		ptrOut = sizeof(VstPatchChunkInfo);
		break;

	default:
		ASSERT(ptr == nullptr);
	}

	if(ptrOut != 0 && ptrIsSize)
	{
		// In case we only reserve space and don't copy stuff over...
		dispatchData.resize(sizeof(DispatchMsg) + static_cast<size_t>(ptrOut), 0);
	}

	uint32_t extraSize = static_cast<uint32_t>(dispatchData.size() - sizeof(DispatchMsg));
	
	// Create message header
	BridgeMessage *msg = reinterpret_cast<BridgeMessage *>(&dispatchData[0]);
	msg->Dispatch(opcode, index, value, ptrOut, opt, extraSize);

	const bool useAuxMem = dispatchData.size() > sizeof(BridgeMessage);
	MappedMemory auxMem;
	if(useAuxMem)
	{
		// Extra data doesn't fit in message - use secondary memory
		wchar_t auxMemName[64];
		static_assert(sizeof(DispatchMsg) + sizeof(auxMemName) <= sizeof(BridgeMessage), "Check message sizes, this will crash!");
		swprintf(auxMemName, CountOf(auxMemName), L"Local\\openmpt-%d-auxmem-%d", GetCurrentProcessId(), GetCurrentThreadId());
		if(auxMem.Create(auxMemName, extraSize))
		{
			// Move message data to shared memory and then move shared memory name to message data
			memcpy(auxMem.view, &dispatchData[sizeof(DispatchMsg)], extraSize);
			memcpy(&dispatchData[sizeof(DispatchMsg)], auxMemName, sizeof(auxMemName));
		} else
		{
			return 0;
		}
	}

	//std::cout << "about to dispatch " << opcode << " to host...";
	//std::flush(std::cout);
	if(!that->SendToBridge(*msg) && opcode != effClose)
	{
		return 0;
	}
	//std::cout << "done." << std::endl;
	const DispatchMsg *resultMsg = &msg->dispatch;

	const char *extraData = useAuxMem ? static_cast<const char *>(auxMem.view) : reinterpret_cast<const char *>(resultMsg + 1);
	// Post-fix some opcodes
	switch(opcode)
	{
	case effClose:
		effect->object = nullptr;
		delete that;
		return 0;

	case effGetProgramName:
	case effGetParamLabel:
	case effGetParamDisplay:
	case effGetParamName:
	case effString2Parameter:
	case effGetProgramNameIndexed:
	case effGetEffectName:
	case effGetErrorText:
	case effGetVendorString:
	case effGetProductString:
	case effShellGetNextPlugin:
		// Name in [ptr]
		strcpy(ptrC, extraData);
		break;

	case effEditGetRect:
		// ERect** in [ptr]
		that->editRect = *reinterpret_cast<const ERect *>(extraData);
		*static_cast<const ERect **>(ptr) = &that->editRect;
		break;

	case effGetChunk:
		// void** in [ptr] for chunk data address
		{
			const wchar_t *str = reinterpret_cast<const wchar_t *>(extraData);
			if(that->getChunkMem.Open(str))
			{
				*static_cast<void **>(ptr) = that->getChunkMem.view;
			} else
			{
				return 0;
			}
		}
		break;

	case effGetSpeakerArrangement:
		// VstSpeakerArrangement* in [value] and [ptr]
		that->speakers[0] = *reinterpret_cast<const VstSpeakerArrangement *>(extraData);
		that->speakers[1] = *(reinterpret_cast<const VstSpeakerArrangement *>(extraData) + 1);
		*static_cast<VstSpeakerArrangement *>(ptr) = that->speakers[0];
		*FromVstPtr<VstSpeakerArrangement>(value) = that->speakers[1];
		break;

	default:
		// TODO: Translate VstVariableIo, offline tasks
		if(copyPtrBack)
		{
			memcpy(ptr, extraData, static_cast<size_t>(ptrOut));
		}
	}

	return static_cast<VstIntPtr>(resultMsg->result);
}


// Send any pending automation events
void BridgeWrapper::SendAutomationQueue()
{
	sigAutomation.Reset();
	BridgeMessage msg;
	msg.Automate();
	if(!SendToBridge(msg))
	{
		// Failed (plugin probably crashed) - auto-fix event count
		sharedMem->automationQueue.pendingEvents = 0;
	}
	sigAutomation.Trigger();

}

void VSTCALLBACK BridgeWrapper::SetParameter(AEffect *effect, VstInt32 index, float parameter)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	const CVstPlugin *plug = FromVstPtr<CVstPlugin>(effect->resvd1);
	if(that)
	{
		AutomationQueue &autoQueue = that->sharedMem->automationQueue;
		if(that->isSettingProgram || (plug && plug->IsSongPlaying()))
		{
			// Queue up messages while rendering to reduce latency introduced by every single bridge call
			uint32_t i;
			while((i = InterlockedExchangeAdd(&autoQueue.pendingEvents, 1)) >= CountOf(autoQueue.params))
			{
				// Queue full!
				if(i == CountOf(autoQueue.params))
				{
					// We're the first to notice that it's full
					that->SendAutomationQueue();
				} else
				{
					// Wait until queue is emptied by someone else (this branch is very unlikely to happen)
					WaitForSingleObject(that->sigAutomation, INFINITE);
				}
			}

			autoQueue.params[i].index = index;
			autoQueue.params[i].value = parameter;
			return;
		} else if(autoQueue.pendingEvents)
		{
			// Actually, this should never happen as pending events are cleared before processing and at the end of a set program event.
			that->SendAutomationQueue();
		}

		BridgeMessage msg;
		msg.SetParameter(index, parameter);
		that->SendToBridge(msg);
	}
}


float VSTCALLBACK BridgeWrapper::GetParameter(AEffect *effect, VstInt32 index)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	if(that)
	{
		BridgeMessage msg;
		msg.GetParameter(index);
		if(that->SendToBridge(msg))
		{
			return msg.parameter.value;
		}
	}
	return 0.0f;
}


void VSTCALLBACK BridgeWrapper::Process(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleFrames)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	if(sampleFrames != 0 && that != nullptr)
	{
		that->BuildProcessBuffer(ProcessMsg::process, effect->numInputs, effect->numOutputs, inputs, outputs, sampleFrames);
	}
}


void VSTCALLBACK BridgeWrapper::ProcessReplacing(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleFrames)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	if(sampleFrames != 0 && that != nullptr)
	{
		that->BuildProcessBuffer(ProcessMsg::processReplacing, effect->numInputs, effect->numOutputs, inputs, outputs, sampleFrames);
	}
}


void VSTCALLBACK BridgeWrapper::ProcessDoubleReplacing(AEffect *effect, double **inputs, double **outputs, VstInt32 sampleFrames)
{
	BridgeWrapper *that = static_cast<BridgeWrapper *>(effect->object);
	if(sampleFrames != 0 && that != nullptr)
	{
		that->BuildProcessBuffer(ProcessMsg::processDoubleReplacing, effect->numInputs, effect->numOutputs, inputs, outputs, sampleFrames);
	}
}


template<typename buf_t>
void BridgeWrapper::BuildProcessBuffer(ProcessMsg::ProcessType type, VstInt32 numInputs, VstInt32 numOutputs, buf_t **inputs, buf_t **outputs, VstInt32 sampleFrames)
{
	if(!processMem.Good())
	{
		ASSERT(false);
		return;
	}

	ProcessMsg *msg = static_cast<ProcessMsg *>(processMem.view);
	const VstInt32 timeInfoFlags = sharedMem->timeInfo.flags;
	new (msg) ProcessMsg(type, numInputs, numOutputs, sampleFrames);

	// Plugin asked for time info in the past (flags are set), so we anticipate that it will do that again
	// and cache the time info so that plugin doesn't have to ask for it.
	if(timeInfoFlags != 0)
	{
		sharedMem->timeInfo = *reinterpret_cast<VstTimeInfo *>(CVstPluginManager::MasterCallBack(&sharedMem->effect, audioMasterGetTime, 0, timeInfoFlags, nullptr, 0.0f));
	}

	buf_t *ptr = reinterpret_cast<buf_t *>(msg + 1);
	for(VstInt32 i = 0; i < numInputs; i++)
	{
		memcpy(ptr, inputs[i], sampleFrames * sizeof(buf_t));
		ptr += sampleFrames;
	}
	// Theoretically, we should memcpy() instead of memset() here in process(), but OpenMPT always clears the output buffer before processing so it doesn't matter.
	memset(ptr, 0, numOutputs * sampleFrames * sizeof(buf_t));

	sigProcess.Send();
	const HANDLE objects[] = { sigProcess.ack, otherProcess };
	WaitForMultipleObjects(CountOf(objects), objects, FALSE, INFINITE);

	for(VstInt32 i = 0; i < numOutputs; i++)
	{
		//memcpy(outputs[i], ptr, sampleFrames * sizeof(buf_t));
		outputs[i] = ptr;	// Exactly what you don't want plugins to do usually (bend your output pointers)... muahahaha!
		ptr += sampleFrames;
	}
}

#endif //NO_VST


OPENMPT_NAMESPACE_END
