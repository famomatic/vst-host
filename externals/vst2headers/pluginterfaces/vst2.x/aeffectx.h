//-----------------------------------------------------------------------------
// VST 2.x SDK header aeffectx.h ( Steinberg VST 2.4 compatible interface )
//
// Extended constants, structs (ERect, VstTimeInfo, speaker arrangements, plug
// categories, SMPTE rates) used by VST2.4 hosts. JUCE's juce_VSTPluginFormat
// #includes <pluginterfaces/vst2.x/aeffectx.h> and expects these exact symbols.
// Replaces the incompatible Xaymar header.
//-----------------------------------------------------------------------------
#ifndef __aeffectx__
#define __aeffectx__

#include "aeffect.h"

//-------------------------------------------------------------------------------------------------------
// string length limits (enum so Vst2::kVstMaxNameLen qualification works)
enum
{
    kVstMaxNameLen      = 64,
    kVstMaxProductStrLen = 64,
    kVstMaxVendorStrLen  = 64
};

//-------------------------------------------------------------------------------------------------------
// pin properties
enum
{
    kVstPinIsActive    = (1 << 0),
    kVstPinIsStereo    = (1 << 1),
    kVstPinUseSpeaker  = (1 << 2)
};

//-------------------------------------------------------------------------------------------------------
struct VstPinProperties
{
    char label[64];
    VstInt32 flags;
    char shortLabel[8];
    int32_t arrangementType;
    char future[48];
};

//-------------------------------------------------------------------------------------------------------
struct VstSpeakerProperties
{
    float azimuth;
    float elevation;
    float radius;
    float reserved;
    char name[64];
    VstInt32 type;
    char future[28];
};

//-------------------------------------------------------------------------------------------------------
struct VstSpeakerArrangement
{
    VstInt32 type;
    VstInt32 numChannels;
    VstSpeakerProperties speakers[8];
};

//-------------------------------------------------------------------------------------------------------
// speaker types
enum VstSpeakerType
{
    kSpeakerM     = 0x7FFFFFFF, // undefined
    kSpeakerL     = 0,
    kSpeakerR     = 1,
    kSpeakerC     = 2,
    kSpeakerLfe   = 3,
    kSpeakerLs    = 4,
    kSpeakerRs    = 5,
    kSpeakerLc    = 6,
    kSpeakerRc    = 7,
    kSpeakerS     = 8,
    kSpeakerSl    = 9,
    kSpeakerSr    = 10,
    kSpeakerTm    = 11,
    kSpeakerTfl   = 12,
    kSpeakerTfc   = 13,
    kSpeakerTfr   = 14,
    kSpeakerTrl   = 15,
    kSpeakerTrc   = 16,
    kSpeakerTrr   = 17,
    kSpeakerLfe2  = 18
};

//-------------------------------------------------------------------------------------------------------
// speaker arrangement types
enum VstSpeakerArrangementType
{
    kSpeakerArrUserDefined       = -2,
    kSpeakerArrEmpty             = -1,
    kSpeakerArrMono             = 0,
    kSpeakerArrStereo,
    kSpeakerArrStereoSurround,
    kSpeakerArrStereoCenter,
    kSpeakerArrStereoSide,
    kSpeakerArrStereoCLfe,
    kSpeakerArr30Cine,
    kSpeakerArr30Music,
    kSpeakerArr31Cine,
    kSpeakerArr31Music,
    kSpeakerArr40Cine,
    kSpeakerArr40Music,
    kSpeakerArr41Cine,
    kSpeakerArr41Music,
    kSpeakerArr50,
    kSpeakerArr51,
    kSpeakerArr60Cine,
    kSpeakerArr60Music,
    kSpeakerArr61Cine,
    kSpeakerArr61Music,
    kSpeakerArr70Cine,
    kSpeakerArr70Music,
    kSpeakerArr71Cine,
    kSpeakerArr71Music,
    kSpeakerArr80Cine,
    kSpeakerArr80Music,
    kSpeakerArr81Cine,
    kSpeakerArr81Music,
    kSpeakerArr102,
    kSpeakerNumArrangements
};

//-------------------------------------------------------------------------------------------------------
// plug category
enum VstPlugCategory
{
    kPlugCategUnknown       = 0,
    kPlugCategEffect,
    kPlugCategSynth,
    kPlugCategAnalysis,
    kPlugCategMastering,
    kPlugCategSpacializer,
    kPlugCategRoomFx,
    kPlugSurroundFx,
    kPlugCategRestoration,
    kPlugCategOfflineProcess,
    kPlugCategShell,
    kPlugCategGenerator,
    kPlugCategMaxCount
};

//-------------------------------------------------------------------------------------------------------
// process precision
enum VstProcessPrecision
{
    kVstProcessPrecision32 = 0,
    kVstProcessPrecision64
};

