// credits original for sse : https://github.com/Exit-9B/AutoAudioSwitch/tree/main

#include <Modules/AdModuleAudioSwitch.h>
#include <AdUtils.h>

#include <comdef.h>
#include <cstddef>
#include <functional>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <new>
#include <objbase.h>
#include <Windows.h>
#include <wrl/client.h>
#include <x3daudio.h>

#pragma comment(lib, "xaudio2.lib")

#undef ERROR
#undef MAX_PATH
#undef MEM_RELEASE

#include <RE/B/BSFixedString.h>
#include <RE/B/BSResource_ID.h>
#include <RE/N/NiPoint3.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSTHashMap.h>
#include <RE/N/NiPointer.h>
#include <RE/N/NiAVObject.h>
#include <RE/B/BSAudioManager.h>
#include <RE/B/BSTArray.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesAudioSwitch{ "Patches"sv, "bAudioSwitch"sv, true };

	// XAudio27
	namespace AudioSystem
	{
		namespace FX
		{
			// All structures defined in this file should use tight packing
			#pragma pack(push, 1)

			// XAUDIO2FX_REVERB_PARAMETERS: Native parameter set for the reverb effect
			typedef struct XAUDIO2FX_REVERB_PARAMETERS
			{
				// ratio of wet (processed) signal to dry (original) signal
				float WetDryMix;            // [0, 100] (percentage)

				// Delay times
				UINT32 ReflectionsDelay;    // [0, 300] in ms
				BYTE ReverbDelay;           // [0, 85] in ms
				BYTE RearDelay;             // [0, 5] in ms

				// Indexed parameters
				BYTE PositionLeft;          // [0, 30] no units
				BYTE PositionRight;         // [0, 30] no units, ignored when configured to mono
				BYTE PositionMatrixLeft;    // [0, 30] no units
				BYTE PositionMatrixRight;   // [0, 30] no units, ignored when configured to mono
				BYTE EarlyDiffusion;        // [0, 15] no units
				BYTE LateDiffusion;         // [0, 15] no units
				BYTE LowEQGain;             // [0, 12] no units
				BYTE LowEQCutoff;           // [0, 9] no units
				BYTE HighEQGain;            // [0, 8] no units
				BYTE HighEQCutoff;          // [0, 14] no units

				// Direct parameters
				float RoomFilterFreq;       // [20, 20000] in Hz
				float RoomFilterMain;       // [-100, 0] in dB
				float RoomFilterHF;         // [-100, 0] in dB
				float ReflectionsGain;      // [-100, 20] in dB
				float ReverbGain;           // [-100, 20] in dB
				float DecayTime;            // [0.1, inf] in seconds
				float Density;              // [0, 100] (percentage)
				float RoomSize;             // [1, 100] in feet
			} XAUDIO2FX_REVERB_PARAMETERS;

			// Maximum, minimum and default values for the parameters above

			constexpr static auto XAUDIO2FX_REVERB_MIN_WET_DRY_MIX = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_REFLECTIONS_DELAY = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_REVERB_DELAY = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_REAR_DELAY = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_POSITION = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_DIFFUSION = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_LOW_EQ_GAIN = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_LOW_EQ_CUTOFF = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_HIGH_EQ_GAIN = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_HIGH_EQ_CUTOFF = 0;
			constexpr static auto XAUDIO2FX_REVERB_MIN_ROOM_FILTER_FREQ = 20.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_ROOM_FILTER_MAIN = -100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_ROOM_FILTER_HF = -100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_REFLECTIONS_GAIN = -100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_REVERB_GAIN = -100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_DECAY_TIME = 0.1f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_DENSITY = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_MIN_ROOM_SIZE = 0.0f;

			constexpr static auto XAUDIO2FX_REVERB_MAX_WET_DRY_MIX = 100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY = 300;
			constexpr static auto XAUDIO2FX_REVERB_MAX_REVERB_DELAY = 85;
			constexpr static auto XAUDIO2FX_REVERB_MAX_REAR_DELAY = 5;
			constexpr static auto XAUDIO2FX_REVERB_MAX_POSITION = 30;
			constexpr static auto XAUDIO2FX_REVERB_MAX_DIFFUSION = 15;
			constexpr static auto XAUDIO2FX_REVERB_MAX_LOW_EQ_GAIN = 12;
			constexpr static auto XAUDIO2FX_REVERB_MAX_LOW_EQ_CUTOFF = 9;
			constexpr static auto XAUDIO2FX_REVERB_MAX_HIGH_EQ_GAIN = 8;
			constexpr static auto XAUDIO2FX_REVERB_MAX_HIGH_EQ_CUTOFF = 14;
			constexpr static auto XAUDIO2FX_REVERB_MAX_ROOM_FILTER_FREQ = 20000.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_ROOM_FILTER_MAIN = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_ROOM_FILTER_HF = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_REFLECTIONS_GAIN = 20.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_REVERB_GAIN = 20.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_DENSITY = 100.0f;
			constexpr static auto XAUDIO2FX_REVERB_MAX_ROOM_SIZE = 100.0f;

			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_WET_DRY_MIX = 100.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_REFLECTIONS_DELAY = 5;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_REVERB_DELAY = 5;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_REAR_DELAY = 5;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_POSITION = 6;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX = 27;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_EARLY_DIFFUSION = 8;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_LATE_DIFFUSION = 8;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_LOW_EQ_GAIN = 8;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_LOW_EQ_CUTOFF = 4;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_HIGH_EQ_GAIN = 8;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_HIGH_EQ_CUTOFF = 4;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_FREQ = 5000.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_MAIN = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_HF = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_REFLECTIONS_GAIN = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_REVERB_GAIN = 0.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_DECAY_TIME = 1.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_DENSITY = 100.0f;
			constexpr static auto XAUDIO2FX_REVERB_DEFAULT_ROOM_SIZE = 100.0f;

			// XAUDIO2FX_REVERB_I3DL2_PARAMETERS: Parameter set compliant with the I3DL2 standard
			typedef struct XAUDIO2FX_REVERB_I3DL2_PARAMETERS
			{
				// ratio of wet (processed) signal to dry (original) signal
				float WetDryMix;            // [0, 100] (percentage)

				// Standard I3DL2 parameters
				INT32 Room;                 // [-10000, 0] in mB (hundredths of decibels)
				INT32 RoomHF;               // [-10000, 0] in mB (hundredths of decibels)
				float RoomRolloffFactor;    // [0.0, 10.0]
				float DecayTime;            // [0.1, 20.0] in seconds
				float DecayHFRatio;         // [0.1, 2.0]
				INT32 Reflections;          // [-10000, 1000] in mB (hundredths of decibels)
				float ReflectionsDelay;     // [0.0, 0.3] in seconds
				INT32 Reverb;               // [-10000, 2000] in mB (hundredths of decibels)
				float ReverbDelay;          // [0.0, 0.1] in seconds
				float Diffusion;            // [0.0, 100.0] (percentage)
				float Density;              // [0.0, 100.0] (percentage)
				float HFReference;          // [20.0, 20000.0] in Hz
			} XAUDIO2FX_REVERB_I3DL2_PARAMETERS;

			// ReverbConvertI3DL2ToNative: Utility function to map from I3DL2 to native parameters

			inline static void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2,
				XAUDIO2FX_REVERB_PARAMETERS* pNative) noexcept
			{
				float reflectionsDelay{};
				float reverbDelay{};

				// RoomRolloffFactor is ignored

				// These parameters have no equivalent in I3DL2
				pNative->RearDelay = XAUDIO2FX_REVERB_DEFAULT_REAR_DELAY; // 5
				pNative->PositionLeft = XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
				pNative->PositionRight = XAUDIO2FX_REVERB_DEFAULT_POSITION; // 6
				pNative->PositionMatrixLeft = XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
				pNative->PositionMatrixRight = XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX; // 27
				pNative->RoomSize = XAUDIO2FX_REVERB_DEFAULT_ROOM_SIZE; // 100
				pNative->LowEQCutoff = 4;
				pNative->HighEQCutoff = 6;

				// The rest of the I3DL2 parameters map to the native property set
				pNative->RoomFilterMain = static_cast<float>(pI3DL2->Room) / 100.0f;
				pNative->RoomFilterHF = static_cast<float>(pI3DL2->RoomHF) / 100.0f;

				if (pI3DL2->DecayHFRatio >= 1.0f)
				{
					auto index = static_cast<INT32>(-4.0 * log10(pI3DL2->DecayHFRatio));
					if (index < -8) index = -8;
					pNative->LowEQGain = static_cast<BYTE>((index < 0) ? index + 8 : 8);
					pNative->HighEQGain = 8;
					pNative->DecayTime = pI3DL2->DecayTime * pI3DL2->DecayHFRatio;
				}
				else
				{
					auto index = static_cast<INT32>(4.0 * log10(pI3DL2->DecayHFRatio));
					if (index < -8) index = -8;
					pNative->LowEQGain = 8;
					pNative->HighEQGain = static_cast<BYTE>((index < 0) ? index + 8 : 8);
					pNative->DecayTime = pI3DL2->DecayTime;
				}

				reflectionsDelay = pI3DL2->ReflectionsDelay * 1000.0f;
				if (reflectionsDelay >= XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY) // 300
					reflectionsDelay = static_cast<float>(XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY - 1);
				else if (reflectionsDelay <= 1)
					reflectionsDelay = 1;
				pNative->ReflectionsDelay = static_cast<UINT32>(reflectionsDelay);

				reverbDelay = pI3DL2->ReverbDelay * 1000.0f;
				if (reverbDelay >= XAUDIO2FX_REVERB_MAX_REVERB_DELAY) // 85
					reverbDelay = static_cast<float>(XAUDIO2FX_REVERB_MAX_REVERB_DELAY - 1);
				pNative->ReverbDelay = static_cast<BYTE>(reverbDelay);

				pNative->ReflectionsGain = pI3DL2->Reflections / 100.0f;
				pNative->ReverbGain = pI3DL2->Reverb / 100.0f;
				pNative->EarlyDiffusion = static_cast<BYTE>(15.0f * pI3DL2->Diffusion / 100.0f);
				pNative->LateDiffusion = pNative->EarlyDiffusion;
				pNative->Density = pI3DL2->Density;
				pNative->RoomFilterFreq = pI3DL2->HFReference;

				pNative->WetDryMix = pI3DL2->WetDryMix;
			}

			constexpr static XAUDIO2FX_REVERB_I3DL2_PARAMETERS XAUDIO2FX_I3DL2_PRESET_DEFAULT
			{ 100, -10000, 0, .0f, 1.f, .5f, -10000, .02f, -10000, .04f, 100.f, 100.f, 5000.f };

			// Undo the #pragma pack(push, 1) at the top of this file
			#pragma pack(pop)
		}

		// All structures defined in this file use tight field packing
		#pragma pack(push, 1)

		struct IXAudio2;
		struct IXAudio2Voice;
		struct IXAudio2SourceVoice;
		struct IXAudio2SubmixVoice;
		struct IXAudio2MasteringVoice;

		typedef struct XAUDIO2_VOICE_DETAILS
		{
			UINT32 CreationFlags;				// Flags the voice was created with.
			UINT32 InputChannels;				// Channels in the voice's input audio.
			UINT32 InputSampleRate;				// Sample rate of the voice's input audio.
		} XAUDIO2_VOICE_DETAILS;

		// Used in XAUDIO2_VOICE_SENDS below
		typedef struct XAUDIO2_SEND_DESCRIPTOR
		{
			UINT32 Flags;						// Either 0 or XAUDIO2_SEND_USEFILTER.
			IXAudio2Voice* pOutputVoice;		// This send's destination voice.
		} XAUDIO2_SEND_DESCRIPTOR;

		// Used in the voice creation functions and in IXAudio2Voice::SetOutputVoices
		typedef struct XAUDIO2_VOICE_SENDS
		{
			UINT32 SendCount;					// Number of sends from this voice.
			XAUDIO2_SEND_DESCRIPTOR* pSends;	// Array of SendCount send descriptors.
		} XAUDIO2_VOICE_SENDS;

		// Used in XAUDIO2_FILTER_PARAMETERS below
		typedef enum XAUDIO2_FILTER_TYPE
		{
			LowPassFilter,						// Attenuates frequencies above the cutoff frequency.
			BandPassFilter,						// Attenuates frequencies outside a given range.
			HighPassFilter,						// Attenuates frequencies below the cutoff frequency.
			NotchFilter							// Attenuates frequencies inside a given range.
		} XAUDIO2_FILTER_TYPE;

		// Used in IXAudio2Voice::Set/GetFilterParameters and Set/GetOutputFilterParameters
		typedef struct XAUDIO2_FILTER_PARAMETERS
		{
			XAUDIO2_FILTER_TYPE Type;			// Low-pass, band-pass or high-pass.
			float Frequency;					// Radian frequency (2 * sin(pi*CutoffFrequency/SampleRate));
												// must be >= 0 and <= XAUDIO2_MAX_FILTER_FREQUENCY
												// (giving a maximum CutoffFrequency of SampleRate/6).
			float OneOverQ;						// Reciprocal of the filter's quality factor Q;
												// must be > 0 and <= XAUDIO2_MAX_FILTER_ONEOVERQ.
		} XAUDIO2_FILTER_PARAMETERS;

		// Used in XAUDIO2_EFFECT_CHAIN below
		typedef struct XAUDIO2_EFFECT_DESCRIPTOR
		{
			LPVOID pEffect;						// Pointer to the effect object's IUnknown interface.
			BOOL InitialState;					// TRUE if the effect should begin in the enabled state.
			UINT32 OutputChannels;				// How many output channels the effect should produce.
		} XAUDIO2_EFFECT_DESCRIPTOR;

		// Used in the voice creation functions and in IXAudio2Voice::SetEffectChain
		typedef struct XAUDIO2_EFFECT_CHAIN
		{
			UINT32 EffectCount;								// Number of effects in this voice's effect chain.
			XAUDIO2_EFFECT_DESCRIPTOR* pEffectDescriptors;	// Array of effect descriptors.
		} XAUDIO2_EFFECT_CHAIN;

		// Used in IXAudio2SourceVoice::SubmitSourceBuffer
		typedef struct XAUDIO2_BUFFER
		{
			UINT32 Flags;						// Either 0 or XAUDIO2_END_OF_STREAM.
			UINT32 AudioBytes;					// Size of the audio data buffer in bytes.
			const BYTE* pAudioData;				// Pointer to the audio data buffer.
			UINT32 PlayBegin;					// First sample in this buffer to be played.
			UINT32 PlayLength;					// Length of the region to be played in samples,
												//  or 0 to play the whole buffer.
			UINT32 LoopBegin;					// First sample of the region to be looped.
			UINT32 LoopLength;					// Length of the desired loop region in samples,
												//  or 0 to loop the entire buffer.
			UINT32 LoopCount;					// Number of times to repeat the loop region,
												//  or XAUDIO2_LOOP_INFINITE to loop forever.
			void* pContext;						// Context value to be passed back in callbacks.
		} XAUDIO2_BUFFER;

		// Used in IXAudio2SourceVoice::SubmitSourceBuffer when submitting XWMA data.
		// NOTE: If an XWMA sound is submitted in more than one buffer, each buffer's
		// pDecodedPacketCumulativeBytes[PacketCount-1] value must be subtracted from
		// all the entries in the next buffer's pDecodedPacketCumulativeBytes array.
		// And whether a sound is submitted in more than one buffer or not, the final
		// buffer of the sound should use the XAUDIO2_END_OF_STREAM flag, or else the
		// client must call IXAudio2SourceVoice::Discontinuity after submitting it.
		typedef struct XAUDIO2_BUFFER_WMA
		{
			const UINT32* pDecodedPacketCumulativeBytes;	// Decoded packet's cumulative size array.
															//  Each element is the number of bytes accumulated
															//  when the corresponding XWMA packet is decoded in
															//  order.  The array must have PacketCount elements.
			UINT32 PacketCount;								// Number of XWMA packets submitted. Must be >= 1 and
															//  divide evenly into XAUDIO2_BUFFER.AudioBytes.
		} XAUDIO2_BUFFER_WMA;

		// Returned by IXAudio2SourceVoice::GetState
		typedef struct XAUDIO2_VOICE_STATE
		{
			void* pCurrentBufferContext;		// The pContext value provided in the XAUDIO2_BUFFER
												//  that is currently being processed, or NULL if
												//  there are no buffers in the queue.
			UINT32 BuffersQueued;				// Number of buffers currently queued on the voice
												//  (including the one that is being processed).
			UINT64 SamplesPlayed;				// Total number of samples produced by the voice since
												//  it began processing the current audio stream.
		} XAUDIO2_VOICE_STATE;

		// Returned by IXAudio2::GetPerformanceData
		typedef struct XAUDIO2_PERFORMANCE_DATA
		{
												// CPU usage information
			UINT64 AudioCyclesSinceLastQuery;	// CPU cycles spent on audio processing since the
												//  last call to StartEngine or GetPerformanceData.
			UINT64 TotalCyclesSinceLastQuery;	// Total CPU cycles elapsed since the last call
												//  (only counts the CPU XAudio2 is running on).
			UINT32 MinimumCyclesPerQuantum;		// Fewest CPU cycles spent processing any one
												//  audio quantum since the last call.
			UINT32 MaximumCyclesPerQuantum;		// Most CPU cycles spent processing any one
												//  audio quantum since the last call.

												// Memory usage information
			UINT32 MemoryUsageInBytes;          // Total heap space currently in use.

												// Audio latency and glitching information
			UINT32 CurrentLatencyInSamples;		// Minimum delay from when a sample is read from a
												//  source buffer to when it reaches the speakers.
			UINT32 GlitchesSinceEngineStarted;	// Audio dropouts since the engine was started.

												// Data about XAudio2's current workload
			UINT32 ActiveSourceVoiceCount;		// Source voices currently playing.
			UINT32 TotalSourceVoiceCount;		// Source voices currently existing.
			UINT32 ActiveSubmixVoiceCount;		// Submix voices currently playing/existing.

			UINT32 ActiveResamplerCount;		// Resample xAPOs currently active.
			UINT32 ActiveMatrixMixCount;		// MatrixMix xAPOs currently active.

												// Usage of the hardware XMA decoder (Xbox 360 only)
			UINT32 ActiveXmaSourceVoices;		// Number of source voices decoding XMA data.
			UINT32 ActiveXmaStreams;			// A voice can use more than one XMA stream.
		} XAUDIO2_PERFORMANCE_DATA;

		// Used in IXAudio2::SetDebugConfiguration
		typedef struct XAUDIO2_DEBUG_CONFIGURATION
		{
			UINT32 TraceMask;			// Bitmap of enabled debug message types.
			UINT32 BreakMask;			// Message types that will break into the debugger.
			BOOL LogThreadID;			// Whether to log the thread ID with each message.
			BOOL LogFileline;			// Whether to log the source file and line number.
			BOOL LogFunctionName;		// Whether to log the function name.
			BOOL LogTiming;				// Whether to log message timestamps.
		} XAUDIO2_DEBUG_CONFIGURATION;

		// Used in XAUDIO2_DEVICE_DETAILS below to describe the types of applications
		// that the user has specified each device as a default for.  0 means that the
		// device isn't the default for any role.
		typedef enum XAUDIO2_DEVICE_ROLE
		{
			NotDefaultDevice = 0x0,
			DefaultConsoleDevice = 0x1,
			DefaultMultimediaDevice = 0x2,
			DefaultCommunicationsDevice = 0x4,
			DefaultGameDevice = 0x8,
			GlobalDefaultDevice = 0xf,
			InvalidDeviceRole = ~GlobalDefaultDevice
		} XAUDIO2_DEVICE_ROLE;

		// Returned by IXAudio2::GetDeviceDetails
		typedef struct XAUDIO2_DEVICE_DETAILS
		{
			WCHAR DeviceID[256];				// String identifier for the audio device.
			WCHAR DisplayName[256];				// Friendly name suitable for display to a human.
			XAUDIO2_DEVICE_ROLE Role;			// Roles that the device should be used for.
			WAVEFORMATEXTENSIBLE OutputFormat;	// The device's native PCM audio output format.
		} XAUDIO2_DEVICE_DETAILS;

		typedef enum XAUDIO2_WINDOWS_PROCESSOR_SPECIFIER
		{
			Processor1 = 0x00000001,
			Processor2 = 0x00000002,
			Processor3 = 0x00000004,
			Processor4 = 0x00000008,
			Processor5 = 0x00000010,
			Processor6 = 0x00000020,
			Processor7 = 0x00000040,
			Processor8 = 0x00000080,
			Processor9 = 0x00000100,
			Processor10 = 0x00000200,
			Processor11 = 0x00000400,
			Processor12 = 0x00000800,
			Processor13 = 0x00001000,
			Processor14 = 0x00002000,
			Processor15 = 0x00004000,
			Processor16 = 0x00008000,
			Processor17 = 0x00010000,
			Processor18 = 0x00020000,
			Processor19 = 0x00040000,
			Processor20 = 0x00080000,
			Processor21 = 0x00100000,
			Processor22 = 0x00200000,
			Processor23 = 0x00400000,
			Processor24 = 0x00800000,
			Processor25 = 0x01000000,
			Processor26 = 0x02000000,
			Processor27 = 0x04000000,
			Processor28 = 0x08000000,
			Processor29 = 0x10000000,
			Processor30 = 0x20000000,
			Processor31 = 0x40000000,
			Processor32 = 0x80000000,
			XAUDIO2_ANY_PROCESSOR = 0xffffffff,
			XAUDIO2_DEFAULT_PROCESSOR = XAUDIO2_ANY_PROCESSOR
		} XAUDIO2_WINDOWS_PROCESSOR_SPECIFIER, XAUDIO2_PROCESSOR;

		/**************************************************************************
		 *
		 * XAudio2 constants, flags and error codes.
		 *
		 **************************************************************************/

		// Numeric boundary values

		constexpr static auto XAUDIO2_MAX_BUFFER_BYTES = 0x80000000;	// Maximum bytes allowed in a source buffer
		constexpr static auto XAUDIO2_MAX_QUEUED_BUFFERS = 64;			// Maximum buffers allowed in a voice queue
		constexpr static auto XAUDIO2_MAX_BUFFERS_SYSTEM = 2;			// Maximum buffers allowed for system threads (Xbox 360 only)
		constexpr static auto XAUDIO2_MAX_AUDIO_CHANNELS = 64;			// Maximum channels in an audio stream
		constexpr static auto XAUDIO2_MIN_SAMPLE_RATE = 1000;			// Minimum audio sample rate supported
		constexpr static auto XAUDIO2_MAX_SAMPLE_RATE = 200000;			// Maximum audio sample rate supported
		constexpr static auto XAUDIO2_MAX_VOLUME_LEVEL = 16777216.0f;	// Maximum acceptable volume level (2^24)
		constexpr static auto XAUDIO2_MIN_FREQ_RATIO = (1 / 1024.0f);	// Minimum SetFrequencyRatio argument
		constexpr static auto XAUDIO2_MAX_FREQ_RATIO = 1024.0f;			// Maximum MaxFrequencyRatio argument
		constexpr static auto XAUDIO2_DEFAULT_FREQ_RATIO = 2.0f;		// Default MaxFrequencyRatio argument
		constexpr static auto XAUDIO2_MAX_FILTER_ONEOVERQ = 1.5f;		// Maximum XAUDIO2_FILTER_PARAMETERS.OneOverQ
		constexpr static auto XAUDIO2_MAX_FILTER_FREQUENCY = 1.0f;		// Maximum XAUDIO2_FILTER_PARAMETERS.Frequency
		constexpr static auto XAUDIO2_MAX_LOOP_COUNT = 254;				// Maximum non-infinite XAUDIO2_BUFFER.LoopCount
		constexpr static auto XAUDIO2_MAX_INSTANCES = 8;				// Maximum simultaneous XAudio2 objects on Xbox 360

		// For XMA voices on Xbox 360 there is an additional restriction on the MaxFrequencyRatio
		// argument and the voice's sample rate: the product of these numbers cannot exceed 600000
		// for one-channel voices or 300000 for voices with more than one channel.

		constexpr static auto XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MONO = 600000;
		constexpr static auto XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MULTICHANNEL = 300000;

		// Numeric values with special meanings

		constexpr static auto XAUDIO2_COMMIT_NOW = 0;					// Used as an OperationSet argument
		constexpr static auto XAUDIO2_COMMIT_ALL = 0;					// Used in IXAudio2::CommitChanges
		constexpr static auto XAUDIO2_INVALID_OPSET = (UINT32)(-1);		// Not allowed for OperationSet arguments
		constexpr static auto XAUDIO2_NO_LOOP_REGION = 0;				// Used in XAUDIO2_BUFFER.LoopCount
		constexpr static auto XAUDIO2_LOOP_INFINITE = 255;				// Used in XAUDIO2_BUFFER.LoopCount
		constexpr static auto XAUDIO2_DEFAULT_CHANNELS = 0;				// Used in CreateMasteringVoice
		constexpr static auto XAUDIO2_DEFAULT_SAMPLERATE = 0;			// Used in CreateMasteringVoice

		// Flags

		constexpr static auto XAUDIO2_DEBUG_ENGINE = 0x0001;			// Used in XAudio2Create on Windows only
		constexpr static auto XAUDIO2_VOICE_NOPITCH = 0x0002;			// Used in IXAudio2::CreateSourceVoice
		constexpr static auto XAUDIO2_VOICE_NOSRC = 0x0004;				// Used in IXAudio2::CreateSourceVoice
		constexpr static auto XAUDIO2_VOICE_USEFILTER = 0x0008;			// Used in IXAudio2::CreateSource/SubmixVoice
		constexpr static auto XAUDIO2_VOICE_MUSIC = 0x0010;				// Used in IXAudio2::CreateSourceVoice
		constexpr static auto XAUDIO2_PLAY_TAILS = 0x0020;				// Used in IXAudio2SourceVoice::Stop
		constexpr static auto XAUDIO2_END_OF_STREAM = 0x0040;			// Used in XAUDIO2_BUFFER.Flags
		constexpr static auto XAUDIO2_SEND_USEFILTER = 0x0080;			// Used in XAUDIO2_SEND_DESCRIPTOR.Flags

		// Default parameters for the built-in filter

		constexpr static auto XAUDIO2_DEFAULT_FILTER_TYPE = LowPassFilter;
		constexpr static auto XAUDIO2_DEFAULT_FILTER_FREQUENCY = XAUDIO2_MAX_FILTER_FREQUENCY;
		constexpr static auto XAUDIO2_DEFAULT_FILTER_ONEOVERQ = 1.0f;

		// Undo the #pragma pack(push, 1) at the top of this file
		#pragma pack(pop)

		/**************************************************************************
		 *
		 * IXAudio2EngineCallback: Client notification interface for engine events.
		 *
		 * REMARKS: Contains methods to notify the client when certain events happen
		 *          in the XAudio2 engine.  This interface should be implemented by
		 *          the client.  XAudio2 will call these methods via the interface
		 *          pointer provided by the client when it calls XAudio2Create or
		 *          IXAudio2::Initialize.
		 *
		 **************************************************************************/
		struct __declspec(novtable) IXAudio2EngineCallback
		{
			virtual void OnProcessingPassStart() = 0;
			virtual void OnProcessingPassEnd() = 0;
			virtual void OnCriticalError(HRESULT a_herror) = 0;
		};

		/**************************************************************************
		 *
		 * IXAudio2VoiceCallback: Client notification interface for voice events.
		 *
		 * REMARKS: Contains methods to notify the client when certain events happen
		 *          in an XAudio2 voice.  This interface should be implemented by the
		 *          client.  XAudio2 will call these methods via an interface pointer
		 *          provided by the client in the IXAudio2::CreateSourceVoice call.
		 *
		 **************************************************************************/

		struct __declspec(novtable) IXAudio2VoiceCallback
		{
			// Called just before this voice's processing pass begins.
			virtual void OnVoiceProcessingPassStart(UINT32 BytesRequired) = 0;

			// Called just after this voice's processing pass ends.
			virtual void OnVoiceProcessingPassEnd() = 0;

			// Called when this voice has just finished playing a buffer stream
			// (as marked with the XAUDIO2_END_OF_STREAM flag on the last buffer).
			virtual void OnStreamEnd() = 0;

			// Called when this voice is about to start processing a new buffer.
			virtual void OnBufferStart(void* pBufferContext) = 0;

			// Called when this voice has just finished processing a buffer.
			// The buffer can now be reused or destroyed.
			virtual void OnBufferEnd(void* pBufferContext) = 0;

			// Called when this voice has just reached the end position of a loop.
			virtual void OnLoopEnd(void* pBufferContext) = 0;

			// Called in the event of a critical error during voice processing,
			// such as a failing xAPO or an error from the hardware XMA decoder.
			// The voice may have to be destroyed and re-created to recover from
			// the error.  The callback arguments report which buffer was being
			// processed when the error occurred, and its HRESULT code.
			virtual void OnVoiceError(void* pBufferContext, HRESULT Error) = 0;
		};

		struct __declspec(novtable) IXAudio2 :
			public IUnknown
		{
			// NAME: IXAudio2::QueryInterface
			// DESCRIPTION: Queries for a given COM interface on the XAudio2 object.
			//              Only IID_IUnknown and IID_IXAudio2 are supported.
			//
			// ARGUMENTS:
			//  riid - IID of the interface to be obtained.
			//  ppvInterface - Returns a pointer to the requested interface.
			//
			virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) noexcept = 0;

			// NAME: IXAudio2::AddRef
			// DESCRIPTION: Adds a reference to the XAudio2 object.
			//
			virtual ULONG AddRef() noexcept = 0;

			// NAME: IXAudio2::Release
			// DESCRIPTION: Releases a reference to the XAudio2 object.
			//
			virtual ULONG Release() noexcept override = 0;

			// NAME: IXAudio2::GetDeviceCount
			// DESCRIPTION: Returns the number of audio output devices available.
			//
			// ARGUMENTS:
			//  pCount - Returns the device count.
			//
			virtual REX::W32::HRESULT GetDeviceCount(UINT32* pCount) const = 0;

			// NAME: IXAudio2::GetDeviceDetails
			// DESCRIPTION: Returns information about the device with the given index.
			//
			// ARGUMENTS:
			//  Index - Index of the device to be queried.
			//  pDeviceDetails - Returns the device details.
			//
			virtual REX::W32::HRESULT GetDeviceDetails([[maybe_unused]] UINT32 Index,
				XAUDIO2_DEVICE_DETAILS* pDeviceDetails) const = 0;

			// NAME: IXAudio2::Initialize
			// DESCRIPTION: Sets global XAudio2 parameters and prepares it for use.
			//
			// ARGUMENTS:
			//  Flags - Flags specifying the XAudio2 object's behavior.  Currently unused.
			//  XAudio2Processor - An XAUDIO2_PROCESSOR enumeration value that specifies
			//  the hardware thread (Xbox) or processor (Windows) that XAudio2 will use.
			//  The enumeration values are platform-specific; platform-independent code
			//  can use XAUDIO2_DEFAULT_PROCESSOR to use the default on each platform.
			//
			virtual REX::W32::HRESULT Initialize([[maybe_unused]] UINT32 Flags = 0,
				[[maybe_unused]] XAUDIO2_PROCESSOR XAudio2Processor = XAUDIO2_DEFAULT_PROCESSOR) noexcept = 0;

			// NAME: IXAudio2::RegisterForCallbacks
			// DESCRIPTION: Adds a new client to receive XAudio2's engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Callback interface to be called during each processing pass.
			//
			virtual REX::W32::HRESULT RegisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept = 0;

			// NAME: IXAudio2::UnregisterForCallbacks
			// DESCRIPTION: Removes an existing receiver of XAudio2 engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Previously registered callback interface to be removed.
			//
			virtual void UnregisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept = 0;

			// NAME: IXAudio2::CreateSourceVoice
			// DESCRIPTION: Creates and configures a source voice.
			//
			// ARGUMENTS:
			//  ppSourceVoice - Returns the new object's IXAudio2SourceVoice interface.
			//  pSourceFormat - Format of the audio that will be fed to the voice.
			//  Flags - XAUDIO2_VOICE flags specifying the source voice's behavior.
			//  MaxFrequencyRatio - Maximum SetFrequencyRatio argument to be allowed.
			//  pCallback - Optional pointer to a client-provided callback interface.
			//  pSendList - Optional list of voices this voice should send audio to.
			//  pEffectChain - Optional list of effects to apply to the audio data.
			//
			virtual REX::W32::HRESULT CreateSourceVoice(IXAudio2SourceVoice** ppSourceVoice,
				const WAVEFORMATEX* pSourceFormat, UINT32 Flags = 0,
				float MaxFrequencyRatio = 2.0f,
				IXAudio2VoiceCallback* pCallback = nullptr,
				const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept = 0;

			// NAME: IXAudio2::CreateSubmixVoice
			// DESCRIPTION: Creates and configures a submix voice.
			//
			// ARGUMENTS:
			//  ppSubmixVoice - Returns the new object's IXAudio2SubmixVoice interface.
			//  InputChannels - Number of channels in this voice's input audio data.
			//  InputSampleRate - Sample rate of this voice's input audio data.
			//  Flags - XAUDIO2_VOICE flags specifying the submix voice's behavior.
			//  ProcessingStage - Arbitrary number that determines the processing order.
			//  pSendList - Optional list of voices this voice should send audio to.
			//  pEffectChain - Optional list of effects to apply to the audio data.
			//
			virtual REX::W32::HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** ppSubmixVoice,
				UINT32 InputChannels, UINT32 InputSampleRate,
				UINT32 Flags = 0, UINT32 ProcessingStage = 0,
				const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept = 0;

			// NAME: IXAudio2::CreateMasteringVoice
			// DESCRIPTION: Creates and configures a mastering voice.
			//
			// ARGUMENTS:
			//  ppMasteringVoice - Returns the new object's IXAudio2MasteringVoice interface.
			//  InputChannels - Number of channels in this voice's input audio data.
			//  InputSampleRate - Sample rate of this voice's input audio data.
			//  Flags - XAUDIO2_VOICE flags specifying the mastering voice's behavior.
			//  DeviceIndex - Identifier of the device to receive the output audio.
			//  pEffectChain - Optional list of effects to apply to the audio data.
			//
			virtual REX::W32::HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** ppMasteringVoice,
				UINT32 InputChannels = XAUDIO2_DEFAULT_CHANNELS,
				UINT32 InputSampleRate = XAUDIO2_DEFAULT_SAMPLERATE,
				UINT32 Flags = 0, UINT32 DeviceIndex = 0,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept = 0;

			// NAME: IXAudio2::StartEngine
			// DESCRIPTION: Creates and starts the audio processing thread.
			//
			virtual REX::W32::HRESULT StartEngine() noexcept = 0;

			// NAME: IXAudio2::StopEngine
			// DESCRIPTION: Stops and destroys the audio processing thread.
			//
			virtual void StopEngine() noexcept = 0;

			// NAME: IXAudio2::CommitChanges
			// DESCRIPTION: Atomically applies a set of operations previously tagged
			//              with a given identifier.
			//
			// ARGUMENTS:
			//  OperationSet - Identifier of the set of operations to be applied.
			//
			virtual REX::W32::HRESULT CommitChanges(UINT32 OperationSet) noexcept = 0;

			// NAME: IXAudio2::GetPerformanceData
			// DESCRIPTION: Returns current resource usage details: memory, CPU, etc.
			//
			// ARGUMENTS:
			//  pPerfData - Returns the performance data structure.
			//
			virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* pPerfData) const noexcept = 0;

			// NAME: IXAudio2::SetDebugConfiguration
			// DESCRIPTION: Configures XAudio2's debug output (in debug builds only).
			//
			// ARGUMENTS:
			//  pDebugConfiguration - Structure describing the debug output behavior.
			//  pReserved - Optional parameter; must be NULL.
			//
			virtual void SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION* pDebugConfiguration,
				[[maybe_unused]] void* pReserved = nullptr) noexcept = 0;
		};

		struct __declspec(novtable) IXAudio2Voice
		{
			// NAME: IXAudio2Voice::GetVoiceDetails
			// DESCRIPTION: Returns the basic characteristics of this voice.
			//
			// ARGUMENTS:
			//  pVoiceDetails - Returns the voice's details.
			//
			virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* pVoiceDetails) const noexcept = 0;

			// NAME: IXAudio2Voice::SetOutputVoices
			// DESCRIPTION: Replaces the set of submix/mastering voices that receive
			//              this voice's output.
			//
			// ARGUMENTS:
			//  pSendList - Optional list of voices this voice should send audio to.
			//
			virtual REX::W32::HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS* pSendList) noexcept = 0;

			// NAME: IXAudio2Voice::SetEffectChain
			// DESCRIPTION: Replaces this voice's current effect chain with a new one.
			//
			// ARGUMENTS:
			//  pEffectChain - Structure describing the new effect chain to be used.
			//
			virtual REX::W32::HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN* pEffectChain) noexcept = 0;

			// NAME: IXAudio2Voice::EnableEffect
			// DESCRIPTION: Enables an effect in this voice's effect chain.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT EnableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::DisableEffect
			// DESCRIPTION: Disables an effect in this voice's effect chain.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT DisableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetEffectState
			// DESCRIPTION: Returns the running state of an effect.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  pEnabled - Returns the enabled/disabled state of the given effect.
			//
			virtual void GetEffectState(UINT32 EffectIndex, BOOL* pEnabled) const noexcept = 0;

			// NAME: IXAudio2Voice::SetEffectParameters
			// DESCRIPTION: Sets effect-specific parameters.
			//
			// REMARKS: Unlike IXAPOParameters::SetParameters, this method may
			//          be called from any thread.  XAudio2 implements
			//          appropriate synchronization to copy the parameters to the
			//          realtime audio processing thread.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  pParameters - Pointer to an effect-specific parameters block.
			//  ParametersByteSize - Size of the pParameters array  in bytes.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetEffectParameters(UINT32 EffectIndex, const void* pParameters,
				UINT32 ParametersByteSize, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetEffectParameters
			// DESCRIPTION: Obtains the current effect-specific parameters.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  pParameters - Returns the current values of the effect-specific parameters.
			//  ParametersByteSize - Size of the pParameters array in bytes.
			//
			virtual REX::W32::HRESULT GetEffectParameters(UINT32 EffectIndex, void* pParameters,
				UINT32 ParametersByteSize) const noexcept = 0;

			// NAME: IXAudio2Voice::SetFilterParameters
			// DESCRIPTION: Sets this voice's filter parameters.
			//
			// ARGUMENTS:
			//  pParameters - Pointer to the filter's parameter structure.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* pParameters,
				UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetFilterParameters
			// DESCRIPTION: Returns this voice's current filter parameters.
			//
			// ARGUMENTS:
			//  pParameters - Returns the filter parameters.
			//
			virtual void GetFilterParameters(XAUDIO2_FILTER_PARAMETERS* pParameters) const noexcept = 0;

			// NAME: IXAudio2Voice::SetOutputFilterParameters
			// DESCRIPTION: Sets the filter parameters on one of this voice's sends.
			//
			// ARGUMENTS:
			//  pDestinationVoice - Destination voice of the send whose filter parameters will be set.
			//  pParameters - Pointer to the filter's parameter structure.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetOutputFilterParameters(IXAudio2Voice* pDestinationVoice,
				const XAUDIO2_FILTER_PARAMETERS* pParameters, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetOutputFilterParameters
			// DESCRIPTION: Returns the filter parameters from one of this voice's sends.
			//
			// ARGUMENTS:
			//  pDestinationVoice - Destination voice of the send whose filter parameters will be read.
			//  pParameters - Returns the filter parameters.
			//
			virtual void GetOutputFilterParameters(IXAudio2Voice* pDestinationVoice,
				XAUDIO2_FILTER_PARAMETERS* pParameters) const noexcept = 0;

			// NAME: IXAudio2Voice::SetVolume
			// DESCRIPTION: Sets this voice's overall volume level.
			//
			// ARGUMENTS:
			//  Volume - New overall volume level to be used, as an amplitude factor.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetVolume(float Volume, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetVolume
			// DESCRIPTION: Obtains this voice's current overall volume level.
			//
			// ARGUMENTS:
			//  pVolume: Returns the voice's current overall volume level.
			//
			virtual void GetVolume(float* pVolume) const noexcept = 0;

			// NAME: IXAudio2Voice::SetChannelVolumes
			// DESCRIPTION: Sets this voice's per-channel volume levels.
			//
			// ARGUMENTS:
			//  Channels - Used to confirm the voice's channel count.
			//  pVolumes - Array of per-channel volume levels to be used.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetChannelVolumes(UINT32 Channels, const float* pVolumes,
				UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetChannelVolumes
			// DESCRIPTION: Returns this voice's current per-channel volume levels.
			//
			// ARGUMENTS:
			//  Channels - Used to confirm the voice's channel count.
			//  pVolumes - Returns an array of the current per-channel volume levels.
			//
			virtual void GetChannelVolumes(UINT32 Channels, float* pVolumes) const noexcept = 0;

			// NAME: IXAudio2Voice::SetOutputMatrix
			// DESCRIPTION: Sets the volume levels used to mix from each channel of this
			//              voice's output audio to each channel of a given destination
			//              voice's input audio.
			//
			// ARGUMENTS:
			//  pDestinationVoice - The destination voice whose mix matrix to change.
			//  SourceChannels - Used to confirm this voice's output channel count
			//   (the number of channels produced by the last effect in the chain).
			//  DestinationChannels - Confirms the destination voice's input channels.
			//  pLevelMatrix - Array of [SourceChannels * DestinationChannels] send
			//   levels.  The level used to send from source channel S to destination
			//   channel D should be in pLevelMatrix[S + SourceChannels * D].
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetOutputMatrix(IXAudio2Voice* pDestinationVoice,
				UINT32 SourceChannels, UINT32 DestinationChannels, const float* pLevelMatrix,
				UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2Voice::GetOutputMatrix
			// DESCRIPTION: Obtains the volume levels used to send each channel of this
			//              voice's output audio to each channel of a given destination
			//              voice's input audio.
			//
			// ARGUMENTS:
			//  pDestinationVoice - The destination voice whose mix matrix to obtain.
			//  SourceChannels - Used to confirm this voice's output channel count
			//   (the number of channels produced by the last effect in the chain).
			//  DestinationChannels - Confirms the destination voice's input channels.
			//  pLevelMatrix - Array of send levels, as above.
			//
			virtual void GetOutputMatrix(IXAudio2Voice* pDestinationVoice,
				UINT32 SourceChannels, UINT32 DestinationChannels, float* pLevelMatrix) const noexcept = 0;

			// NAME: IXAudio2Voice::DestroyVoice
			// DESCRIPTION: Destroys this voice, stopping it if necessary and removing
			//              it from the XAudio2 graph.
			//
			virtual void DestroyVoice() noexcept = 0;
		};

		/**************************************************************************
		 *
		 * IXAudio2SourceVoice: Source voice management interface.
		 *
		 **************************************************************************/

		struct __declspec(novtable) IXAudio2SourceVoice :
			public IXAudio2Voice
		{
			// NAME: IXAudio2SourceVoice::Start
			// DESCRIPTION: Makes this voice start consuming and processing audio.
			//
			// ARGUMENTS:
			//  Flags - Flags controlling how the voice should be started.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT Start(UINT32 Flags = 0, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2SourceVoice::Stop
			// DESCRIPTION: Makes this voice stop consuming audio.
			//
			// ARGUMENTS:
			//  Flags - Flags controlling how the voice should be stopped.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT Stop(UINT32 Flags = 0, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2SourceVoice::SubmitSourceBuffer
			// DESCRIPTION: Adds a new audio buffer to this voice's input queue.
			//
			// ARGUMENTS:
			//  pBuffer - Pointer to the buffer structure to be queued.
			//  pBufferWMA - Additional structure used only when submitting XWMA data.
			//
			virtual REX::W32::HRESULT SubmitSourceBuffer(XAUDIO2_BUFFER* pBuffer,
				const XAUDIO2_BUFFER_WMA* pBufferWMA = nullptr) noexcept = 0;

			// NAME: IXAudio2SourceVoice::FlushSourceBuffers
			// DESCRIPTION: Removes all pending audio buffers from this voice's queue.
			//
			virtual REX::W32::HRESULT FlushSourceBuffers() noexcept = 0;

			// NAME: IXAudio2SourceVoice::Discontinuity
			// DESCRIPTION: Notifies the voice of an intentional break in the stream of
			//              audio buffers (e.g. the end of a sound), to prevent XAudio2
			//              from interpreting an empty buffer queue as a glitch.
			//
			virtual REX::W32::HRESULT Discontinuity() noexcept = 0;

			// NAME: IXAudio2SourceVoice::ExitLoop
			// DESCRIPTION: Breaks out of the current loop when its end is reached.
			//
			// ARGUMENTS:
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT ExitLoop(UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2SourceVoice::GetState
			// DESCRIPTION: Returns the number of buffers currently queued on this voice,
			//              the pContext value associated with the currently processing
			//              buffer (if any), and other voice state information.
			//
			// ARGUMENTS:
			//  pVoiceState - Returns the state information.
			//
			virtual void GetState(XAUDIO2_VOICE_STATE* pVoiceState) const noexcept = 0;

			// NAME: IXAudio2SourceVoice::SetFrequencyRatio
			// DESCRIPTION: Sets this voice's frequency adjustment, i.e. its pitch.
			//
			// ARGUMENTS:
			//  Ratio - Frequency change, expressed as source frequency / target frequency.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual REX::W32::HRESULT SetFrequencyRatio(float Ratio, UINT32 OperationSet = 0) noexcept = 0;

			// NAME: IXAudio2SourceVoice::GetFrequencyRatio
			// DESCRIPTION: Returns this voice's current frequency adjustment ratio.
			//
			// ARGUMENTS:
			//  pRatio - Returns the frequency adjustment.
			//
			virtual void GetFrequencyRatio(float* pRatio) const noexcept = 0;

			// NAME: IXAudio2SourceVoice::SetSourceSampleRate
			// DESCRIPTION: Reconfigures this voice to treat its source data as being
			//              at a different sample rate than the original one specified
			//              in CreateSourceVoice's pSourceFormat argument.
			//
			// ARGUMENTS:
			//  UINT32 - The intended sample rate of further submitted source data.
			//
			virtual REX::W32::HRESULT SetSourceSampleRate(UINT32 NewSourceSampleRate) noexcept = 0;
		};

		/**************************************************************************
		 *
		 * IXAudio2SubmixVoice: Submixing voice management interface.
		 *
		 **************************************************************************/

		struct __declspec(novtable) IXAudio2SubmixVoice:
			public IXAudio2Voice
		{};


		/**************************************************************************
		 *
		 * IXAudio2MasteringVoice: Mastering voice management interface.
		 *
		 **************************************************************************/

		struct __declspec(novtable) IXAudio2MasteringVoice :
			public IXAudio2Voice
		{};

		// All structures defined in this file use tight field packing
		#pragma pack(push, 1)

		struct XAPO_REGISTRATION_PROPERTIES
		{
			constexpr static auto XAPO_REGISTRATION_STRING_LENGTH = 256;

			REX::W32::IID clsid;
			wchar_t friendlyName[XAPO_REGISTRATION_STRING_LENGTH];
			wchar_t copyrightInfo[XAPO_REGISTRATION_STRING_LENGTH];
			uint32_t majorVersion;
			uint32_t minorVersion;
			uint32_t flags;
			uint32_t minInputBufferCount;
			uint32_t maxInputBufferCount;
			uint32_t minOutputBufferCount;
			uint32_t maxOutputBufferCount;
		};
		static_assert(sizeof(XAPO_REGISTRATION_PROPERTIES) == 0x42C);

		struct XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS
		{
			const WAVEFORMATEX* format;
			uint32_t maxFrameCount;
		};
		static_assert(sizeof(XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS) == 0xC);

		enum class XAPO_BUFFER_FLAGS
		{
			XAPO_BUFFER_SILENT,
			XAPO_BUFFER_VALID,
		};

		struct XAPO_PROCESS_BUFFER_PARAMETERS
		{
			void* buffer;
			XAPO_BUFFER_FLAGS bufferFlags;
			uint32_t validFrameCount;
		};
		static_assert(sizeof(XAPO_PROCESS_BUFFER_PARAMETERS) == 0x10);

		struct __declspec(novtable) IXAPO : public IUnknown
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::IXAPO;

			// add
			virtual int32_t GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** a_registrationProperties) = 0;
			virtual int32_t IsInputFormatSupported(const WAVEFORMATEX* a_outputFormat, const WAVEFORMATEX* a_requestedInputFormat,
				WAVEFORMATEX** a_supportedInputFormat) = 0;
			virtual int32_t IsOutputFormatSupported(const WAVEFORMATEX* a_inputFormat, const WAVEFORMATEX* a_requestedOutputFormat,
				WAVEFORMATEX** a_supportedOutputFormat) = 0;
			virtual int32_t Initialize(const void* a_data, uint32_t a_dataByteSize) = 0;
			virtual void Reset() = 0;
			virtual int32_t LockForProcess(uint32_t a_inputLockedParameterCount,
				const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_inputLockedParameters,
				uint32_t a_outputLockedParameterCount,
				const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_outputLockedParameters) = 0;
			virtual void UnlockForProcess() = 0;
			virtual void Process(uint32_t a_inputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS* a_InputProcessParameters,
				uint32_t a_outputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS* a_outputProcessParameters,
				BOOL a_isEnabled) = 0;
			virtual uint32_t CalcInputFrames(uint32_t a_outputFrameCount) = 0;
			virtual uint32_t CalcOutputFrames(uint32_t a_inputFrameCount) = 0;
		};
		static_assert(sizeof(IXAPO) == 0x8);

		// Undo the #pragma pack(push, 1) at the top of this file
		#pragma pack(pop)
	}

	namespace AudioBethesdaSystem
	{
		// All structures defined in this file use tight field packing
#pragma pack(push, 8)

		class __declspec(novtable) CXAPOBase : public AudioSystem::IXAPO
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::CXAPOBase;

			// add
			virtual int32_t ValidateFormatDefault(WAVEFORMATEX* a_format, BOOL a_overwrite) = 0;
			virtual ~CXAPOBase() = default;

			[[nodiscard]] const AudioSystem::XAPO_REGISTRATION_PROPERTIES*
				GetRegistrationPropertiesInternal() const noexcept { return registrationProperties; }
			[[nodiscard]] BOOL IsLocked() const noexcept { return isLocked; }

			// members
			const AudioSystem::XAPO_REGISTRATION_PROPERTIES* registrationProperties;
			void* fnMatrixMixFunction;
			float* matrixCoefficients;
			uint32_t srcFormatType;
			BOOL isScalarMatrix;
			BOOL isLocked;
			int32_t referenceCount;
		};
		static_assert(sizeof(CXAPOBase) == 0x30);

		// Undo the #pragma pack(push, 8) at the top of this file
