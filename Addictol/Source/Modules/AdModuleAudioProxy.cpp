#include <Modules/AdModuleAudioProxy.h>
#include <AdUtils.h>

#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <comdef.h>
#include <functional>

#define AD_USE_FAUDIO 1
#define AD_USE_CHECKUPDATE_AUDIODEVICE 0

#if AD_USE_FAUDIO
#	include <FAudio.h>
#else
#	include <xaudio2.h>
#	include <xaudio2fx.h>
#	pragma comment(lib, "xaudio2.lib")
#endif

#undef ERROR
#undef MAX_PATH
#undef MEM_RELESE

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesAudioProxy{ "Patches"sv, "bAudioProxy"sv, true };

	namespace detail
	{
		constexpr static GUID IID_IXAudio2_7{ 0x8bcf1f58, 0x9fe7, 0x4583, 0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb };

		class IXAudio2Proxy;
		class IXAudio2VoiceProxy;
		class IXAudio2SourceVoiceProxy;
		class IXAudio2SubmixVoiceProxy;
		class IXAudio2MasteringVoiceProxy;

		// All structures defined in this file use tight field packing
		#pragma pack(push, 1)

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
			IXAudio2VoiceProxy* pOutputVoice;	// This send's destination voice.
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
			IUnknown* pEffect;					// Pointer to the effect object's IUnknown interface.
			BOOL InitialState;					// TRUE if the effect should begin in the enabled state.
			UINT32 OutputChannels;				// How many output channels the effect should produce.
		} XAUDIO2_EFFECT_DESCRIPTOR;

		// Used in the voice creation functions and in IXAudio2Voice::SetEffectChain
		typedef struct XAUDIO2_EFFECT_CHAIN
		{
			UINT32 EffectCount;								// Number of effects in this voice's effect chain.
			XAUDIO2_EFFECT_DESCRIPTOR* pEffectDescriptors;	// Array of effect descriptors.
		} XAUDIO2_EFFECT_CHAIN;

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

		#pragma pack(pop)

		// These methods are declared in a macro so that the same declarations
		// can be used in the derived voice types (IXAudio2SourceVoice, etc).
		class IXAudio2VoiceProxy
		{
		protected:
#if AD_USE_FAUDIO
			FAudioVoice* data{ nullptr };
#else
			::IXAudio2Voice* data{ nullptr };
#endif
			
		public:
			friend class IXAudio2Proxy;

			// NAME: IXAudio2Voice::GetVoiceDetails
			// DESCRIPTION: Returns the basic characteristics of this voice.
			//
			// ARGUMENTS:
			//  pVoiceDetails - Returns the voice's details.
			//
			virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* pVoiceDetails) const noexcept
			{
				if (pVoiceDetails && data)
				{
#if AD_USE_FAUDIO
					FAudioVoiceDetails details{};
					FAudioVoice_GetVoiceDetails(data, std::addressof(details));
#else
					::XAUDIO2_VOICE_DETAILS details{};
					data->GetVoiceDetails(std::addressof(details));
#endif
					pVoiceDetails->CreationFlags = details.CreationFlags;
					pVoiceDetails->InputChannels = details.InputChannels;
					pVoiceDetails->InputSampleRate = details.InputSampleRate;
				}
			}

			// NAME: IXAudio2Voice::SetOutputVoices
			// DESCRIPTION: Replaces the set of submix/mastering voices that receive
			//              this voice's output.
			//
			// ARGUMENTS:
			//  pSendList - Optional list of voices this voice should send audio to.
			//
			virtual HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS* pSendList) noexcept
			{
				if (data)
				{
#if AD_USE_FAUDIO
					if (!pSendList || !pSendList->SendCount)
						FAudioVoice_SetOutputVoices(data, nullptr);
					else
					{
						FAudioVoiceSends sends{};

						sends.SendCount = pSendList->SendCount;
						sends.pSends = new FAudioSendDescriptor[pSendList->SendCount];
						if (!sends.pSends) return E_OUTOFMEMORY;
						for (UINT32 i = 0; i < pSendList->SendCount; i++)
						{
							sends.pSends[i].Flags = pSendList->pSends[i].Flags;
							sends.pSends[i].pOutputVoice =
								pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
						}

						FAudioVoice_SetOutputVoices(data, std::addressof(sends));

						delete[] sends.pSends;
					}

					return S_OK;
#else
					if (!pSendList || !pSendList->SendCount)
						return data->SetOutputVoices(nullptr);
					else
					{
						::XAUDIO2_VOICE_SENDS sends{};

						sends.SendCount = pSendList->SendCount;
						sends.pSends = new ::XAUDIO2_SEND_DESCRIPTOR[pSendList->SendCount];
						if (!sends.pSends) return E_OUTOFMEMORY;
						for (UINT32 i = 0; i < pSendList->SendCount; i++)
						{
							sends.pSends[i].Flags = pSendList->pSends[i].Flags;
							sends.pSends[i].pOutputVoice =
								pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
						}

						auto hr = data->SetOutputVoices(std::addressof(sends));
						delete[] sends.pSends;
						return hr;
					}
#endif
				}

				return E_FAIL;
			}

			// NAME: IXAudio2Voice::SetEffectChain
			// DESCRIPTION: Replaces this voice's current effect chain with a new one.
			//
			// ARGUMENTS:
			//  pEffectChain - Structure describing the new effect chain to be used.
			//
			virtual HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN* pEffectChain) noexcept
			{
				if (!data || !pEffectChain)
					return E_FAIL;

				return S_OK; //data->SetEffectChain(reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
			}

			// NAME: IXAudio2Voice::EnableEffect
			// DESCRIPTION: Enables an effect in this voice's effect chain.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT EnableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) noexcept
			{
				if (!data)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_EnableEffect(data, EffectIndex, OperationSet) ? S_OK : E_FAIL;
#else
				return data->EnableEffect(EffectIndex, OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::DisableEffect
			// DESCRIPTION: Disables an effect in this voice's effect chain.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT DisableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) noexcept
			{
				if (!data)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_DisableEffect(data, EffectIndex, OperationSet) ? S_OK : E_FAIL;
#else
				return data->DisableEffect(EffectIndex, OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::GetEffectState
			// DESCRIPTION: Returns the running state of an effect.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  pEnabled - Returns the enabled/disabled state of the given effect.
			//
			virtual void GetEffectState(UINT32 EffectIndex, BOOL* pEnabled) const noexcept
			{
				if (!data || !pEnabled)
					return;
#if AD_USE_FAUDIO
				// BOOL is int
				FAudioVoice_GetEffectState(data, EffectIndex, pEnabled);
#else
				data->GetEffectState(EffectIndex, pEnabled);
#endif
			}

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
			virtual HRESULT SetEffectParameters(UINT32 EffectIndex, const void* pParameters,
				UINT32 ParametersByteSize, UINT32 OperationSet = 0) noexcept
			{
				if (!data || !pParameters)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetEffectParameters(data, EffectIndex, pParameters, ParametersByteSize,
					OperationSet) ? S_OK : E_FAIL;
#else
				if (ParametersByteSize == sizeof(XAUDIO2FX_REVERB_PARAMETERS))
				{
					::XAUDIO2FX_REVERB_PARAMETERS reverb{};
					auto srcReverb = reinterpret_cast<const XAUDIO2FX_REVERB_PARAMETERS*>(pParameters);

					reverb.WetDryMix = srcReverb->WetDryMix;
					reverb.ReflectionsDelay = srcReverb->ReflectionsDelay;
					reverb.ReverbDelay = srcReverb->ReverbDelay;
					reverb.RearDelay = srcReverb->RearDelay;
					reverb.SideDelay = XAUDIO2FX_REVERB_DEFAULT_7POINT1_SIDE_DELAY;
					reverb.PositionLeft = srcReverb->PositionLeft;
					reverb.PositionRight = srcReverb->PositionRight;
					reverb.PositionMatrixLeft = srcReverb->PositionMatrixLeft;
					reverb.PositionMatrixRight = srcReverb->PositionMatrixRight;
					reverb.EarlyDiffusion = srcReverb->EarlyDiffusion;
					reverb.LateDiffusion = srcReverb->LateDiffusion;
					reverb.LowEQGain = srcReverb->LowEQGain;
					reverb.LowEQCutoff = srcReverb->LowEQCutoff;
					reverb.HighEQGain = srcReverb->HighEQGain;
					reverb.HighEQCutoff = srcReverb->HighEQCutoff;
					reverb.RoomFilterFreq = srcReverb->RoomFilterFreq;
					reverb.RoomFilterMain = srcReverb->RoomFilterMain;
					reverb.RoomFilterHF = srcReverb->RoomFilterHF;
					reverb.ReflectionsGain = srcReverb->ReflectionsGain;
					reverb.ReverbGain = srcReverb->ReverbGain;
					reverb.DecayTime = srcReverb->DecayTime;
					reverb.Density = srcReverb->Density;
					reverb.RoomSize = srcReverb->RoomSize;
					reverb.DisableLateField = XAUDIO2FX_REVERB_DEFAULT_DISABLE_LATE_FIELD;
					
					return data->SetEffectParameters(EffectIndex, std::addressof(reverb), sizeof(reverb), OperationSet);
				}

				return E_FAIL;
#endif
			}

			// NAME: IXAudio2Voice::GetEffectParameters
			// DESCRIPTION: Obtains the current effect-specific parameters.
			//
			// ARGUMENTS:
			//  EffectIndex - Index of an effect within this voice's effect chain.
			//  pParameters - Returns the current values of the effect-specific parameters.
			//  ParametersByteSize - Size of the pParameters array in bytes.
			//
			virtual HRESULT GetEffectParameters(UINT32 EffectIndex, void* pParameters,
				UINT32 ParametersByteSize) const noexcept
			{
				if (!data || !pParameters)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_GetEffectParameters(data, EffectIndex, pParameters, ParametersByteSize) ? S_OK : E_FAIL;
#else
				if (ParametersByteSize == sizeof(XAUDIO2FX_REVERB_PARAMETERS))
				{
					::XAUDIO2FX_REVERB_PARAMETERS reverb{};
					auto hr = data->GetEffectParameters(EffectIndex, pParameters, ParametersByteSize);
					
					if (SUCCEEDED(hr))
					{
						auto parms = reinterpret_cast<XAUDIO2FX_REVERB_PARAMETERS*>(pParameters);

						parms->WetDryMix = reverb.WetDryMix;
						parms->ReflectionsDelay = reverb.ReflectionsDelay;
						parms->ReverbDelay = reverb.ReverbDelay;
						parms->RearDelay = reverb.RearDelay;
						parms->PositionLeft = reverb.PositionLeft;
						parms->PositionRight = reverb.PositionRight;
						parms->PositionMatrixLeft = reverb.PositionMatrixLeft;
						parms->PositionMatrixRight = reverb.PositionMatrixRight;
						parms->EarlyDiffusion = reverb.EarlyDiffusion;
						parms->LateDiffusion = reverb.LateDiffusion;
						parms->LowEQGain = reverb.LowEQGain;
						parms->LowEQCutoff = reverb.LowEQCutoff;
						parms->HighEQGain = reverb.HighEQGain;
						parms->HighEQCutoff = reverb.HighEQCutoff;
						parms->RoomFilterFreq = reverb.RoomFilterFreq;
						parms->RoomFilterMain = reverb.RoomFilterMain;
						parms->RoomFilterHF = reverb.RoomFilterHF;
						parms->ReflectionsGain = reverb.ReflectionsGain;
						parms->ReverbGain = reverb.ReverbGain;
						parms->DecayTime = reverb.DecayTime;
						parms->Density = reverb.Density;
						parms->RoomSize = reverb.RoomSize;
					}

					return hr;
				}

				return E_FAIL;
#endif
			}

			// NAME: IXAudio2Voice::SetFilterParameters
			// DESCRIPTION: Sets this voice's filter parameters.
			//
			// ARGUMENTS:
			//  pParameters - Pointer to the filter's parameter structure.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* pParameters,
				UINT32 OperationSet = 0) noexcept
			{
				if (!data || !pParameters)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetFilterParameters(data, reinterpret_cast<const FAudioFilterParameters*>(pParameters),
					OperationSet) ? S_OK : E_FAIL;
#else
				return data->SetFilterParameters(reinterpret_cast<const ::XAUDIO2_FILTER_PARAMETERS*>(pParameters),
					OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::GetFilterParameters
			// DESCRIPTION: Returns this voice's current filter parameters.
			//
			// ARGUMENTS:
			//  pParameters - Returns the filter parameters.
			//
			virtual void GetFilterParameters(XAUDIO2_FILTER_PARAMETERS* pParameters) const noexcept
			{
				if (!data || !pParameters)
					return;
#if AD_USE_FAUDIO
				FAudioVoice_GetFilterParameters(data, reinterpret_cast<FAudioFilterParameters*>(pParameters));
#else
				data->GetFilterParameters(reinterpret_cast<::XAUDIO2_FILTER_PARAMETERS*>(pParameters));
#endif
			}

			// NAME: IXAudio2Voice::SetOutputFilterParameters
			// DESCRIPTION: Sets the filter parameters on one of this voice's sends.
			//
			// ARGUMENTS:
			//  pDestinationVoice - Destination voice of the send whose filter parameters will be set.
			//  pParameters - Pointer to the filter's parameter structure.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT SetOutputFilterParameters(IXAudio2VoiceProxy* pDestinationVoice, 
				const XAUDIO2_FILTER_PARAMETERS* pParameters, UINT32 OperationSet = 0) noexcept
			{
				if (!data || !pDestinationVoice || !pDestinationVoice->data || !pParameters)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetOutputFilterParameters(data, pDestinationVoice->data,
					reinterpret_cast<const FAudioFilterParameters*>(pParameters), OperationSet) ? S_OK : E_FAIL;
#else
				return data->SetOutputFilterParameters(pDestinationVoice->data,
					reinterpret_cast<const ::XAUDIO2_FILTER_PARAMETERS*>(pParameters), OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::GetOutputFilterParameters
			// DESCRIPTION: Returns the filter parameters from one of this voice's sends.
			//
			// ARGUMENTS:
			//  pDestinationVoice - Destination voice of the send whose filter parameters will be read.
			//  pParameters - Returns the filter parameters.
			//
			virtual void GetOutputFilterParameters(IXAudio2VoiceProxy* pDestinationVoice,
				XAUDIO2_FILTER_PARAMETERS* pParameters) const noexcept
			{
				if (!data || !pDestinationVoice || !pDestinationVoice->data || !pParameters)
					return;
#if AD_USE_FAUDIO
				// XAUDIO2_FILTER_PARAMETERS matches with FAudioFilterParameters
				FAudioVoice_GetOutputFilterParameters(data, pDestinationVoice->data,
					reinterpret_cast<FAudioFilterParameters*>(pParameters));
#else
				data->GetOutputFilterParameters(pDestinationVoice->data,
					reinterpret_cast<::XAUDIO2_FILTER_PARAMETERS*>(pParameters));
#endif
			}

			// NAME: IXAudio2Voice::SetVolume
			// DESCRIPTION: Sets this voice's overall volume level.
			//
			// ARGUMENTS:
			//  Volume - New overall volume level to be used, as an amplitude factor.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT SetVolume(float Volume, UINT32 OperationSet = 0) noexcept
			{
				if (!data)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetVolume(data, Volume, OperationSet) ? S_OK : E_FAIL;
#else
				return data->SetVolume(Volume, OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::GetVolume
			// DESCRIPTION: Obtains this voice's current overall volume level.
			//
			// ARGUMENTS:
			//  pVolume: Returns the voice's current overall volume level.
			//
			virtual void GetVolume(float* pVolume) const noexcept
			{
				if (!data || !pVolume)
					return;
#if AD_USE_FAUDIO
				FAudioVoice_GetVolume(data, pVolume);
#else
				data->GetVolume(pVolume);
#endif
			}

			// NAME: IXAudio2Voice::SetChannelVolumes
			// DESCRIPTION: Sets this voice's per-channel volume levels.
			//
			// ARGUMENTS:
			//  Channels - Used to confirm the voice's channel count.
			//  pVolumes - Array of per-channel volume levels to be used.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT SetChannelVolumes(UINT32 Channels, const float* pVolumes,
				UINT32 OperationSet = 0) noexcept
			{
				if (!data || !pVolumes)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetChannelVolumes(data, Channels,
					pVolumes, OperationSet) ? S_OK : E_FAIL;
#else
				return data->SetChannelVolumes(Channels, pVolumes, OperationSet);
#endif
			}

			// NAME: IXAudio2Voice::GetChannelVolumes
			// DESCRIPTION: Returns this voice's current per-channel volume levels.
			//
			// ARGUMENTS:
			//  Channels - Used to confirm the voice's channel count.
			//  pVolumes - Returns an array of the current per-channel volume levels.
			//
			virtual void GetChannelVolumes(UINT32 Channels, float* pVolumes) const noexcept
			{
				if (!data || !pVolumes)
					return;
#if AD_USE_FAUDIO
				FAudioVoice_GetChannelVolumes(data, Channels, pVolumes);
#else
				data->GetChannelVolumes(Channels, pVolumes);
#endif
			}

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
			virtual HRESULT SetOutputMatrix(IXAudio2VoiceProxy* pDestinationVoice,
				UINT32 SourceChannels, UINT32 DestinationChannels, const float* pLevelMatrix,
				UINT32 OperationSet = 0) noexcept
			{
				if (!data || !pDestinationVoice || !pDestinationVoice->data || !pLevelMatrix)
					return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioVoice_SetOutputMatrix(data, pDestinationVoice->data, SourceChannels,
					DestinationChannels, pLevelMatrix, OperationSet) ? S_OK : E_FAIL;
#else
				auto hr = data->SetOutputMatrix(pDestinationVoice->data, SourceChannels,
					DestinationChannels, pLevelMatrix, OperationSet);				
				return hr;
#endif
			}

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
			virtual void GetOutputMatrix(IXAudio2VoiceProxy* pDestinationVoice,
				UINT32 SourceChannels, UINT32 DestinationChannels, float* pLevelMatrix) const noexcept
			{
				if (!data || !pDestinationVoice || !pDestinationVoice->data || !pLevelMatrix)
					return;
#if AD_USE_FAUDIO
				FAudioVoice_GetOutputMatrix(data, pDestinationVoice->data, SourceChannels, DestinationChannels,
					pLevelMatrix);
#else
				data->GetOutputMatrix(pDestinationVoice->data, SourceChannels, DestinationChannels, pLevelMatrix);
#endif
			}

			// NAME: IXAudio2Voice::DestroyVoice
			// DESCRIPTION: Destroys this voice, stopping it if necessary and removing
			//              it from the XAudio2 graph.
			//
			virtual void DestroyVoice() noexcept
			{
				if (data)
				{
#if AD_USE_FAUDIO
					FAudioVoice_DestroyVoice(data);
#else
					data->DestroyVoice();
#endif
					data = nullptr;
				}
			}
		};

		// All structures defined in this file use tight field packing
		#pragma pack(push, 1)

		// Used in IXAudio2SourceVoice::SubmitSourceBuffer
		typedef struct XAUDIO2_BUFFER
		{
			UINT32 Flags;						// Either 0 or XAUDIO2_END_OF_STREAM.
			UINT32 AudioBytes;					// Size of the audio data buffer in bytes.
			const BYTE* pAudioData;				// Pointer to the audio data buffer.
			UINT32 PlayBegin;					// First sample in this buffer to be played.
			UINT32 PlayLength;					// Length of the region to be played in samples,
												// or 0 to play the whole buffer.
			UINT32 LoopBegin;					// First sample of the region to be looped.
			UINT32 LoopLength;					// Length of the desired loop region in samples,
												// or 0 to loop the entire buffer.
			UINT32 LoopCount;					// Number of times to repeat the loop region,
												// or XAUDIO2_LOOP_INFINITE to loop forever.
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
															// Each element is the number of bytes accumulated
															// when the corresponding XWMA packet is decoded in
															// order. The array must have PacketCount elements.
			UINT32 PacketCount;								// Number of XWMA packets submitted. Must be >= 1 and
															// divide evenly into XAUDIO2_BUFFER. AudioBytes.
		} XAUDIO2_BUFFER_WMA;

		// Returned by IXAudio2SourceVoice::GetState
		typedef struct XAUDIO2_VOICE_STATE
		{
			void* pCurrentBufferContext;		// The pContext value provided in the XAUDIO2_BUFFER
												// that is currently being processed, or NULL if
												// there are no buffers in the queue.
			UINT32 BuffersQueued;				// Number of buffers currently queued on the voice
												// (including the one that is being processed).
			UINT64 SamplesPlayed;				// Total number of samples produced by the voice since
												// it began processing the current audio stream.
		} XAUDIO2_VOICE_STATE;

		#pragma pack(pop)

		struct IXAudio2EngineCallback
		{
			// Called by XAudio2 just before an audio processing pass begins.
			virtual void OnProcessingPassStart() const noexcept = 0;

			// Called just after an audio processing pass ends.
			virtual void OnProcessingPassEnd() const noexcept = 0;

			// Called in the event of a critical system error which requires XAudio2
			// to be closed down and restarted. The error code is given in Error.
			virtual void OnCriticalError(HRESULT Error) const noexcept = 0;
		};

#if AD_USE_FAUDIO
		class IXAudio2EngineCallbackProxy :
			public FAudioEngineCallback
		{
			IXAudio2EngineCallback* orig{ nullptr };

			IXAudio2EngineCallbackProxy(IXAudio2EngineCallbackProxy&&) = delete;
			IXAudio2EngineCallbackProxy(const IXAudio2EngineCallbackProxy&) = delete;
			IXAudio2EngineCallbackProxy& operator=(IXAudio2EngineCallbackProxy&&) = delete;
			IXAudio2EngineCallbackProxy& operator=(const IXAudio2EngineCallbackProxy&) = delete;
		public:
			IXAudio2EngineCallbackProxy(IXAudio2EngineCallback* a_orig) :
				orig(a_orig)
			{
				OnProcessingPassStart = (OnProcessingPassStartFunc)DoProcessingPassStart;
				OnProcessingPassEnd = (OnProcessingPassEndFunc)DoProcessingPassEnd;
				OnCriticalError = (OnCriticalErrorFunc)DoCriticalError;
			}

			// Called by XAudio2 just before an audio processing pass begins.
			static void DoProcessingPassStart(IXAudio2EngineCallbackProxy* a_this) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnProcessingPassStart();
			}

			// Called just after an audio processing pass ends.
			static void DoProcessingPassEnd(IXAudio2EngineCallbackProxy* a_this) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnProcessingPassEnd();
			}

			// Called in the event of a critical system error which requires XAudio2
			// to be closed down and restarted. The error code is given in Error.
			static void DoCriticalError(IXAudio2EngineCallbackProxy* a_this, uint32_t a_error) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnCriticalError(static_cast<HRESULT>(a_error));
			}
		};
#endif

		struct IXAudio2VoiceCallback
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

#if AD_USE_FAUDIO
		class IXAudio2VoiceCallbackProxy :
			public FAudioVoiceCallback
		{
			IXAudio2VoiceCallback* orig{ nullptr };

			IXAudio2VoiceCallbackProxy(IXAudio2VoiceCallbackProxy&&) = delete;
			IXAudio2VoiceCallbackProxy(const IXAudio2VoiceCallbackProxy&) = delete;
			IXAudio2VoiceCallbackProxy& operator=(IXAudio2VoiceCallbackProxy&&) = delete;
			IXAudio2VoiceCallbackProxy& operator=(const IXAudio2VoiceCallbackProxy&) = delete;
		public:
			IXAudio2VoiceCallbackProxy(IXAudio2VoiceCallback* a_orig) :
				orig(a_orig)
			{
				OnBufferEnd = (OnBufferEndFunc)DoBufferEnd;
				OnBufferStart = (OnBufferStartFunc)DoBufferStart;
				OnLoopEnd = (OnLoopEndFunc)DoLoopEnd;
				OnStreamEnd = (OnStreamEndFunc)DoStreamEnd;
				OnVoiceError = (OnVoiceErrorFunc)DoVoiceError;
				OnVoiceProcessingPassEnd = (OnVoiceProcessingPassEndFunc)DoVoiceProcessingPassEnd;
				OnVoiceProcessingPassStart = (OnVoiceProcessingPassStartFunc)DoVoiceProcessingPassStart;
			}

			// Called just before this voice's processing pass begins.
			static void DoVoiceProcessingPassStart(IXAudio2VoiceCallbackProxy* a_this, uint32_t BytesRequired) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnVoiceProcessingPassStart(BytesRequired);
			}

			// Called just after this voice's processing pass ends.
			static void DoVoiceProcessingPassEnd(IXAudio2VoiceCallbackProxy* a_this) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnVoiceProcessingPassEnd();
			}

			// Called when this voice has just finished playing a buffer stream
			// (as marked with the XAUDIO2_END_OF_STREAM flag on the last buffer).
			static void DoStreamEnd(IXAudio2VoiceCallbackProxy* a_this) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnStreamEnd();
			}

			// Called when this voice is about to start processing a new buffer.
			static void DoBufferStart(IXAudio2VoiceCallbackProxy* a_this, void* a_bufferContext) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnBufferStart(a_bufferContext);
			}

			// Called when this voice has just finished processing a buffer.
			// The buffer can now be reused or destroyed.
			static void DoBufferEnd(IXAudio2VoiceCallbackProxy* a_this, void* a_bufferContext) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnBufferEnd(a_bufferContext);
			}

			// Called when this voice has just reached the end position of a loop.
			static void DoLoopEnd(IXAudio2VoiceCallbackProxy* a_this, void* a_bufferContext) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnLoopEnd(a_bufferContext);
			}

			// Called in the event of a critical error during voice processing,
			// such as a failing xAPO or an error from the hardware XMA decoder.
			// The voice may have to be destroyed and re-created to recover from
			// the error.  The callback arguments report which buffer was being
			// processed when the error occurred, and its HRESULT code.
			static void DoVoiceError(IXAudio2VoiceCallbackProxy* a_this, void* a_bufferContext, uint32_t a_error) noexcept
			{
				if (a_this->orig)
					a_this->orig->OnVoiceError(a_bufferContext, static_cast<HRESULT>(a_error));
			}
		};
