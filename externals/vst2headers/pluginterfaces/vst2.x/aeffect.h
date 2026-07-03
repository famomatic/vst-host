//-----------------------------------------------------------------------------
// VST 2.x SDK header aeffect.h ( Steinberg VST 2.4 compatible interface )
//
// Canonical AEffect / dispatcher interface used by every VST2.4 plug-in and
// host. JUCE's juce_VSTPluginFormat #includes <pluginterfaces/vst2.x/aeffect.h>
// and expects these exact symbols. Replaces the incompatible Xaymar header.
//-----------------------------------------------------------------------------
#ifndef __aeffect__
#define __aeffect__

#include <stdint.h>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    #define VSTCALLBACK __cdecl
#else
    #define VSTCALLBACK
#endif

typedef int32_t  VstInt32;
typedef intptr_t VstIntPtr;

//-------------------------------------------------------------------------------------------------------
struct AEffect;

typedef VstIntPtr (VSTCALLBACK *audioMasterCallback)(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (VSTCALLBACK *dispatcherPtr)(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef void      (VSTCALLBACK *processProc)(AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void      (VSTCALLBACK *setParameterProc)(AEffect* effect, VstInt32 index, float parameter);
typedef float     (VSTCALLBACK *getParameterProc)(AEffect* effect, VstInt32 index);
typedef void      (VSTCALLBACK *processDoubleProc)(AEffect* effect, double** inputs, double** outputs, VstInt32 sampleFrames);

//-------------------------------------------------------------------------------------------------------
struct AEffect
{
    VstInt32 magic;

    dispatcherPtr dispatcher;
    processProc   process;

    setParameterProc setParameter;
    getParameterProc getParameter;

    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;

    VstInt32 flags;

    void* resvd1;
    VstIntPtr resvd2;

    VstInt32 initialDelay;
    VstInt32 realQualities;
    VstInt32 offQualities;
    float    ioRatio;

    void* object;
    void* user;

    VstInt32 uniqueID;
    VstInt32 version;

    processProc       processReplacing;
    processDoubleProc processDoubleReplacing;

    char future[56];
};

//-------------------------------------------------------------------------------------------------------
#define kEffectMagic 0x56737450      // 'VstP'

//-------------------------------------------------------------------------------------------------------
// AEffect flags. JUCE qualifies these as Vst2::effFlagsHasEditor, so they
// must be enum values (not macros, which would break namespace qualification).
enum VstAEffectFlags
{
    effFlagsHasEditor          = (1 << 0),
    effFlagsIsSynth            = (1 << 1),
    effFlagsNoSoundInStop      = (1 << 2),
    effFlagsIsExtension        = (1 << 3),
    effFlagsCanDoubleReplacing = (1 << 4),
    effFlagsCanReplacing       = effFlagsCanDoubleReplacing,
    effFlagsProgramChunks       = (1 << 5)
};

//-------------------------------------------------------------------------------------------------------
// AEffect dispatcher opcodes (single consolidated enum, canonical VST2.4 values)
enum VstAEffectOpcodes
{
    effOpen               = 0,
    effClose              = 1,
    effSetProgram         = 2,
    effGetProgram         = 3,
    effSetProgramName     = 4,
    effGetProgramName     = 5,
    effGetParamLabel      = 6,
    effGetParamDisplay    = 7,
    effGetParamName       = 8,
    effGetVu              = 9,
    effSetSampleRate      = 10,
    effSetBlockSize       = 11,
    effMainsChanged       = 12,
    effEditGetRect         = 13,
    effEditOpen            = 14,
    effEditClose           = 15,
    effEditDraw            = 16,
    effEditMouse           = 17,
    effEditKey             = 18,
    effEditIdle            = 19,
    effEditTop             = 20,
    effEditSleep           = 21,
    effEditIdentify        = 22,
    effGetChunk            = 23,
    effSetChunk            = 24,
    effProcessEvents       = 25,
    effCanBeAutomated      = 26,
    effString2Parameter    = 27,
    effSetBypass           = 28,
    effGetProgramNameIndexed = 30,
    effGetInputProperties  = 33,
    effGetOutputProperties = 34,
    effGetPlugCategory     = 35,
    effGetCurrentPosition  = 36,
    effGetDestinationBuffer = 37,
    effGetEffectName       = 45,
    effIdentify            = 22,
    effGetVendorString     = 47,
    effGetProductString    = 48,
    effGetVendorVersion    = 49,
    effVendorSpecific      = 50,
    effCanDo               = 51,
    effGetTailSize         = 52,
    effIdle                = 53,
    effGetIcon             = 54,
    effSetIcon             = 55,
    effGetParameterProperties = 56,
    effKeysRequired        = 57,
    effGetVstVersion       = 58,
    effGetMidiKeyName      = 66,
    effShellGetNextPlugin  = 70,
    effGetSpeakerArrangement = 69,
    effSetSpeakerArrangement = 68,
    effStartProcess        = 71,
    effStopProcess         = 72,
    effSetProcessPrecision = 77,
    effGetNumMidiInputChannels  = 78,
    effGetNumMidiOutputChannels = 79,
    effConnectInput        = 95,
    effConnectOutput       = 96
};

//-------------------------------------------------------------------------------------------------------
// audioMaster opcodes. Canonical VST2.4 numbering; JUCE references the
// unprefixed names (audioMasterUpdateDisplay, audioMasterGetLanguage, ...).
// Multiple names may share a value (canonical Steinberg behaviour).
enum VstAEffectXOpcodes
{
    audioMasterAutomate                    = 0,
    audioMasterVersion                      = 1,
    audioMasterCurrentId                    = 2,
    audioMasterIdle                         = 3,
    audioMasterPinConnected                 = 4,
    audioMasterWantMidi                     = 6,
    audioMasterGetTime                      = 7,
    audioMasterProcessEvents                = 8,
    audioMasterSetTime                      = 10,
    audioMasterTempoAt                      = 11,
    audioMasterGetNumAutomatableParameters  = 12,
    audioMasterGetParameterQuantization     = 13,
    audioMasterIOChanged                    = 15,
    audioMasterNeedIdle                     = 16,
    audioMasterSizeWindow                   = 17,
    audioMasterGetSampleRate                = 18,
    audioMasterGetBlockSize                 = 19,
    audioMasterGetInputLatency              = 20,
    audioMasterGetOutputLatency             = 21,
    audioMasterGetPreviousPlug              = 23,
    audioMasterGetNextPlug                  = 24,
    audioMasterWillReplaceOrAccumulate      = 25,
    audioMasterGetCurrentProcessLevel      = 26,
    audioMasterGetAutomationState          = 27,
    audioMasterOfflineStart                = 29,
    audioMasterOfflineRead                 = 30,
    audioMasterOfflineWrite                = 31,
    audioMasterOfflineGetCurrentPass      = 32,
    audioMasterOfflineGetCurrentMetaPass   = 33,
    audioMasterGetVendorString             = 36,
    audioMasterGetProductString            = 37,
    audioMasterGetVendorVersion            = 38,
    audioMasterCanDo                       = 39,
    audioMasterGetLanguage                 = 40,
    audioMasterGetDirectory                = 41,
    audioMasterUpdateDisplay               = 42,
    audioMasterSetIcon                     = 50,
    audioMasterOpenWindow                  = 60,
    audioMasterCloseWindow                 = 61,
    audioMasterSetOutputSampleRate          = 62,
    audioMasterBeginEdit                    = 63,
    audioMasterEndEdit                       = 64,
    audioMasterVendorSpecific              = 99,
    audioMasterGetOutputSpeakerArrangement = 46
};

#endif // __aeffect__