#pragma pack(pop)

		class __declspec(novtable) MonitorAPO : public CXAPOBase
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::__MonitorAPO;
			inline static constexpr auto VTABLE = RE::VTABLE::__MonitorAPO[0];

			// override (CXAPOBase)
			int32_t LockForProcess(uint32_t a_inputLockedParameterCount,
				const AudioSystem::XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_inputLockedParameters,
				uint32_t a_outputLockedParameterCount,
				const AudioSystem::XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_outputLockedParameters) = 0;

			void Process(uint32_t a_inputProcessParameterCount,
				const AudioSystem::XAPO_PROCESS_BUFFER_PARAMETERS* a_InputProcessParameters,
				uint32_t a_outputProcessParameterCount,
				AudioSystem::XAPO_PROCESS_BUFFER_PARAMETERS* a_outputProcessParameters,
				BOOL a_isEnabled) = 0;

			// members
			uint32_t numChannels;
			float amplitude;
		};
		static_assert(sizeof(MonitorAPO) == 0x38);

		struct BSXAudio2Monitor
		{
			// members
			MonitorAPO* monitorAPO{ nullptr };
			AudioSystem::IXAudio2SubmixVoice* submixVoice{ nullptr };
		};
		static_assert(sizeof(BSXAudio2Monitor) == 0x10);

		class __declspec(novtable) BSXAudio2Graph : public AudioSystem::IXAudio2EngineCallback
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::__BSXAudio2Graph;
			inline static constexpr auto VTABLE = RE::VTABLE::__BSXAudio2Graph[0];

			struct ReverbEffect
			{
				// Bethesda uses 2 presets like as DEFAULT
				std::array<AudioSystem::FX::XAUDIO2FX_REVERB_I3DL2_PARAMETERS, 2> presets
				{{
					AudioSystem::FX::XAUDIO2FX_I3DL2_PRESET_DEFAULT,
					AudioSystem::FX::XAUDIO2FX_I3DL2_PRESET_DEFAULT
				}};

				IUnknown* reverb{ nullptr };
				AudioSystem::IXAudio2SubmixVoice* submixVoice{ nullptr };
				std::array<void*, 2> unk{};
			};

			[[nodiscard]] static BSXAudio2Graph* GetSingleton()
			{
				static REL::Relocation<BSXAudio2Graph**> singleton{ REL::ID{ 1219921, 2703127 } };
				return *singleton;
			}

			[[nodiscard]] BSXAudio2Graph* Recreate()
			{
				using func_t = decltype(&BSXAudio2Graph::Recreate);
				static REL::Relocation<func_t> func{ REL::ID{ 799447, 2267547 } };
				return func(this);
			}

			void OnProcessingPassStart() noexcept override { return; }
			void OnProcessingPassEnd() noexcept override
			{
				using func_t = decltype(&BSXAudio2Graph::OnProcessingPassEnd);
				static REL::Relocation<func_t> func{ REL::ID{ 351273, 2267567 } };
				func(this);
			}
			void OnCriticalError([[maybe_unused]] HRESULT Error) noexcept override { return; }

			// add member
			AudioSystem::IXAudio2* xaudio;							// 008
			AudioSystem::IXAudio2MasteringVoice* masteringVoice;	// 010
			std::array<ReverbEffect, 2> effects;					// 018
			uint32_t channelMask;									// 128
			uint32_t currentDevice;									// 12C
			bool registerCallbacks;									// 130
			bool initEffects;										// 131
			bool initEngine;										// 132
		};
		static_assert(sizeof(BSXAudio2Graph::ReverbEffect) == 0x88);
		static_assert(sizeof(BSXAudio2Graph) == 0x138);

		namespace BSAudioMonitor
		{
			class Request
			{
			public:
				Request(uint16_t a_monitor, uint16_t a_sendLevel) :
					monitor(a_monitor),
					sendLevel(a_sendLevel)
				{}

				[[nodiscard]] inline uint32_t QID() const noexcept { return monitor; }
				[[nodiscard]] inline uint16_t QSendLevel() const noexcept { return sendLevel; }

				// members
				uint16_t monitor;
				uint16_t sendLevel;
			};
			static_assert(sizeof(Request) == 0x4);

			class Receiver
			{
			public:
				Receiver(const float& a_amplitude) :
					amplitude(std::addressof(a_amplitude))
				{}

				[[nodiscard]] inline float QAmplitude() const noexcept { return *amplitude; }

				// members
				const float* amplitude;
			};
			static_assert(sizeof(Receiver) == 0x8);
			static_assert(!REL::detail::is_x64_pod_v<Receiver>);
		}

		class __declspec(novtable) BSAudioListener
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BSAudioListener;
			inline static constexpr auto VTABLE = RE::VTABLE::BSAudioListener[0];

			virtual ~BSAudioListener() = default;

			// add
			virtual void SetPosition(const RE::NiPoint3& a_pos) = 0;
			virtual void Reset(float a_value = .0f) = 0;

			// members
			RE::NiPoint3 listenerPosition;
			uint8_t unk[0x68];
		};
		static_assert(sizeof(BSAudioListener) == 0x80);

		class __declspec(novtable) BSXAudio2AudioListener :
			public BSAudioListener
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BSXAudio2AudioListener;
			inline static constexpr auto VTABLE = RE::VTABLE::BSXAudio2AudioListener[0];
		};

		class __declspec(novtable) BSIReverbType
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BSIReverbType;
			inline static constexpr auto VTABLE = RE::VTABLE::BSIReverbType[0];

			// add
			[[nodiscard]] virtual uint32_t DoGetRoomLevel() const = 0;
			[[nodiscard]] virtual uint32_t DoGetRoomHFLevel() const = 0;
			[[nodiscard]] virtual float DoGetDecayTime() const = 0;
			[[nodiscard]] virtual float DoGetDecayHFRatio() const = 0;
			[[nodiscard]] virtual uint32_t DoGetReflectionLevel() const = 0;
			[[nodiscard]] virtual float DoGetReflectionDelay() const = 0;
			[[nodiscard]] virtual uint32_t DoGetReverbLevel() const = 0;
			[[nodiscard]] virtual float DoGetReverbDelay() const = 0;
			[[nodiscard]] virtual float DoGetDiffusion() const = 0;
			[[nodiscard]] virtual float DoGetDensity() const = 0;
			[[nodiscard]] virtual float DoGetHFReference() const = 0;
		};
		static_assert(sizeof(BSIReverbType) == 0x8);

		class BSISoundCategory;
		class BSISoundDescriptor;
		class BSISoundOutputModel;

		class SoundMessageList;
		class SoundMessageStack;

		namespace BSExternalAudioIO
		{
			class ExternalIOInterface;
			class ExternalLoad;
		}

		class BSGameSound;
		class BSSoundInfo;

		class __declspec(novtable) BSAudio
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BSAudio;
			inline static constexpr auto VTABLE = RE::VTABLE::BSAudio[0];

			virtual ~BSAudio() = default;

			// add
			[[nodiscard]] virtual bool Init() = 0;
			virtual void Shutdown() = 0;
			[[nodiscard]] virtual BSGameSound* GetGameSound(const RE::BSResource::ID& a_resourceID) = 0;
			virtual void ReleaseGameSound(BSGameSound* a_gameSound) = 0;
			[[nodiscard]] virtual const RE::BSFixedString& GetSystemName() = 0;
			virtual void ApplyReverbType(const uint8_t a_unk, const BSIReverbType* a_reverbType, uint32_t a_tickLength) = 0;
			virtual void Initialize3D() = 0;
			virtual void nullfunc_40() = 0;
			[[nodiscard]] virtual uint32_t CreateMonitor(float a_amplitude) = 0;
			virtual void ReleaseMonitor(uint32_t a_monitor) = 0;
			[[nodiscard]] virtual BSAudioMonitor::Receiver GetReceiver(uint32_t a_monitor) = 0;
			virtual void GetDeviceInfo() = 0;

			// members
			BSAudioListener* audioListener;	// 08
			uint32_t currentDevice;			// 10
			uint8_t init;					// 14 always 1
		};
		static_assert(sizeof(BSAudio) == 0x18);

		using BSXAudio2Monitors = RE::BSTSmallArray<BSXAudio2Monitor, 8>;

		class __declspec(novtable) BSXAudio2Audio :
			public BSAudio
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BSXAudio2Audio;
			inline static constexpr auto VTABLE = RE::VTABLE::BSXAudio2Audio[0];

			virtual ~BSXAudio2Audio() = default;  // 00

			[[nodiscard]] static BSXAudio2Audio* GetSingleton() noexcept
			{
				static REL::Relocation<BSXAudio2Audio**> singleton{ REL::ID{ 1565436, 2703127 } };
				return *singleton;
			}

			[[nodiscard]] static RE::BSSpinLock* GetSpinLock() noexcept
			{
				static REL::Relocation<RE::BSSpinLock*> spinlock{ REL::ID{ 1000104, 2703125 } };
				return spinlock.get();
			}

			[[nodiscard]] static BSXAudio2Monitors* GetActiveMonitor() noexcept
			{
				static REL::Relocation<BSXAudio2Monitors*> arr{ REL::ID{ 65846, 2666254 } };
				return arr.get();
			}

			[[nodiscard]] static BSXAudio2Monitors* GetInactiveMonitor() noexcept
			{
				static REL::Relocation<BSXAudio2Monitors*> arr{ REL::ID{ 850059, 2666257 } };
				return arr.get();
			}			

			// add member
			X3DAUDIO_HANDLE X3DAudioHandle;	// 18
		};
		static_assert(sizeof(BSXAudio2Audio) == 0x30);

		class __declspec(novtable) BSAudioManager :
			public RE::BSAudioManager
		{
		public:
			enum class State : uint32_t
			{
				ManagerInitialized = 1 << 0,
				PlatformInitialized = 1 << 1,
				PlatformInitFailed = 1 << 2,
				CacheEnabled = 1 << 3,
				ShuttingDown = 1 << 4,
				RunDisabled = 1 << 5
			};

			[[nodiscard]] inline uint8_t HasStateFlag(State a_state) const noexcept 
			{ return (stateFlags & std::to_underlying(a_state)) != 0; }
			inline void SetStateFlag(State a_state) noexcept { stateFlags |= std::to_underlying(a_state); }
			inline void UnsetStateFlag(State a_state) noexcept { stateFlags &= ~std::to_underlying(a_state); }

			inline void SetManagerInitialized(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::ManagerInitialized) : UnsetStateFlag(State::ManagerInitialized); }

			inline void SetPlatformInitialized(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::PlatformInitialized) : UnsetStateFlag(State::PlatformInitialized); }

			inline void SetPlatformInitFailed(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::PlatformInitFailed) : UnsetStateFlag(State::PlatformInitFailed); }

			inline void SetCacheEnabled(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::CacheEnabled) : UnsetStateFlag(State::CacheEnabled); }

			inline void SetShuttingDown(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::ShuttingDown) : UnsetStateFlag(State::ShuttingDown); }

			inline void SetRunDisabled(bool a_value) noexcept
			{ a_value ? SetStateFlag(State::RunDisabled) : UnsetStateFlag(State::RunDisabled); }

			[[nodiscard]] static BSAudio* QPlatformInstance() noexcept
			{
				using func_t = decltype(&BSAudioManager::QPlatformInstance);
				static REL::Relocation<func_t> func{ REL::ID{ 1117951, 2267094 } };
				return func();
			}

			[[nodiscard]] static bool QInitialized() noexcept
			{
				using func_t = decltype(&BSAudioManager::QInitialized);
				static REL::Relocation<func_t> func{ REL::ID{ 1556964, 2267095 } };
				return func();
			}

			void Init(RE::BSAudioInit& a_init) noexcept
			{
				using func_t = decltype(&BSAudioManager::Init);
				static REL::Relocation<func_t> func{ REL::ID{ 40086, 2267098 } };
				func(this, a_init);
			}

			void Shutdown() noexcept
			{
				using func_t = decltype(&BSAudioManager::Shutdown);
				static REL::Relocation<func_t> func{ REL::ID{ 64750, 2267099 } };
				func(this);
			}

			void ClearCache() noexcept
			{
				using func_t = decltype(&BSAudioManager::ClearCache);
				static REL::Relocation<func_t> func{ REL::ID{ 114073, 2267197 } };
				func(this);
			}

			void KillAll(bool a_waitForCompletion = false, uint32_t a_waitMs = 1000) noexcept
			{
				using func_t = decltype(&BSAudioManager::KillAll);
				static REL::Relocation<func_t> func{ REL::ID{ 580105, 2267108 } };
				func(this, a_waitForCompletion, a_waitMs);
			}

			void ClearMaps() noexcept
			{
				using func_t = decltype(&BSAudioManager::ClearMaps);
				static REL::Relocation<func_t> func{ REL::ID{ 583644, 2267194 } };
				func(this);
			}

			void Play(uint32_t a_soundFormID) const noexcept
			{
				using func_t = decltype(&BSAudioManager::Play);
				static REL::Relocation<func_t> func{ REL::ID{ 590936, 2267115 } };
				func(this, a_soundFormID);
			}

			void PlayAfter(uint32_t a_soundFormID, uint64_t a_waitms) const noexcept
			{
				using func_t = decltype(&BSAudioManager::PlayAfter);
				static REL::Relocation<func_t> func{ REL::ID{ 1108952, 2267116 } };
				func(this, a_soundFormID, a_waitms);
			}

			void SuspendAudioThread() noexcept
			{
				using func_t = decltype(&BSAudioManager::SuspendAudioThread);
				static REL::Relocation<func_t> func{ REL::ID{ 1366970, 2267100 } };
				func(this);
			}

			void ResumeAudioThread() noexcept
			{
				using func_t = decltype(&BSAudioManager::ResumeAudioThread);
				static REL::Relocation<func_t> func{ REL::ID{ 1274072, 2267101 } };
				func(this);
			}
		};

		class MoviePlayer
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::MoviePlayer;
			inline static constexpr auto VTABLE = RE::VTABLE::MoviePlayer[0];

			virtual ~MoviePlayer() = default;  // 00

			// add
			virtual void PauseAll() = 0;					// idk this both call TESAudio::PauseUnpauseAllAudio(bool,uint)
			virtual void UnPauseAll() = 0;					//
			virtual void PauseMusic() = 0;
			virtual void UnPauseMusic() = 0;
			virtual void nullfunc_28() = 0;
			virtual void nullfunc_30() = 0;
			virtual void LockRenderer() = 0;
			virtual void UnlockRenderer() = 0;
			virtual void unknown_48() = 0;
			virtual void unknown_50() = 0;
			virtual void unknown_58() = 0;
			virtual void QueueResourceDeletion(void*) = 0;	// need bink context
			virtual void unknown_68() = 0;
		};

		class BGSMoviePlayer :
			public MoviePlayer
		{
		public:
			inline static constexpr auto RTTI = RE::RTTI::BGSMoviePlayer;
			inline static constexpr auto VTABLE = RE::VTABLE::BGSMoviePlayer[0];

			virtual ~BGSMoviePlayer() = default;  // 00

			[[nodiscard]] static BGSMoviePlayer* GetSingleton() noexcept
			{
				return *REL::Relocation<BGSMoviePlayer**>(REL::ID{ 1429717, 2710691 });
			}
		};
	}

	namespace Hooks
	{
		struct XAudio
		{
			static REX::W32::HRESULT ThunkMasteringVoice(AudioSystem::IXAudio2* a_xaudio,
				AudioSystem::IXAudio2MasteringVoice** a_masteringVoice,
				uint32_t a_inputChannels, uint32_t a_inputSampleRate, uint32_t a_flags,
				uint32_t a_deviceIndex, const AudioSystem::XAUDIO2_EFFECT_CHAIN* a_effectChain);

			using TThunkMasteringVoice = decltype(ThunkMasteringVoice);

			static REX::W32::HRESULT Thunk1(REFCLSID a_rclsid, IUnknown* a_unkOuter, uint32_t a_clsContext,
				REFIID a_riid, void** a_ppv);
			static bool Install() noexcept;

			inline static std::function<TThunkMasteringVoice> originalMasteringVoice{};
		};

		struct Bink
		{
			using TBinkOpen2 = int32_t(*(*)(void*, void*))(void*, int32_t, int32_t, void*, void*);
			using TBinkSetSoundSystem2 = void (*)(TBinkOpen2, void*, void*);

			inline static TBinkOpen2 BinkOpenXAudio2{ nullptr };
			inline static TBinkSetSoundSystem2 BinkSetSoundSystem2{ nullptr };

			static void ThunkSetSoundSystem();
			static bool Install() noexcept;
		};

		struct Callbacks
		{
			static void ThunkDoCriticalError([[maybe_unused]] REX::W32::HRESULT a_herror);
			static bool Install() noexcept;
		};

		struct ProcessSound
		{
			static void ThunkSignal() noexcept;

			using TThunkSignal = void(*)();

			static bool Install() noexcept;
			inline static TThunkSignal original{ nullptr };
		};
	}

	namespace AudioEngine
	{
		static bool RetryAudio{ false };
		static AudioSystem::XAUDIO2_DEVICE_DETAILS CurrentDevice{};
		static AudioSystem::IXAudio2* Engine{ nullptr };
		static AudioSystem::IXAudio2MasteringVoice* MasteringVoice{ nullptr };
		static RELEX::ScopeEvent UpdateEvent{ true, false, "FO4__AudioEngine__UpdateEvent"sv };
		static RE::BSSpinLock* AudioMutex{ nullptr };

		static void KillGameSounds(AudioBethesdaSystem::BSAudioManager* a_audioManager)
		{
			a_audioManager->ClearMaps();
			a_audioManager->ClearCache();
		}

		static void KillAudioMonitors() noexcept
		{
			RE::BSAutoLock scope_lock(AudioBethesdaSystem::BSXAudio2Audio::GetSpinLock());

			auto activeMonitors = AudioBethesdaSystem::BSXAudio2Audio::GetActiveMonitor();
			auto inactiveMonitors = AudioBethesdaSystem::BSXAudio2Audio::GetInactiveMonitor();

			for (auto& monitor : *activeMonitors)
				if (monitor.submixVoice)
					std::exchange(monitor.submixVoice, nullptr)->DestroyVoice();

			for (auto& monitor : *inactiveMonitors)
			{
				if (monitor.submixVoice)
					std::exchange(monitor.submixVoice, nullptr)->DestroyVoice();

				if (monitor.monitorAPO)
					delete std::exchange(monitor.monitorAPO, nullptr);
			}

			inactiveMonitors->clear();
		}

		static void KillEngine() noexcept
		{
			if (Engine)
				Engine->StopEngine();

			auto graph = AudioBethesdaSystem::BSXAudio2Graph::GetSingleton();
			if (graph)
			{
				for (auto& effect : graph->effects)
					if (effect.submixVoice)
						std::exchange(effect.submixVoice, nullptr)->DestroyVoice();

				Engine->UnregisterForCallbacks(graph);

				std::exchange(graph->masteringVoice, nullptr)->DestroyVoice();
				MasteringVoice = nullptr;

				if (graph->xaudio)
					std::exchange(graph->xaudio, nullptr)->Release();

				Engine = nullptr;

				graph->initEngine = false;
				graph->initEffects = false;
				graph->registerCallbacks = false;
			}

			// idk how restore
			//auto audio = AudioBethesdaSystem::BSXAudio2Audio::GetSingleton();
			//if (audio && audio->audioListener)
			//	delete std::exchange(audio->audioListener, nullptr);
		}

		static void SilentMode(AudioBethesdaSystem::BSAudioManager* a_audioManager)
		{
			KillGameSounds(a_audioManager);
			KillAudioMonitors();
			KillEngine();

			a_audioManager->SetManagerInitialized(false);	// important: needs disable it, otherwise, 
															// the game will still try to play the sounds.
			a_audioManager->SetPlatformInitialized(false);
			a_audioManager->SetPlatformInitFailed(false);
		}

		static void Reset(AudioBethesdaSystem::BSAudioManager* a_audioManager) noexcept
		{
			using namespace AudioBethesdaSystem;

			if (!Engine || !MasteringVoice)
				return;

			if (Engine)
			{
				std::uint32_t deviceCount{};
				Engine->GetDeviceCount(std::addressof(deviceCount));
				if (deviceCount == 0)
					return;
			}

			SilentMode(a_audioManager);

			REX::INFO("Reinitializing XAudio2..."sv);

			auto audio = AudioBethesdaSystem::BSXAudio2Audio::GetSingleton();
			auto graph = AudioBethesdaSystem::BSXAudio2Graph::GetSingleton();
			if (audio && graph && graph->Recreate() && graph->xaudio)
			{
				graph->registerCallbacks = SUCCEEDED(Engine->RegisterForCallbacks(graph));
				
				// Bethesda don't use X3DAUDIO_SPEED_OF_SOUND... they send magick value 24041.6
				static REL::Relocation<float*> speed{ REL::ID{ 207777, 207777, 4563742 } };
				X3DAudioInitialize(graph->channelMask, *speed, audio->X3DAudioHandle);

				Hooks::Bink::ThunkSetSoundSystem();

				a_audioManager->SetPlatformInitialized(true);
				a_audioManager->SetManagerInitialized(true);
			}
			else
			{
				a_audioManager->SetPlatformInitFailed(true);
				a_audioManager->ClearMaps();
				a_audioManager->ClearCache();
			}
			
			UpdateEvent.Reset();
		}

		static bool Update() noexcept
		{
			using enum RELEX::ScopeEvent::Result;

			switch (UpdateEvent.WaitFor(0))
			{
			default:
			case WaitTimeout:
				break;
			case WaitObject0:
				return false;
			case WaitFailed:
				REX::ERROR("WaitForSingleObjectEx failed"sv);
				break;
			}

			return true;
		}
	}

	namespace Hooks
	{
		REX::W32::HRESULT XAudio::ThunkMasteringVoice(AudioSystem::IXAudio2* a_xaudio,
			AudioSystem::IXAudio2MasteringVoice** a_masteringVoice,
			uint32_t a_inputChannels, uint32_t a_inputSampleRate, uint32_t a_flags,
			uint32_t a_deviceIndex, const AudioSystem::XAUDIO2_EFFECT_CHAIN* a_effectChain)
		{
			uint32_t deviceCount{};
			auto hr = a_xaudio->GetDeviceCount(std::addressof(deviceCount));
			if (FAILED(hr))
				return hr;

			REX::INFO("[XAudio2] {} audio devices available"sv, deviceCount);

			hr = a_xaudio->GetDeviceDetails(a_deviceIndex, std::addressof(AudioEngine::CurrentDevice));
			if (FAILED(hr))
				return hr;

			REX::INFO(L"[XAudio2] Using device at index {}: {}"sv, a_deviceIndex, AudioEngine::CurrentDevice.DisplayName);

			hr = originalMasteringVoice(a_xaudio, a_masteringVoice, a_inputChannels,
				a_inputSampleRate, a_flags, a_deviceIndex, a_effectChain);
			if (SUCCEEDED(hr) && a_masteringVoice && *a_masteringVoice)
				AudioEngine::MasteringVoice = *a_masteringVoice;

			return hr;
		}

		REX::W32::HRESULT XAudio::Thunk1(REFCLSID a_rclsid, IUnknown* a_unkOuter, uint32_t a_clsContext,
			REFIID a_riid, void** a_ppv)
		{
			auto hr = CoCreateInstance(a_rclsid, a_unkOuter, a_clsContext, a_riid, a_ppv);
			if (SUCCEEDED(hr) && a_ppv && *a_ppv)
			{
				static std::once_flag once;
				std::call_once(once, [&]() {
					const auto vfptr = *reinterpret_cast<std::uintptr_t**>(a_ppv);
					originalMasteringVoice = reinterpret_cast<TThunkMasteringVoice*>(RELEX::DetourVTable(*vfptr,
						reinterpret_cast<uintptr_t>(&ThunkMasteringVoice), 10));
					});
				AudioEngine::Engine = *reinterpret_cast<AudioSystem::IXAudio2**>(a_ppv);
			}
			return hr;
		}

		bool XAudio::Install() noexcept
		{
			REL::Relocation thumb(REL::ID{ 303985, 2267547 }, REL::Offset{ 0x43, 0x108 });

			if (RELEX::Validate(thumb.address(), { 0xFF, 0x15 }))
				return RELEX::DetourCall(thumb.address(), reinterpret_cast<uintptr_t>(&Thunk1)) != 0;

			return false;
		}

		void Bink::ThunkSetSoundSystem()
		{
			if (AudioEngine::Engine && AudioEngine::MasteringVoice)
				BinkSetSoundSystem2(BinkOpenXAudio2, AudioEngine::Engine, AudioEngine::MasteringVoice);
		}

		bool Bink::Install() noexcept
		{
			const auto bink2w64 = REX::W32::GetModuleHandle("bink2w64.dll");
			if (!bink2w64)
				return false;

			BinkOpenXAudio2 = reinterpret_cast<TBinkOpen2>(
				REX::W32::GetProcAddress(bink2w64, "BinkOpenXAudio2"));
			BinkSetSoundSystem2 = reinterpret_cast<TBinkSetSoundSystem2>(
				REX::W32::GetProcAddress(bink2w64, "BinkSetSoundSystem2"));

			if (!BinkOpenXAudio2 || !BinkSetSoundSystem2)
				return false;

			REL::Relocation thumb(REL::ID{ 143766, 2300587 }, REL::Offset{ 0x4 });
			if (RELEX::Validate(thumb.address(), { 0x48, 0x8B, 0x0D }))
			{
				RELEX::WriteSafeNop(thumb.address(), 0x10);
				return RELEX::DetourJump(thumb.address(),
					reinterpret_cast<uintptr_t>(&ThunkSetSoundSystem)) != 0;
			}

			return false;
		}

		void Callbacks::ThunkDoCriticalError([[maybe_unused]] REX::W32::HRESULT a_herror)
		{
			//REX::WARN("XAudio2 encountered critical error ({:08X})", static_cast<std::uint32_t>(a_herror));

			if (AudioBethesdaSystem::BSAudioManager::QInitialized())
				AudioEngine::UpdateEvent.Set();
		}

		bool Callbacks::Install() noexcept
		{
			return RELEX::DetourVTable(REL::ID(AudioBethesdaSystem::BSXAudio2Graph::VTABLE).address(),
				reinterpret_cast<uintptr_t>(&ThunkDoCriticalError), 2) != 0;
		}

		void ProcessSound::ThunkSignal() noexcept
		{
			auto audioManager = reinterpret_cast<AudioBethesdaSystem::BSAudioManager*>
				(AudioBethesdaSystem::BSAudioManager::GetSingleton());
			if (audioManager && (audioManager->audioThreadID == REX::W32::GetCurrentThreadId()))
			{
				if (AudioEngine::RetryAudio)
				{
					AudioEngine::Reset(audioManager);
					AudioEngine::RetryAudio = false;
					return;
				}
				else if (!AudioEngine::Update())
				{
					AudioEngine::RetryAudio = true;
					return;
				}
			}
			else if (AudioEngine::RetryAudio)
				return;

			original();
		}

		bool ProcessSound::Install() noexcept
		{
			REL::Relocation thumb(REL::ID{ 225971, 2267207 }, REL::Offset{ 0x34, 0x36 });
			if (RELEX::Validate(thumb.address(), { 0xE8 }))
			{
				*(uintptr_t*)&original = REL::ID{ 1450714, 2268796 }.address();
				return RELEX::DetourCall(thumb.address(),
					reinterpret_cast<uintptr_t>(&ThunkSignal)) != 0;
			}

			return false;
		}
	}

	// Audio Device Notification
	// https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-events
	namespace AudioDeviceNotification
	{
		using namespace Microsoft::WRL;

		class NotificationClient : public IMMNotificationClient
		{
		private:
			LONG _cRef;
			ComPtr<IMMDeviceEnumerator> _pEnumerator;

		public:
			NotificationClient() : _cRef(1) {}
			~NotificationClient() { Unregister(); }

			// Register / Unregister
			HRESULT Register() noexcept
			{
				// Create _pEnumerator
				auto hr = CoCreateInstance(
					__uuidof(MMDeviceEnumerator),
					NULL, CLSCTX_INPROC_SERVER,
					__uuidof(IMMDeviceEnumerator),
					(void**)_pEnumerator.ReleaseAndGetAddressOf());

				if (FAILED(hr))
					return hr;

				// Register Callback
				hr = _pEnumerator->RegisterEndpointNotificationCallback(this);
				if (FAILED(hr))
					_pEnumerator.Reset();

				return hr;
			}

			void Unregister() noexcept
			{
				if (_pEnumerator)
				{
					_pEnumerator->UnregisterEndpointNotificationCallback(this);
					_pEnumerator.Reset();
				}
			}

			// IUnknown Methods
			ULONG STDMETHODCALLTYPE AddRef()
			{
				return InterlockedIncrement(&_cRef);
			}

			ULONG STDMETHODCALLTYPE Release()
			{
				ULONG ulRef = InterlockedDecrement(&_cRef);
				if (0 == ulRef)
					delete this;

				return ulRef;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface)
			{
				if (IID_IUnknown == riid)
				{
					AddRef();
					*ppvInterface = (IUnknown*)this;
				}
				else if (__uuidof(IMMNotificationClient) == riid)
				{
					AddRef();
					*ppvInterface = (IMMNotificationClient*)this;
				}
				else
				{
					*ppvInterface = NULL;
					return E_NOINTERFACE;
				}

				return S_OK;
			}

			// Callback Methods
			HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
			{
				// We only care about Playback (eRender) for the Console Device Role (eConsole)
				// https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles
				if (flow == eRender && role == eConsole && pwstrDeviceId)
				{
					if (AudioBethesdaSystem::BSAudioManager::QInitialized())
						AudioEngine::UpdateEvent.Set();
				}

				return S_OK;
			}

			// Unused
			HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) { return S_OK; }
			HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) { return S_OK; }
		};

		static ComPtr<NotificationClient> Client;

		bool Install() noexcept
		{
			// Create the Notification Client
			Client.Attach(new (std::nothrow) NotificationClient());
			if (!Client)
				return false;

			// Register the Notification Client
			const auto hr = Client->Register();
			if (FAILED(hr))
			{
				Client.Reset();
				return false;
			}

			return true;
		}
	}

	ModuleAudioSwitch::ModuleAudioSwitch() :
		Module("Audio Switch", &bPatchesAudioSwitch)
	{}

	bool ModuleAudioSwitch::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAudioSwitch::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (AudioEngine::UpdateEvent.Empty())
			return false;

		if (Hooks::XAudio::Install())
			REX::INFO("Hook for XAudio2 installed"sv);
		else
			return false;

		if (Hooks::Bink::Install())
			REX::INFO("Hook for Bink installed"sv);
		else
			return false;

		if (Hooks::Callbacks::Install())
			REX::INFO("Hook for Callbacks installed"sv);
		else
			return false;

		if (Hooks::ProcessSound::Install())
			REX::INFO("Hook for ProcessSound installed"sv);
		else
			return false;

		if (AudioDeviceNotification::Install())
			REX::INFO("Registered for Audio Device Notifications"sv);
		else
			REX::WARN("Failed to register for Audio Device Notifications"sv);

		AudioEngine::AudioMutex = reinterpret_cast<RE::BSSpinLock*>(REL::ID{ 210823, 2703076 }.address());

		return true;
	}

	bool ModuleAudioSwitch::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAudioSwitch::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