#endif

		class IXAudio2SourceVoiceProxy :
			public IXAudio2VoiceProxy
		{
#if AD_USE_FAUDIO
			IXAudio2Proxy* proxy{ nullptr };
			IXAudio2VoiceCallback* callback{ nullptr };
#endif
			IXAudio2SourceVoiceProxy(IXAudio2SourceVoiceProxy&&) = delete;
			IXAudio2SourceVoiceProxy(const IXAudio2SourceVoiceProxy&) = delete;
			IXAudio2SourceVoiceProxy& operator=(IXAudio2SourceVoiceProxy&&) = delete;
			IXAudio2SourceVoiceProxy& operator=(const IXAudio2SourceVoiceProxy&) = delete;
		public:
			friend class IXAudio2Proxy;

			constexpr IXAudio2SourceVoiceProxy() noexcept = default;

#if AD_USE_FAUDIO
			~IXAudio2SourceVoiceProxy() noexcept;
#endif

			// NAME: IXAudio2SourceVoice::Start
			// DESCRIPTION: Makes this voice start consuming and processing audio.
			//
			// ARGUMENTS:
			//  Flags - Flags controlling how the voice should be started.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT Start(UINT32 Flags = 0, UINT32 OperationSet = 0) noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_Start(data, Flags, OperationSet) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Start();
#endif
			}

			// NAME: IXAudio2SourceVoice::Stop
			// DESCRIPTION: Makes this voice stop consuming audio.
			//
			// ARGUMENTS:
			//  Flags - Flags controlling how the voice should be stopped.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT Stop(UINT32 Flags = 0, UINT32 OperationSet = 0) noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_Stop(data, Flags, OperationSet) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Stop();