//-------------------------------------------------------------------------------------------------------
// SMPTE frame rates
enum VstSmpteFrameRate
{
    kVstSmpte24fps    = 0,
    kVstSmpte25fps    = 1,
    kVstSmpte2997fps  = 2,
    kVstSmpte30fps    = 3,
    kVstSmpte2997dfps = 4,
    kVstSmpte30dfps   = 5,
    kVstSmpte239fps   = 6,
    kVstSmpte249fps   = 7,
    kVstSmpte599fps   = 8,
    kVstSmpte60fps    = 9
};

//-------------------------------------------------------------------------------------------------------
// Editor rectangle (effEditGetRect). Host passes ERect** and the plug-in
// writes a pointer to its own internal ERect into *ptr.
struct ERect
{
    short top;
    short left;
    short bottom;
    short right;
};

//-------------------------------------------------------------------------------------------------------
// time info flags
enum VstTimeInfoFlags
{
    kVstTransportChanged     = 1,
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,

    kVstAutomationWriting    = 1 << 6,
    kVstAutomationReading   = 1 << 7,

    kVstNanosValid          = 1 << 8,
    kVstPpqPosValid         = 1 << 9,
    kVstTempoValid          = 1 << 10,
    kVstBarsValid          = 1 << 11,
    kVstCyclePosValid      = 1 << 12,
    kVstTimeSigValid       = 1 << 13,
    kVstSmpteValid         = 1 << 14,
    kVstClockValid         = 1 << 15
};

//-------------------------------------------------------------------------------------------------------
// process levels
enum VstProcessLevels
{
    kVstProcessLevelUnknown  = 0,
    kVstProcessLevelUser,
    kVstProcessLevelRealtime,
    kVstProcessLevelPrefetch,
    kVstProcessLevelOffline
};

//-------------------------------------------------------------------------------------------------------
// automation states
enum VstAutomationStates
{
    kVstAutomationUnsupported = 0,
    kVstAutomationOff,
    kVstAutomationRead,
    kVstAutomationWrite,
    kVstAutomationReadWrite
};

//-------------------------------------------------------------------------------------------------------
// host language
enum VstHostLanguage
{
    kVstLangEnglish = 0,
    kVstLangGerman,
    kVstLangFrench,
    kVstLangItalian,
    kVstLangSpanish,
    kVstLangJapanese
};

//-------------------------------------------------------------------------------------------------------
// MIDI / SysEx event types
enum
{
    kVstMidiType    = 1,
    kVstSysExType   = 6
};

//-------------------------------------------------------------------------------------------------------
struct VstMidiEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 noteLength;
    VstInt32 noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};

//-------------------------------------------------------------------------------------------------------
struct VstMidiSysexEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 dumpBytes;
    void* resvd1;
    char* sysexDump;
    VstInt32 resvd2;
};

//-------------------------------------------------------------------------------------------------------
struct VstEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    char data[16];
};

//-------------------------------------------------------------------------------------------------------
struct VstEvents
{
    VstInt32 numEvents;
    void* reserved;
    VstEvent* events[2];
};

//-------------------------------------------------------------------------------------------------------
struct VstTimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    VstInt32 timeSigNumerator;
    VstInt32 timeSigDenominator;
    VstInt32 smpteOffset;
    VstInt32 smpteFrameRate;
    VstInt32 samplesToNextClock;
    VstInt32 flags;
};

//-------------------------------------------------------------------------------------------------------
struct MidiKeyName
{
    VstInt32 thisProgramIndex;
    VstInt32 thisKeyNumber;
    char keyName[64];
    char reserved[52];
};

//-------------------------------------------------------------------------------------------------------
// vendor / host canDo string constants. Declared as inline const char* so
// Vst2::kVstCanDoBypass qualification works and the value is a usable C-string.
inline const char* const kVstCanDoBypass              = "bypass";
inline const char* const kVstCanDoSendVstEvents       = "sendVstEvents";
inline const char* const kVstCanDoSendVstMidiEvent      = "sendVstMidiEvent";
inline const char* const kVstCanDoReceiveVstEvents     = "receiveVstEvents";
inline const char* const kVstCanDoReceiveVstMidiEvent  = "receiveVstMidiEvent";
inline const char* const kVstCanDoReceiveVstTimeInfo   = "receiveVstTimeInfo";
inline const char* const kVstCanDoOffline              = "offline";
inline const char* const kVstCanDoMidiProgramNames     = "midiProgramNames";
inline const char* const kVstCanDoSizeWindow           = "sizeWindow";


//-------------------------------------------------------------------------------------------------------
enum { kVstVersion = 2400 };

#endif // __aeffectx__