#endif
			}

			// NAME: IXAudio2SourceVoice::SubmitSourceBuffer
			// DESCRIPTION: Adds a new audio buffer to this voice's input queue.
			//
			// ARGUMENTS:
			//  pBuffer - Pointer to the buffer structure to be queued.
			//  pBufferWMA - Additional structure used only when submitting XWMA data.
			//
			virtual HRESULT SubmitSourceBuffer(XAUDIO2_BUFFER* pBuffer,
				const XAUDIO2_BUFFER_WMA* pBufferWMA = nullptr) noexcept
			{
				if (!data || !pBuffer) return E_FAIL;

#if AD_USE_FAUDIO
				return !FAudioSourceVoice_SubmitSourceBuffer(data,
					reinterpret_cast<const FAudioBuffer*>(pBuffer),
					reinterpret_cast<const FAudioBufferWMA*>(pBufferWMA)) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SubmitSourceBuffer(
					reinterpret_cast<const ::XAUDIO2_BUFFER*>(pBuffer),
					reinterpret_cast<const ::XAUDIO2_BUFFER_WMA*>(pBufferWMA));
#endif
			}

			// NAME: IXAudio2SourceVoice::FlushSourceBuffers
			// DESCRIPTION: Removes all pending audio buffers from this voice's queue.
			//
			virtual HRESULT FlushSourceBuffers() noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_FlushSourceBuffers(data) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->FlushSourceBuffers();
#endif
			}

			// NAME: IXAudio2SourceVoice::Discontinuity
			// DESCRIPTION: Notifies the voice of an intentional break in the stream of
			//              audio buffers (e.g. the end of a sound), to prevent XAudio2
			//              from interpreting an empty buffer queue as a glitch.
			//
			virtual HRESULT Discontinuity() noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_Discontinuity(data) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Discontinuity();
#endif
			}

			// NAME: IXAudio2SourceVoice::ExitLoop
			// DESCRIPTION: Breaks out of the current loop when its end is reached.
			//
			// ARGUMENTS:
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT ExitLoop(UINT32 OperationSet = 0) noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_ExitLoop(data, OperationSet) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->ExitLoop(OperationSet);
#endif
			}

			// NAME: IXAudio2SourceVoice::GetState
			// DESCRIPTION: Returns the number of buffers currently queued on this voice,
			//              the pContext value associated with the currently processing
			//              buffer (if any), and other voice state information.
			//
			// ARGUMENTS:
			//  pVoiceState - Returns the state information.
			//
			virtual void GetState(XAUDIO2_VOICE_STATE* pVoiceState) const noexcept
			{
				if (!data) return;
#if AD_USE_FAUDIO
				FAudioSourceVoice_GetState(data, reinterpret_cast<FAudioVoiceState*>(pVoiceState), 0);
#else
				(reinterpret_cast<IXAudio2SourceVoice*>(data))->GetState(
					reinterpret_cast<::XAUDIO2_VOICE_STATE*>(pVoiceState));
#endif
			}

			// NAME: IXAudio2SourceVoice::SetFrequencyRatio
			// DESCRIPTION: Sets this voice's frequency adjustment, i.e. its pitch.
			//
			// ARGUMENTS:
			//  Ratio - Frequency change, expressed as source frequency / target frequency.
			//  OperationSet - Used to identify this call as part of a deferred batch.
			//
			virtual HRESULT SetFrequencyRatio(float Ratio, UINT32 OperationSet = 0) noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_SetFrequencyRatio(data, Ratio, OperationSet) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SetFrequencyRatio(Ratio, OperationSet);
#endif
			}

			// NAME: IXAudio2SourceVoice::GetFrequencyRatio
			// DESCRIPTION: Returns this voice's current frequency adjustment ratio.
			//
			// ARGUMENTS:
			//  pRatio - Returns the frequency adjustment.
			//
			virtual void GetFrequencyRatio(float* pRatio) const noexcept
			{
				if (!data || !pRatio) return;
#if AD_USE_FAUDIO
				FAudioSourceVoice_GetFrequencyRatio(data, pRatio);
#else
				(reinterpret_cast<IXAudio2SourceVoice*>(data))->GetFrequencyRatio(pRatio);
#endif
			}

			// NAME: IXAudio2SourceVoice::SetSourceSampleRate
			// DESCRIPTION: Reconfigures this voice to treat its source data as being
			//              at a different sample rate than the original one specified
			//              in CreateSourceVoice's pSourceFormat argument.
			//
			// ARGUMENTS:
			//  UINT32 - The intended sample rate of further submitted source data.
			//
			virtual HRESULT SetSourceSampleRate(UINT32 NewSourceSampleRate) noexcept
			{
				if (!data) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudioSourceVoice_SetSourceSampleRate(data, NewSourceSampleRate) ? S_OK : E_FAIL;
#else
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SetSourceSampleRate(NewSourceSampleRate);
#endif
			}
		};

		class IXAudio2SubmixVoiceProxy :
			public IXAudio2VoiceProxy
		{
			IXAudio2SubmixVoiceProxy(IXAudio2SubmixVoiceProxy&&) = delete;
			IXAudio2SubmixVoiceProxy(const IXAudio2SubmixVoiceProxy&) = delete;
			IXAudio2SubmixVoiceProxy& operator=(IXAudio2SubmixVoiceProxy&&) = delete;
			IXAudio2SubmixVoiceProxy& operator=(const IXAudio2SubmixVoiceProxy&) = delete;
		public:
			friend class IXAudio2Proxy;

			constexpr IXAudio2SubmixVoiceProxy() noexcept = default;
		};

		class IXAudio2MasteringVoiceProxy :
			public IXAudio2VoiceProxy
		{
			IXAudio2MasteringVoiceProxy(IXAudio2MasteringVoiceProxy&&) = delete;
			IXAudio2MasteringVoiceProxy(const IXAudio2MasteringVoiceProxy&) = delete;
			IXAudio2MasteringVoiceProxy& operator=(IXAudio2MasteringVoiceProxy&&) = delete;
			IXAudio2MasteringVoiceProxy& operator=(const IXAudio2MasteringVoiceProxy&) = delete;
		public:
			friend class IXAudio2Proxy;

			constexpr IXAudio2MasteringVoiceProxy() noexcept = default;
		};

		// All structures defined in this file use tight field packing
		#pragma pack(push, 1)

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
			UINT32 MemoryUsageInBytes;			// Total heap space currently in use.

												// Audio latency and glitching information
			UINT32 CurrentLatencyInSamples;		// Minimum delay from when a sample is read from a
												//  source buffer to when it reaches the speakers.
			UINT32 GlitchesSinceEngineStarted;  // Audio dropouts since the engine was started.

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
			UINT32 TraceMask;					// Bitmap of enabled debug message types.
			UINT32 BreakMask;					// Message types that will break into the debugger.
			BOOL LogThreadID;					// Whether to log the thread ID with each message.
			BOOL LogFileline;					// Whether to log the source file and line number.
			BOOL LogFunctionName;				// Whether to log the function name.
			BOOL LogTiming;						// Whether to log message timestamps.
		} XAUDIO2_DEBUG_CONFIGURATION;

#if AD_USE_FAUDIO
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
#endif

		#pragma pack(pop)

		class IXAudio2Proxy :
			public IUnknown
		{
			volatile long ref{ 1 };

#if AD_USE_FAUDIO
			FAudio* audio{ nullptr };
			std::unordered_map<IXAudio2EngineCallback*, IXAudio2EngineCallbackProxy*> callbacks{};
			std::unordered_map<IXAudio2VoiceCallback*, IXAudio2VoiceCallbackProxy*> callbacks_audio{};
#else
			IXAudio2* audio{ nullptr };
#endif
			
			IXAudio2Proxy(IXAudio2Proxy&&) = delete;
			IXAudio2Proxy(const IXAudio2Proxy&) = delete;
			IXAudio2Proxy& operator=(IXAudio2Proxy&&) = delete;
			IXAudio2Proxy& operator=(const IXAudio2Proxy&) = delete;
		public:
			friend class IXAudio2SourceVoiceProxy;

			IXAudio2Proxy() noexcept
			{
#if AD_USE_FAUDIO
				FAudioCOMConstructEXT(std::addressof(audio), FAUDIO_TARGET_VERSION);
#else
				auto hr = XAudio2Create(std::addressof(audio), 0, XAUDIO2_ANY_PROCESSOR);
				if (FAILED(hr))
					REX::ERROR("XAudio2Create return failed \"{}\"", _com_error(hr).ErrorMessage());
#endif
			}

#if AD_USE_FAUDIO
			~IXAudio2Proxy() noexcept
			{
				for (auto& call : callbacks)
					if (call.second)
						delete call.second;

				callbacks.clear();

				for (auto& call : callbacks_audio)
					if (call.second)
						delete call.second;

				callbacks_audio.clear();
			}
#endif

			// NAME: IXAudio2::QueryInterface
			// DESCRIPTION: Queries for a given COM interface on the XAudio2 object.
			//              Only IID_IUnknown and IID_IXAudio2 are supported.
			//
			// ARGUMENTS:
			//  riid - IID of the interface to be obtained.
			//  ppvInterface - Returns a pointer to the requested interface.
			//
			HRESULT QueryInterface(REFIID riid, void** ppvObject) noexcept override
			{
				if (riid == IID_IUnknown)
				{
					*ppvObject = static_cast<IUnknown*>(this);
					return S_OK;
				}
				else if (riid == IID_IXAudio2_7)
				{
					*ppvObject = static_cast<IXAudio2Proxy*>(this);
					return S_OK;
				}
				else
				{
					*ppvObject = nullptr;
					return E_FAIL;
				}
			}

			// NAME: IXAudio2::AddRef
			// DESCRIPTION: Adds a reference to the XAudio2 object.
			//
			ULONG AddRef() noexcept override
			{
				return InterlockedIncrement(&ref);
			}

			// NAME: IXAudio2::Release
			// DESCRIPTION: Releases a reference to the XAudio2 object.
			//
			ULONG Release() noexcept override
			{
				ULONG count = InterlockedDecrement(&ref);
				if (!count)
				{
					delete this;
					return 0;
				}
				return count;
			}

			// NAME: IXAudio2::GetDeviceCount
			// DESCRIPTION: Returns the number of audio output devices available.
			//
			// ARGUMENTS:
			//  pCount - Returns the device count.
			//
			virtual HRESULT GetDeviceCount(UINT32* pCount) const noexcept
			{
				if (!audio || !pCount)
					return E_FAIL;

#if AD_USE_FAUDIO
				return !FAudio_GetDeviceCount(audio, pCount) ? S_OK : E_FAIL;
#else
				// Fake
				*pCount = 1;
				return S_OK;
#endif
			}

			// NAME: IXAudio2::GetDeviceDetails
			// DESCRIPTION: Returns information about the device with the given index.
			//
			// ARGUMENTS:
			//  Index - Index of the device to be queried.
			//  pDeviceDetails - Returns the device details.
			//
			virtual HRESULT GetDeviceDetails([[maybe_unused]] UINT32 Index, 
				XAUDIO2_DEVICE_DETAILS* pDeviceDetails) const noexcept
			{
				if (!audio || !pDeviceDetails)
					return E_FAIL;

#if AD_USE_FAUDIO
				return !FAudio_GetDeviceDetails(audio, Index,
					reinterpret_cast<FAudioDeviceDetails*>(pDeviceDetails)) ? S_OK : E_FAIL;
#else
				wcscpy_s(pDeviceDetails->DeviceID, L"{default}");
				wcscpy_s(pDeviceDetails->DisplayName, L"Default");
				
				return S_OK;
#endif
			}

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
			virtual HRESULT Initialize([[maybe_unused]] UINT32 Flags = 0,
				[[maybe_unused]] XAUDIO2_PROCESSOR XAudio2Processor = XAUDIO2_DEFAULT_PROCESSOR) noexcept
			{
				if (!audio)
					return E_FAIL;

#if AD_USE_FAUDIO
				return !FAudio_Initialize(audio, Flags, XAudio2Processor) ? S_OK : E_FAIL;
#else
				return S_OK;
#endif
			}

			// NAME: IXAudio2::RegisterForCallbacks
			// DESCRIPTION: Adds a new client to receive XAudio2's engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Callback interface to be called during each processing pass.
			//
			virtual HRESULT RegisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept
			{
				if (!audio || !pCallback
#if AD_USE_FAUDIO
					|| callbacks.contains(pCallback)
#endif
					)
					return E_FAIL;

#if AD_USE_FAUDIO
				auto call = new IXAudio2EngineCallbackProxy(pCallback);
				if (!call) return E_OUTOFMEMORY;

				if (FAudio_RegisterForCallbacks(audio, call))
				{
					callbacks.try_emplace(pCallback, call);
					return S_OK;
				}

				//delete call;
				return E_FAIL;
#else
				return audio->RegisterForCallbacks(reinterpret_cast<::IXAudio2EngineCallback*>(pCallback));
#endif
			}

			// NAME: IXAudio2::UnregisterForCallbacks
			// DESCRIPTION: Removes an existing receiver of XAudio2 engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Previously registered callback interface to be removed.
			//
			virtual void UnregisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept
			{
				if (!audio || !pCallback)
					return;

#if AD_USE_FAUDIO
				auto it_call = callbacks.find(pCallback);
				if (it_call == callbacks.end())
					return;

				if (it_call->second)
				{
					FAudio_UnregisterForCallbacks(audio, it_call->second);
					//delete it_call->second;
				}

				callbacks.erase(it_call);
#else
				audio->UnregisterForCallbacks(reinterpret_cast<::IXAudio2EngineCallback*>(pCallback));
#endif
			}

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
			virtual HRESULT CreateSourceVoice(IXAudio2SourceVoiceProxy** ppSourceVoice,
				const WAVEFORMATEX* pSourceFormat, UINT32 Flags = 0,
				float MaxFrequencyRatio = 2.0f,
				IXAudio2VoiceCallback* pCallback = nullptr,
				const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept
			{
				if (!audio || !ppSourceVoice)
					return E_FAIL;

#if AD_USE_FAUDIO
				uint32_t result{ 1 };
				IXAudio2VoiceCallbackProxy* call{ nullptr };

				if (pCallback)
				{
					call = new IXAudio2VoiceCallbackProxy(pCallback);
					if (!call) return E_OUTOFMEMORY;

					callbacks_audio.try_emplace(pCallback, call);
				}
#endif

				*ppSourceVoice = new IXAudio2SourceVoiceProxy();
				if (!(*ppSourceVoice)) return E_OUTOFMEMORY;

#if AD_USE_FAUDIO
				(*ppSourceVoice)->callback = pCallback;

				if (pSendList && pSendList->SendCount)
				{
					FAudioVoiceSends sends{};

					sends.SendCount = pSendList->SendCount;
					sends.pSends = new FAudioSendDescriptor[pSendList->SendCount];
					if (!sends.pSends)
					{
						delete (*ppSourceVoice);
						return E_OUTOFMEMORY;
					}

					for (UINT32 i = 0; i < pSendList->SendCount; i++)
					{
						sends.pSends[i].Flags = pSendList->pSends[i].Flags;
						sends.pSends[i].pOutputVoice =
							pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
					}

					result = FAudio_CreateSourceVoice(audio, std::addressof((*ppSourceVoice)->data),
						reinterpret_cast<const FAudioWaveFormatEx*>(pSourceFormat), Flags, MaxFrequencyRatio, nullptr,
						std::addressof(sends), nullptr);
					delete[] sends.pSends;
				}
				else
					result = FAudio_CreateSourceVoice(audio, std::addressof((*ppSourceVoice)->data),
						reinterpret_cast<const FAudioWaveFormatEx*>(pSourceFormat), Flags, MaxFrequencyRatio, nullptr,
						nullptr, nullptr);

				return !result ? S_OK : E_FAIL;
#else
				if (pSendList && pSendList->SendCount)
				{
					::XAUDIO2_VOICE_SENDS sends{};

					sends.SendCount = pSendList->SendCount;
					sends.pSends = new ::XAUDIO2_SEND_DESCRIPTOR[pSendList->SendCount];
					if (!sends.pSends)
					{
						delete (*ppSourceVoice);
						return E_OUTOFMEMORY;
					}

					for (UINT32 i = 0; i < pSendList->SendCount; i++)
					{
						sends.pSends[i].Flags = pSendList->pSends[i].Flags;
						sends.pSends[i].pOutputVoice =
							pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
					}

					auto hr = audio->CreateSourceVoice(reinterpret_cast<::IXAudio2SourceVoice**>(std::addressof((*ppSourceVoice)->data)),
						pSourceFormat, Flags, MaxFrequencyRatio, reinterpret_cast<::IXAudio2VoiceCallback*>(pCallback), 
						std::addressof(sends), reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
					delete[] sends.pSends;
					return hr;
				}
				else
					return audio->CreateSourceVoice(reinterpret_cast<::IXAudio2SourceVoice**>(std::addressof((*ppSourceVoice)->data)),
						pSourceFormat, Flags, MaxFrequencyRatio, reinterpret_cast<::IXAudio2VoiceCallback*>(pCallback),
						nullptr, reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
#endif
			}

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
			virtual HRESULT CreateSubmixVoice(IXAudio2SubmixVoiceProxy** ppSubmixVoice,
				UINT32 InputChannels, UINT32 InputSampleRate,
				UINT32 Flags = 0, UINT32 ProcessingStage = 0,
				const XAUDIO2_VOICE_SENDS* pSendList = nullptr,
				[[maybe_unused]] const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept
			{
				if (!audio || !ppSubmixVoice)
					return E_FAIL;

#if AD_USE_FAUDIO
				uint32_t result{ 1 };

				*ppSubmixVoice = new IXAudio2SubmixVoiceProxy();
				if (!(*ppSubmixVoice)) return E_OUTOFMEMORY;

				if (pSendList && pSendList->SendCount)
				{
					FAudioVoiceSends sends{};

					sends.SendCount = pSendList->SendCount;
					sends.pSends = new FAudioSendDescriptor[pSendList->SendCount];
					if (!sends.pSends)
					{
						delete (*ppSubmixVoice);
						return E_OUTOFMEMORY;
					}

					for (UINT32 i = 0; i < pSendList->SendCount; i++)
					{
						sends.pSends[i].Flags = pSendList->pSends[i].Flags;
						sends.pSends[i].pOutputVoice =
							pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
					}

					result = FAudio_CreateSubmixVoice(audio, std::addressof((*ppSubmixVoice)->data),
						InputChannels, InputSampleRate, Flags, ProcessingStage, std::addressof(sends), nullptr);
					delete[] sends.pSends;
				}
				else
					result = FAudio_CreateSubmixVoice(audio, std::addressof((*ppSubmixVoice)->data),
						InputChannels, InputSampleRate, Flags, ProcessingStage, nullptr, nullptr);

				return !result ? S_OK : E_FAIL;
#else
				* ppSubmixVoice = new IXAudio2SubmixVoiceProxy();
				if (!(*ppSubmixVoice)) return E_OUTOFMEMORY;

				if (pSendList && pSendList->SendCount)
				{
					::XAUDIO2_VOICE_SENDS sends{};

					sends.SendCount = pSendList->SendCount;
					sends.pSends = new ::XAUDIO2_SEND_DESCRIPTOR[pSendList->SendCount];
					if (!sends.pSends)
					{
						delete (*ppSubmixVoice);
						return E_OUTOFMEMORY;
					}

					for (UINT32 i = 0; i < pSendList->SendCount; i++)
					{
						sends.pSends[i].Flags = pSendList->pSends[i].Flags;
						sends.pSends[i].pOutputVoice =
							pSendList->pSends[i].pOutputVoice ? pSendList->pSends[i].pOutputVoice->data : nullptr;
					}

					auto hr = audio->CreateSubmixVoice(reinterpret_cast<::IXAudio2SubmixVoice**>(std::addressof((*ppSubmixVoice)->data)),
						InputChannels, InputSampleRate, Flags, ProcessingStage, std::addressof(sends),
						reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
					delete[] sends.pSends;
					return hr;
				}
				else
					return audio->CreateSubmixVoice(reinterpret_cast<::IXAudio2SubmixVoice**>(std::addressof((*ppSubmixVoice)->data)),
						InputChannels, InputSampleRate, Flags, ProcessingStage, nullptr,
						reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
#endif				
			}

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
			virtual HRESULT CreateMasteringVoice(IXAudio2MasteringVoiceProxy** ppMasteringVoice,
#if AD_USE_FAUDIO
				UINT32 InputChannels = FAUDIO_DEFAULT_CHANNELS,
				UINT32 InputSampleRate = FAUDIO_DEFAULT_SAMPLERATE,
#else
				UINT32 InputChannels = XAUDIO2_DEFAULT_CHANNELS,
				UINT32 InputSampleRate = XAUDIO2_DEFAULT_SAMPLERATE,
#endif
				UINT32 Flags = 0, UINT32 DeviceIndex = 0,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept
			{
				if (!audio || !ppMasteringVoice)
					return E_FAIL;

				*ppMasteringVoice = new IXAudio2MasteringVoiceProxy();
				if (!(*ppMasteringVoice)) return E_OUTOFMEMORY;

#if AD_USE_FAUDIO
				return !FAudio_CreateMasteringVoice(audio, std::addressof((*ppMasteringVoice)->data),
					InputChannels, InputSampleRate, Flags, DeviceIndex, nullptr) ? S_OK : E_FAIL;
#else
				return audio->CreateMasteringVoice(reinterpret_cast<::IXAudio2MasteringVoice**>(std::addressof((*ppMasteringVoice)->data)),
					InputChannels, InputSampleRate, Flags, nullptr, 
					reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
#endif
			}

			// NAME: IXAudio2::StartEngine
			// DESCRIPTION: Creates and starts the audio processing thread.
			//
			virtual HRESULT StartEngine() noexcept
			{
				if (!audio) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudio_StartEngine(audio) ? S_OK : E_FAIL;
#else
				return audio->StartEngine();
#endif
			}

			// NAME: IXAudio2::StopEngine
			// DESCRIPTION: Stops and destroys the audio processing thread.
			//
			virtual void StopEngine() noexcept
			{
				if (!audio) return;
#if AD_USE_FAUDIO
				FAudio_StopEngine(audio);
#else
				return audio->StopEngine();
#endif
			}

			// NAME: IXAudio2::CommitChanges
			// DESCRIPTION: Atomically applies a set of operations previously tagged
			//              with a given identifier.
			//
			// ARGUMENTS:
			//  OperationSet - Identifier of the set of operations to be applied.
			//
			virtual HRESULT CommitChanges(UINT32 OperationSet) noexcept
			{
				if (!audio) return E_FAIL;
#if AD_USE_FAUDIO
				return !FAudio_CommitOperationSet(audio, OperationSet) ? S_OK : E_FAIL;
#else
				return audio->CommitChanges(OperationSet);
#endif
			}

			// NAME: IXAudio2::GetPerformanceData
			// DESCRIPTION: Returns current resource usage details: memory, CPU, etc.
			//
			// ARGUMENTS:
			//  pPerfData - Returns the performance data structure.
			//
			virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* pPerfData) const noexcept
			{
				if (!audio || !pPerfData)
					return;

#if AD_USE_FAUDIO
				FAudio_GetPerformanceData(audio, reinterpret_cast<FAudioPerformanceData*>(pPerfData));
#else
				audio->GetPerformanceData(reinterpret_cast<::XAUDIO2_PERFORMANCE_DATA*>(pPerfData));
#endif
			}

			// NAME: IXAudio2::SetDebugConfiguration
			// DESCRIPTION: Configures XAudio2's debug output (in debug builds only).
			//
			// ARGUMENTS:
			//  pDebugConfiguration - Structure describing the debug output behavior.
			//  pReserved - Optional parameter; must be NULL.
			//
			virtual void SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION* pDebugConfiguration,
				[[maybe_unused]] void* pReserved = nullptr) noexcept
			{
				if (!audio || !pDebugConfiguration)
					return;

#if AD_USE_FAUDIO
				FAudio_SetDebugConfiguration(audio,
					// ??? bruh
					const_cast<FAudioDebugConfiguration*>(reinterpret_cast<const FAudioDebugConfiguration*>(pDebugConfiguration)),
					nullptr);
#else
				audio->SetDebugConfiguration(reinterpret_cast<const ::XAUDIO2_DEBUG_CONFIGURATION*>(pDebugConfiguration), nullptr);
#endif
			}
		};

#if !AD_USE_FAUDIO
		static void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* pI3DL2, 
			XAUDIO2FX_REVERB_PARAMETERS* pNative)
		{
			float reflectionsDelay;
			float reverbDelay;

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
			pNative->RoomFilterMain = (float)pI3DL2->Room / 100.0f;
			pNative->RoomFilterHF = (float)pI3DL2->RoomHF / 100.0f;

			if (pI3DL2->DecayHFRatio >= 1.0f)
			{
				INT32 index = (INT32)(-4.0 * log10(pI3DL2->DecayHFRatio));
				if (index < -8) index = -8;
				pNative->LowEQGain = (BYTE)((index < 0) ? index + 8 : 8);
				pNative->HighEQGain = 8;
				pNative->DecayTime = pI3DL2->DecayTime * pI3DL2->DecayHFRatio;
			}
			else
			{
				INT32 index = (INT32)(4.0 * log10(pI3DL2->DecayHFRatio));
				if (index < -8) index = -8;
				pNative->LowEQGain = 8;
				pNative->HighEQGain = (BYTE)((index < 0) ? index + 8 : 8);
				pNative->DecayTime = pI3DL2->DecayTime;
			}

			reflectionsDelay = pI3DL2->ReflectionsDelay * 1000.0f;
			if (reflectionsDelay >= XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY) // 300
			{
				reflectionsDelay = (float)(XAUDIO2FX_REVERB_MAX_REFLECTIONS_DELAY - 1);
			}
			else if (reflectionsDelay <= 1)
			{
				reflectionsDelay = 1;
			}
			pNative->ReflectionsDelay = (UINT32)reflectionsDelay;

			reverbDelay = pI3DL2->ReverbDelay * 1000.0f;
			if (reverbDelay >= XAUDIO2FX_REVERB_MAX_REVERB_DELAY) // 85
			{
				reverbDelay = (float)(XAUDIO2FX_REVERB_MAX_REVERB_DELAY - 1);
			}
			pNative->ReverbDelay = (BYTE)reverbDelay;

			pNative->ReflectionsGain = pI3DL2->Reflections / 100.0f;
			pNative->ReverbGain = pI3DL2->Reverb / 100.0f;
			pNative->EarlyDiffusion = (BYTE)(15.0f * pI3DL2->Diffusion / 100.0f);
			pNative->LateDiffusion = pNative->EarlyDiffusion;
			pNative->Density = pI3DL2->Density;
			pNative->RoomFilterFreq = pI3DL2->HFReference;

			pNative->WetDryMix = pI3DL2->WetDryMix;
		}
#endif

#if AD_USE_FAUDIO
		IXAudio2SourceVoiceProxy::~IXAudio2SourceVoiceProxy() noexcept
		{
			if (callback && proxy)
			{
				auto it_call = proxy->callbacks_audio.find(callback);
				if (it_call != proxy->callbacks_audio.end())
					proxy->callbacks_audio.erase(it_call);
			}
		}
#endif
	}

#if 0
	struct BSXAudio2Audio
	{
		using NotifyFunc = void();

		inline static std::function<NotifyFunc> InitSDM{};
		inline static std::function<NotifyFunc> ShutdownSDM{};
	};
#endif

#if AD_USE_CHECKUPDATE_AUDIODEVICE
	class IAudioNotificationClient : public IMMNotificationClient
	{
		long m_cRef{ 1 };
	public:
		// IUnknown methods

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept
		{
			if ((riid == IID_IUnknown) || (riid == __uuidof(IMMNotificationClient)))
			{
				*ppvObject = this;
				AddRef();
				return S_OK;
			}

			*ppvObject = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() noexcept { return InterlockedIncrement(&m_cRef); }
		ULONG STDMETHODCALLTYPE Release() noexcept
		{
			ULONG ulRef = InterlockedDecrement(&m_cRef);
			if (ulRef == 0)
				delete this;
			return ulRef;
		}

		// IMMNotificationClient methods
		HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) noexcept {
			
			//BSXAudio2Audio::ShutdownSDM();
			//BSXAudio2Audio::InitSDM();

			//REX::INFO(L"sound {} {}", std::to_underlying(role), pwstrDeviceId);

			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE OnDeviceAdded([[maybe_unused]] LPCWSTR pwstrDeviceId) noexcept
		{ return S_OK; }
		HRESULT STDMETHODCALLTYPE OnDeviceRemoved([[maybe_unused]] LPCWSTR pwstrDeviceId) noexcept
		{ return S_OK; }
		HRESULT STDMETHODCALLTYPE OnDeviceStateChanged([[maybe_unused]] LPCWSTR pwstrDeviceId, 
			[[maybe_unused]] DWORD dwNewState) noexcept
		{ return S_OK; }
		HRESULT STDMETHODCALLTYPE OnPropertyValueChanged([[maybe_unused]] LPCWSTR pwstrDeviceId,
			[[maybe_unused]] const PROPERTYKEY key) noexcept
		{ return S_OK; }

		IAudioNotificationClient() noexcept = default;
		~IAudioNotificationClient() noexcept = default;

		IAudioNotificationClient(IAudioNotificationClient&&) = delete;
		IAudioNotificationClient(const IAudioNotificationClient&) = delete;
		IAudioNotificationClient& operator=(IAudioNotificationClient&&) = delete;
		IAudioNotificationClient& operator=(const IAudioNotificationClient&) = delete;
	};

	class AudioNotificationListener
	{
		bool initCOM{ false };
		bool initListener{ false };
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
		Microsoft::WRL::ComPtr<IAudioNotificationClient> client{};
	public:
		AudioNotificationListener() noexcept
		{
			auto hr = CoInitialize(nullptr);
			initCOM = SUCCEEDED(hr);
			if (initCOM)
			{
				client = new IAudioNotificationClient();
				hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
					__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
				if (FAILED(hr))
					REX::ERROR("AudioNotificationListener::CoCreateInstance return failed \"{}\"",
						_com_error(hr).ErrorMessage());
				else
				{
					if (enumerator)
					{
						hr = enumerator->RegisterEndpointNotificationCallback(client.Get());
						initListener = SUCCEEDED(hr);
						if(!initListener)
							REX::ERROR("AudioNotificationListener::RegisterEndpointNotificationCallback return failed \"{}\"",
								_com_error(hr).ErrorMessage());
					}
					else
						// ??? paranoic
						REX::ERROR("MMDeviceEnumerator pointer is nullptr");
				}
			}
			else
				REX::ERROR("AudioNotificationListener::CoInitialize return failed \"{}\"", _com_error(hr).ErrorMessage());
		}

		~AudioNotificationListener() noexcept
		{
			if (initCOM)
			{
				if (initListener)
					enumerator->UnregisterEndpointNotificationCallback(client.Get());

				CoUninitialize();
			}
		}

		AudioNotificationListener(AudioNotificationListener&&) = delete;
		AudioNotificationListener(const AudioNotificationListener&) = delete;
		AudioNotificationListener& operator=(AudioNotificationListener&&) = delete;
		AudioNotificationListener& operator=(const AudioNotificationListener&) = delete;
	};

	static AudioNotificationListener audioNotificationListener{};
#endif 

	static HRESULT XAudio2_CoCreateInstance([[maybe_unused]] REFCLSID rclsid, [[maybe_unused]] LPUNKNOWN pUnkOuter,
		[[maybe_unused]] DWORD dwClsContext, [[maybe_unused]] REFIID riid, LPVOID* ppv) noexcept
	{
		auto proxy = (detail::IXAudio2Proxy**)ppv;
		*proxy = new detail::IXAudio2Proxy();
		if (!(*proxy)) return E_OUTOFMEMORY;
		return S_OK;
	}

	//static HRESULT XAudio2Reverb_CoCreateInstance([[maybe_unused]] REFCLSID rclsid, [[maybe_unused]] LPUNKNOWN pUnkOuter,
	//	[[maybe_unused]] DWORD dwClsContext, [[maybe_unused]] REFIID riid, LPVOID* ppv) noexcept
	//{
	//	return XAudio2CreateReverb(reinterpret_cast<IUnknown**>(ppv));
	//}

	ModuleAudioProxy::ModuleAudioProxy() :
		Module("Audio Proxy", &bPatchesAudioProxy)
	{}

	bool ModuleAudioProxy::DoQuery() const noexcept
	{
		// disable patch
		return false; //RELEX::IsRuntimeAE();
	}

	bool ModuleAudioProxy::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
#if 0
		BSXAudio2Audio::InitSDM = reinterpret_cast<BSXAudio2Audio::NotifyFunc*>(REL::ID{ 2267536 }.address());
		BSXAudio2Audio::ShutdownSDM = reinterpret_cast<BSXAudio2Audio::NotifyFunc*>(REL::ID{ 2267537 }.address());
		
		MessageBoxA(0, "tet", "", 0);
#endif

		RELEX::DetourCall(REL::Relocation(REL::ID{ 2267547 }, REL::Offset{ 0x108 }).address(),
			(uintptr_t)&XAudio2_CoCreateInstance);
		//RELEX::DetourCall(REL::Relocation(REL::ID{ 2267579 }, REL::Offset{ 0xFD }).address(),
		//	(uintptr_t)&XAudio2Reverb_CoCreateInstance);
		//RELEX::DetourJump(REL::ID{ 2267576 }.address(), (uintptr_t)&detail::ReverbConvertI3DL2ToNative);

		return true;
	}

	bool ModuleAudioProxy::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAudioProxy::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
