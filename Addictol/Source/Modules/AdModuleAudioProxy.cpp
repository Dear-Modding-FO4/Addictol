#include <Modules/AdModuleAudioProxy.h>
#include <AdUtils.h>

#define NO_USE_PORTAUDIO 1

#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <comdef.h>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <VersionHelpers.h>

#define AD_USE_CHECKUPDATE_AUDIODEVICE 0

#include <xaudio2.h>
#include <xaudio2fx.h>
#include <x3daudio.h>
#include <audioclient.h>
#pragma comment(lib, "xaudio2.lib")

#undef ERROR
#undef MAX_PATH
#undef MEM_RELESE

#include <RE/B/BSFixedString.h>
#include <RE/B/BSResource_ID.h>
#include <RE/N/NiPoint3.h>

namespace RE
{
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
		inline static constexpr auto RTTI = RTTI::IXAPO;

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
	#pragma pack(pop)
	// All structures defined in this file use tight field packing
	#pragma pack(push, 8)
	class __declspec(novtable) CXAPOBase : public IXAPO
	{
	public:
		inline static constexpr auto RTTI = RTTI::CXAPOBase;

		// override (IXAPO)
		HRESULT QueryInterface(REFIID a_riid, void** a_interface) override;
		ULONG AddRef() override;
		ULONG Release() override;
		int32_t GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** a_registrationProperties) override;
		int32_t IsInputFormatSupported(const WAVEFORMATEX* a_outputFormat, const WAVEFORMATEX* a_requestedInputFormat,
			WAVEFORMATEX** a_supportedInputFormat) override;
		int32_t IsOutputFormatSupported(const WAVEFORMATEX* a_inputFormat, const WAVEFORMATEX* a_requestedOutputFormat,
			WAVEFORMATEX** a_supportedOutputFormat) override;
		int32_t Initialize(const void* a_data, uint32_t a_dataByteSize) override;
		void Reset() override;
		int32_t LockForProcess(uint32_t a_inputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_inputLockedParameters,
			uint32_t a_outputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_outputLockedParameters) override;
		void UnlockForProcess() override;
		void Process(uint32_t a_inputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS* a_InputProcessParameters,
			uint32_t a_outputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS* a_outputProcessParameters, BOOL a_isEnabled) override;
		uint32_t CalcInputFrames(uint32_t a_outputFrameCount) override;
		uint32_t CalcOutputFrames(uint32_t a_inputFrameCount) override;

		// add
		virtual int32_t ValidateFormatDefault(WAVEFORMATEX* a_format, BOOL a_overwrite);
		virtual ~CXAPOBase();

		[[nodiscard]] const XAPO_REGISTRATION_PROPERTIES* GetRegistrationPropertiesInternal() const noexcept { return registrationProperties; }
		[[nodiscard]] BOOL IsLocked() const noexcept { return isLocked; }

		// members
		const XAPO_REGISTRATION_PROPERTIES* registrationProperties;
		void* fnMatrixMixFunction;
		float* matrixCoefficients;
		uint32_t srcFormatType;
		BOOL isScalarMatrix;
		BOOL isLocked;
		int32_t referenceCount;
	};
	static_assert(sizeof(CXAPOBase) == 0x30);
	#pragma pack(pop)

	class MonitorAPO : public CXAPOBase
	{
	public:
		inline static constexpr auto RTTI = RTTI::__MonitorAPO;

		~MonitorAPO() override;

		// override (CXAPOBase)
		int32_t LockForProcess(uint32_t a_inputLockedParameterCount,
			const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_inputLockedParameters, uint32_t a_outputLockedParameterCount,
			const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_outputLockedParameters) override;
		void Process(uint32_t a_inputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS* a_InputProcessParameters,
			uint32_t a_outputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS* a_outputProcessParameters,
			BOOL a_isEnabled) override;

		// members
		uint32_t numChannels;
		float amplitude;
	};
	static_assert(sizeof(MonitorAPO) == 0x38);

	struct XAudio2Monitor
	{
		// members
		MonitorAPO* monitorAPO{ nullptr };
		IXAudio2SubmixVoice* submixVoice{ nullptr };
	};
	static_assert(sizeof(XAudio2Monitor) == 0x10);

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

	class BSAudioListener
	{
	public:
		inline static constexpr auto RTTI = RE::RTTI::BSAudioListener;

		virtual ~BSAudioListener();

		// add
		virtual void SetPosition(const RE::NiPoint3& a_pos) = 0;
		virtual void Unk_10() = 0;

		// members
		RE::NiPoint3 listenerPosition;
		uint8_t unk[0x68];
	};
	static_assert(sizeof(BSAudioListener) == 0x80);

	class BSIReverbType
	{
	public:
		inline static constexpr auto RTTI = RE::RTTI::BSIReverbType;

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
	class BSISoundOutputModel;

	class BSGameSound;

	class BSAudio
	{
	public:
		inline static constexpr auto RTTI = RE::RTTI::BSAudio;

		virtual ~BSAudio();

		// add
		virtual bool Init(REX::W32::HWND* a_wnd);
		virtual void Shutdown();
		virtual BSGameSound* GetGameSound(const ::RE::BSResource::ID& a_resourceID) = 0;
		virtual void ReleaseGameSound(BSGameSound* a_gameSound) = 0;
		virtual const RE::BSFixedString& GetSystemName() = 0;
		virtual void ApplyReverbType(const BSIReverbType* a_reverbType, uint32_t a_tickLength);
		virtual void Unk38();
		virtual void Unk40();
		virtual uint32_t CreateMonitor(float a_amplitude);
		virtual void ReleaseMonitor(uint32_t a_monitor);
		virtual BSAudioMonitor::Receiver GetReceiver(uint32_t a_monitor);
		virtual void Unk60();

		// members
		BSAudioListener* audioListener;
	};
	static_assert(sizeof(BSAudio) == 0x10);

	class BSXAudio2Graph : public IXAudio2EngineCallback
	{
	public:
		inline static constexpr auto RTTI = RE::RTTI::__BSXAudio2Graph;

		struct ReverbEffect
		{
			// idk.. Bethesda uses 2 presets like as DEFAULT
			// but first use for init via IXAudio2Voice::SetEffectParameters
			std::array<XAUDIO2FX_REVERB_I3DL2_PARAMETERS, 2> presets
			{ {
				XAUDIO2FX_I3DL2_PRESET_DEFAULT,
				XAUDIO2FX_I3DL2_PRESET_DEFAULT
			} };

			XAudio2Monitor monitor{};
			// idk.. no init area
			std::array<void*, 2> unk{};
		};

		[[nodiscard]] static BSXAudio2Graph* GetSingleton()
		{
			static REL::Relocation<BSXAudio2Graph**> singleton{ REL::ID{ 1219921, 2703127 } };
			return *singleton;
		}

		void OnProcessingPassStart() noexcept override { return; }
		void OnProcessingPassEnd() noexcept override
		{
			using func_t = decltype(&BSXAudio2Graph::OnProcessingPassEnd);
			static REL::Relocation<func_t> func{ REL::ID{ 351273, 2267567 } };
			func(this);
		}
		void OnCriticalError(HRESULT Error) noexcept override { return; }

		IXAudio2* xaudio;
		IXAudio2MasteringVoice* masteringVoice;
		std::array<ReverbEffect, 2> effects;
		uint32_t totalDevice;
		uint32_t currentDevice;
		bool registerCallbacks;
		bool initEffects;
		bool initEngine;
	};
	static_assert(sizeof(BSXAudio2Graph::ReverbEffect) == 0x88);
	static_assert(sizeof(BSXAudio2Graph) == 0x138);

	class BSXAudio2Audio : public BSAudio
	{
	public:
		inline static constexpr auto RTTI = RE::RTTI::BSXAudio2Audio;

		~BSXAudio2Audio() override;  // 00

		[[nodiscard]] static BSXAudio2Audio* GetSingleton()
		{
			static REL::Relocation<BSXAudio2Audio**> singleton{ REL::ID{ 1565436, 2703127 } };
			return *singleton;
		}

		// override (BSAudio)
		bool Init(REX::W32::HWND* a_wnd) override;
		void Shutdown() override;
		BSGameSound* GetGameSound(const BSResource::ID& a_resourceID) override;
		void ReleaseGameSound(BSGameSound* a_gameSound) override;
		const BSFixedString& GetSystemName() override;
		void ApplyReverbType(const BSIReverbType* a_reverbType, std::uint32_t a_tickLength) override;
		std::uint32_t CreateMonitor(float a_amplitude) override;
		void ReleaseMonitor(std::uint32_t a_monitor) override;
		BSAudioMonitor::Receiver GetReceiver(std::uint32_t a_monitor) override;
	};
	//static_assert(sizeof(BSXAudio2Audio) == 0x78);
}

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

		// --- 2.7 XAPO -> 2.9 QI compatibility layer ---
		// The game's built-in APO classes (BSStateVariableFilter, BSOverdrive,
		// BSDelayEffect, BSCXAPOWrapper, MonitorAPO) are compiled against the
		// DirectX SDK 2.7 headers and only recognise the 2.7 IID_IXAPO. The 2.9
		// engine queries for IID_IXAPO_29 and rejects them with E_NOINTERFACE,
		// which strips the radio EQ and power-armor effects (dry voice).
		// This layer patches each APO vtable's QueryInterface slot on first use
		// so it also accepts the 2.9 IID.
		// (contribution: tryname @ Nexus — AudioDeviceFollowFix)

		inline constexpr GUID IID_IXAPO_29{ 0xA410B984, 0x9839, 0x4819,
			{ 0xA0, 0xBE, 0x28, 0x56, 0xAE, 0x6B, 0x3A, 0xDB } };

		using IXAPO_QI_Fn = HRESULT(__stdcall*)(void*, REFIID, void**);

		static std::unordered_map<void*, IXAPO_QI_Fn> g_qiShimMap{};
		static std::mutex g_qiShimMutex{};

		HRESULT __stdcall GenericXAPOQIShim(void* self, REFIID riid, void** out) noexcept
		{
			if (riid == IID_IXAPO_29)
			{
				if (out) *out = self;
				reinterpret_cast<IUnknown*>(self)->AddRef();
				return S_OK;
			}
			void** vtbl = *reinterpret_cast<void***>(self);
			IXAPO_QI_Fn origQI{};
			{
				std::lock_guard lock(g_qiShimMutex);
				auto it = g_qiShimMap.find(vtbl);
				if (it != g_qiShimMap.end())
					origQI = it->second;
			}
			if (origQI)
				return origQI(self, riid, out);
			if (out) *out = nullptr;
			return E_NOINTERFACE;
		}

		static void EnsureEffectChainCompat(const XAUDIO2_EFFECT_CHAIN* a_chain) noexcept
		{
			if (!a_chain || !a_chain->EffectCount) return;
			for (UINT32 i = 0; i < a_chain->EffectCount; ++i)
			{
				auto* effect = a_chain->pEffectDescriptors[i].pEffect;
				if (!effect) continue;

				// Already 2.9-compatible?
				void* dummy{};
				if (SUCCEEDED(effect->QueryInterface(IID_IXAPO_29, &dummy)))
				{
					reinterpret_cast<IUnknown*>(dummy)->Release();
					continue;
				}

				auto** vtbl = *reinterpret_cast<void***>(effect);

				// Serialize the whole check-and-patch: a concurrent patch of the same
				// vtable must not race its protection restore against our write.
				std::lock_guard lock(g_qiShimMutex);
				auto currentQI = reinterpret_cast<IXAPO_QI_Fn>(vtbl[0]);
				if (currentQI == &GenericXAPOQIShim) continue;

				g_qiShimMap[vtbl] = currentQI;
				DWORD old{};
				if (VirtualProtect(&vtbl[0], sizeof(void*), PAGE_READWRITE, &old))
				{
					vtbl[0] = reinterpret_cast<void*>(&GenericXAPOQIShim);
					FlushInstructionCache(GetCurrentProcess(), &vtbl[0], sizeof(void*));
					VirtualProtect(&vtbl[0], sizeof(void*), old, &old);
				}
			}
		}

		// These methods are declared in a macro so that the same declarations
		// can be used in the derived voice types (IXAudio2SourceVoice, etc).
		class IXAudio2VoiceProxy
		{
		protected:
			::IXAudio2Voice* data{ nullptr };
		public:
			friend class IXAudio2Proxy;

			static HRESULT CopyVoiceSends(::XAUDIO2_VOICE_SENDS* dest, const XAUDIO2_VOICE_SENDS* src) noexcept
			{
				dest->SendCount = src->SendCount;
				dest->pSends = new ::XAUDIO2_SEND_DESCRIPTOR[src->SendCount];
				if (!dest->pSends) return E_OUTOFMEMORY;
				for (UINT32 i = 0; i < src->SendCount; i++)
				{
					auto& apoSrcSend = src->pSends[i];
					auto& apoDstSend = dest->pSends[i];
#if 0
					auto proxy = dynamic_cast<IXAudio2VoiceProxy*>(apoSrcSend.pOutputVoice);

					// FIXME: need to figure out why XAPO needs to be disabled here. The sound is getting too quiet, something is wrong.
					apoDstSend.Flags = XAUDIO2_SEND_USEFILTER;
					apoDstSend.pOutputVoice = proxy ? proxy->data : reinterpret_cast<IXAudio2Voice*>(apoSrcSend.pOutputVoice);
#else
					apoDstSend.Flags = XAUDIO2_SEND_USEFILTER;
					apoDstSend.pOutputVoice = apoSrcSend.pOutputVoice->data;
#endif
				}
				return S_OK;
			}

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
					::XAUDIO2_VOICE_DETAILS details{};
					data->GetVoiceDetails(std::addressof(details));

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
					if (!pSendList || !pSendList->SendCount)
						return data->SetOutputVoices(nullptr);
					else
					{
						::XAUDIO2_VOICE_SENDS sends{};
						if (SUCCEEDED(CopyVoiceSends(&sends, pSendList)))
						{
							auto hr = data->SetOutputVoices(std::addressof(sends));
							delete[] sends.pSends;
							return hr;
						}
					}
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

				EnsureEffectChainCompat(pEffectChain);
				return data->SetEffectChain(reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
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
				return data->EnableEffect(EffectIndex, OperationSet);
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
				return data->DisableEffect(EffectIndex, OperationSet);
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
				data->GetEffectState(EffectIndex, pEnabled);
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

				return data->SetFilterParameters(reinterpret_cast<const ::XAUDIO2_FILTER_PARAMETERS*>(pParameters),
					OperationSet);
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

				data->GetFilterParameters(reinterpret_cast<::XAUDIO2_FILTER_PARAMETERS*>(pParameters));
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

				return data->SetOutputFilterParameters(pDestinationVoice->data,
					reinterpret_cast<const ::XAUDIO2_FILTER_PARAMETERS*>(pParameters), OperationSet);
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

				data->GetOutputFilterParameters(pDestinationVoice->data,
					reinterpret_cast<::XAUDIO2_FILTER_PARAMETERS*>(pParameters));
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

				return data->SetVolume(Volume, OperationSet);
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

				data->GetVolume(pVolume);
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

				return data->SetChannelVolumes(Channels, pVolumes, OperationSet);
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

				data->GetChannelVolumes(Channels, pVolumes);
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
				auto hr = data->SetOutputMatrix(pDestinationVoice->data, SourceChannels,
					DestinationChannels, pLevelMatrix, OperationSet);				
				return hr;
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
				data->GetOutputMatrix(pDestinationVoice->data, SourceChannels, DestinationChannels, pLevelMatrix);
			}

			// NAME: IXAudio2Voice::DestroyVoice
			// DESCRIPTION: Destroys this voice, stopping it if necessary and removing
			//              it from the XAudio2 graph.
			//
			virtual void DestroyVoice() noexcept
			{
				if (data)
				{
					data->DestroyVoice();
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

		class IXAudio2SourceVoiceProxy :
			public IXAudio2VoiceProxy
		{
			IXAudio2SourceVoiceProxy(IXAudio2SourceVoiceProxy&&) = delete;
			IXAudio2SourceVoiceProxy(const IXAudio2SourceVoiceProxy&) = delete;
			IXAudio2SourceVoiceProxy& operator=(IXAudio2SourceVoiceProxy&&) = delete;
			IXAudio2SourceVoiceProxy& operator=(const IXAudio2SourceVoiceProxy&) = delete;
		public:
			friend class IXAudio2Proxy;

			constexpr IXAudio2SourceVoiceProxy() noexcept = default;

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
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Start();
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
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Stop();
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

				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SubmitSourceBuffer(
					reinterpret_cast<const ::XAUDIO2_BUFFER*>(pBuffer),
					reinterpret_cast<const ::XAUDIO2_BUFFER_WMA*>(pBufferWMA));
			}

			// NAME: IXAudio2SourceVoice::FlushSourceBuffers
			// DESCRIPTION: Removes all pending audio buffers from this voice's queue.
			//
			virtual HRESULT FlushSourceBuffers() noexcept
			{
				if (!data) return E_FAIL;
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->FlushSourceBuffers();
			}

			// NAME: IXAudio2SourceVoice::Discontinuity
			// DESCRIPTION: Notifies the voice of an intentional break in the stream of
			//              audio buffers (e.g. the end of a sound), to prevent XAudio2
			//              from interpreting an empty buffer queue as a glitch.
			//
			virtual HRESULT Discontinuity() noexcept
			{
				if (!data) return E_FAIL;
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->Discontinuity();
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
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->ExitLoop(OperationSet);
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
				(reinterpret_cast<IXAudio2SourceVoice*>(data))->GetState(
					reinterpret_cast<::XAUDIO2_VOICE_STATE*>(pVoiceState));
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
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SetFrequencyRatio(Ratio, OperationSet);
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
				(reinterpret_cast<IXAudio2SourceVoice*>(data))->GetFrequencyRatio(pRatio);
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
				return (reinterpret_cast<IXAudio2SourceVoice*>(data))->SetSourceSampleRate(NewSourceSampleRate);
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

			virtual void GetChannelMask(DWORD* pChannelmask) noexcept 
			{
				if (data && pChannelmask)
					reinterpret_cast<IXAudio2MasteringVoice*>(data)->GetChannelMask(pChannelmask);
			}
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

		#pragma pack(pop)

		// This approach retrieves the stream format that the Windows audio engine uses internally for digital processing.
		// It is the most reliable way to negotiate a stream format for playback or capture.
		static HRESULT GetDeviceFormatFromAudioClient(IMMDevice* pDevice, WAVEFORMATEXTENSIBLE* pFormat) noexcept
		{
			if (!pDevice || !pFormat) return E_INVALIDARG;

			Microsoft::WRL::ComPtr<IAudioClient> pAudioClient{};
			// Activate the IAudioClient interface from the IMMDevice
			HRESULT hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 
				nullptr, (void**)pAudioClient.GetAddressOf());
			if (SUCCEEDED(hr)) 
			{
				// Retrieve the audio engine's mix format
				WAVEFORMATEX* pMixFormat{ nullptr };
				hr = pAudioClient->GetMixFormat(std::addressof(pMixFormat));
				if (SUCCEEDED(hr) && pMixFormat)
				{
					// Copy the format to dest
					CopyMemory(pFormat, pMixFormat, sizeof(WAVEFORMATEX) + pMixFormat->cbSize);
					// Free the format memory when done
					CoTaskMemFree(pMixFormat);
				}
			}

			return hr;
		}

		class IXAudio2Proxy :
			public IUnknown
		{
			volatile long ref{ 1 };
			IXAudio2* audio{ nullptr };

			IXAudio2Proxy(IXAudio2Proxy&&) = delete;
			IXAudio2Proxy(const IXAudio2Proxy&) = delete;
			IXAudio2Proxy& operator=(IXAudio2Proxy&&) = delete;
			IXAudio2Proxy& operator=(const IXAudio2Proxy&) = delete;
		public:
			friend class IXAudio2SourceVoiceProxy;

			IXAudio2Proxy() noexcept
			{
				auto hr = XAudio2Create(std::addressof(audio), 0, XAUDIO2_USE_DEFAULT_PROCESSOR);
				if (FAILED(hr))
					REX::ERROR("XAudio2Create return failed \"{}\"", _com_error(hr).ErrorMessage());
			}

			~IXAudio2Proxy() noexcept
			{
				if (audio)
				{
					audio->Release();
					audio = nullptr;
				}
			}

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

				Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
				auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
					__uuidof(IMMDeviceEnumerator), (void**)enumerator.GetAddressOf());

				if (SUCCEEDED(hr))
				{
					Microsoft::WRL::ComPtr<IMMDeviceCollection> collection{};
					hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf());
					if (SUCCEEDED(hr))
					{
						collection->GetCount(pCount);
						return S_OK;
					}
				}

				return E_FAIL;
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

				std::fill_n(reinterpret_cast<uint8_t*>(pDeviceDetails), sizeof(XAUDIO2_DEVICE_DETAILS), 0);

				Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
				auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
					__uuidof(IMMDeviceEnumerator), (void**)enumerator.GetAddressOf());
				if (SUCCEEDED(hr))
				{
					Microsoft::WRL::ComPtr<IMMDeviceCollection> collection{};
					hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf());
					if (SUCCEEDED(hr))
					{
						UINT count{};
						collection->GetCount(std::addressof(count));
						if (count <= Index)
							return E_BOUNDS;

						Microsoft::WRL::ComPtr<IMMDevice> device{};
						hr = collection->Item(Index, device.GetAddressOf());
						if (FAILED(hr))
							return E_FAIL;

						LPWSTR pIdStr{ nullptr };
						hr = device->GetId(std::addressof(pIdStr));
						if (FAILED(hr))
							return E_FAIL;

						wcscpy_s(pDeviceDetails->DeviceID, pIdStr);

						Microsoft::WRL::ComPtr<IPropertyStore> props{};
						if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf())))
						{
							PROPVARIANT varName{};
							PropVariantInit(std::addressof(varName));

							// Retrieve the human-readable name
							if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, std::addressof(varName))))
								wcscpy_s(pDeviceDetails->DisplayName, varName.pwszVal);

							PropVariantClear(std::addressof(varName));
						}
						
						hr = GetDeviceFormatFromAudioClient(device.Get(),
							std::addressof(pDeviceDetails->OutputFormat));
						if (FAILED(hr))
							REX::ERROR(L"Failed get audio format for audio device: {} {}",
								pIdStr, SysCharToWide(_com_error(hr).ErrorMessage()).c_str());
						/*else
						{
							REX::INFO("[AudioProxy] Audio format: {}Hz {}bit {} channels",
								pDeviceDetails->OutputFormat.Format.nSamplesPerSec,
								pDeviceDetails->OutputFormat.Format.wBitsPerSample,
								pDeviceDetails->OutputFormat.Format.nChannels
							);
						}*/

						{
							Microsoft::WRL::ComPtr<IMMDevice> defDevice{};
							if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, defDevice.GetAddressOf())))
							{
								LPWSTR pIdCurStr{ nullptr };
								if (SUCCEEDED(defDevice->GetId(std::addressof(pIdCurStr))) && !wcscmp(pIdStr, pIdCurStr))
									pDeviceDetails->Role = GlobalDefaultDevice;
								if (pIdCurStr)
									CoTaskMemFree(pIdCurStr);
							}
						}

						IXAudio2MasteringVoice* pMasteringVoice{ nullptr };
						if (SUCCEEDED(audio->CreateMasteringVoice(std::addressof(pMasteringVoice), 0, 0, 0, pIdStr)))
						{
							// pMasteringVoice->GetChannelMask(std::addressof(pDeviceDetails->OutputFormat.dwChannelMask));
							// pDeviceDetails->OutputFormat.Samples.wValidBitsPerSample = pDeviceDetails->OutputFormat.Format.wBitsPerSample;
							// pDeviceDetails->OutputFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
							pMasteringVoice->DestroyVoice();
						}

						CoTaskMemFree(pIdStr);
						return hr;
					}
				}
				
				return E_FAIL;
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

				return S_OK;
			}

			// NAME: IXAudio2::RegisterForCallbacks
			// DESCRIPTION: Adds a new client to receive XAudio2's engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Callback interface to be called during each processing pass.
			//
			virtual HRESULT RegisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept
			{
				if (!audio || !pCallback) return E_FAIL;
				return audio->RegisterForCallbacks(reinterpret_cast<::IXAudio2EngineCallback*>(pCallback));
			}

			// NAME: IXAudio2::UnregisterForCallbacks
			// DESCRIPTION: Removes an existing receiver of XAudio2 engine callbacks.
			//
			// ARGUMENTS:
			//  pCallback - Previously registered callback interface to be removed.
			//
			virtual void UnregisterForCallbacks(IXAudio2EngineCallback* pCallback) noexcept
			{
				if (!audio || !pCallback) return;
				audio->UnregisterForCallbacks(reinterpret_cast<::IXAudio2EngineCallback*>(pCallback));
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

				*ppSourceVoice = new IXAudio2SourceVoiceProxy();
				if (!(*ppSourceVoice)) return E_OUTOFMEMORY;

				EnsureEffectChainCompat(pEffectChain);

				HRESULT hr = E_FAIL;
				if (pSendList && pSendList->SendCount)
				{
					::XAUDIO2_VOICE_SENDS sends{};
					if (SUCCEEDED(IXAudio2VoiceProxy::CopyVoiceSends(&sends, pSendList)))
					{
						hr = audio->CreateSourceVoice(reinterpret_cast<::IXAudio2SourceVoice**>(std::addressof((*ppSourceVoice)->data)),
							pSourceFormat, Flags, MaxFrequencyRatio, reinterpret_cast<::IXAudio2VoiceCallback*>(pCallback),
							std::addressof(sends),
							reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
						delete[] sends.pSends;
					}
				}
				else
					hr = audio->CreateSourceVoice(reinterpret_cast<::IXAudio2SourceVoice**>(std::addressof((*ppSourceVoice)->data)),
						pSourceFormat, Flags, MaxFrequencyRatio, reinterpret_cast<::IXAudio2VoiceCallback*>(pCallback),
						nullptr,
						reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));

				if (FAILED(hr))
				{
					delete *ppSourceVoice;
					*ppSourceVoice = nullptr;
				}
				return hr;
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
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept
			{
				if (!audio || !ppSubmixVoice)
					return E_FAIL;

				* ppSubmixVoice = new IXAudio2SubmixVoiceProxy();
				if (!(*ppSubmixVoice)) return E_OUTOFMEMORY;

				EnsureEffectChainCompat(pEffectChain);

				HRESULT hr = E_FAIL;
				if (pSendList && pSendList->SendCount)
				{
					::XAUDIO2_VOICE_SENDS sends{};
					if (SUCCEEDED(IXAudio2VoiceProxy::CopyVoiceSends(&sends, pSendList)))
					{
						hr = audio->CreateSubmixVoice(reinterpret_cast<::IXAudio2SubmixVoice**>(std::addressof((*ppSubmixVoice)->data)),
							InputChannels, InputSampleRate, Flags, ProcessingStage, std::addressof(sends),
							reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
						delete[] sends.pSends;
					}
				}
				else
					hr = audio->CreateSubmixVoice(reinterpret_cast<::IXAudio2SubmixVoice**>(std::addressof((*ppSubmixVoice)->data)),
						InputChannels, InputSampleRate, Flags, ProcessingStage, nullptr,
						reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));

				if (FAILED(hr))
				{
					delete *ppSubmixVoice;
					*ppSubmixVoice = nullptr;
				}
				return hr;
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
				UINT32 InputChannels = XAUDIO2_DEFAULT_CHANNELS,
				UINT32 InputSampleRate = XAUDIO2_DEFAULT_SAMPLERATE,
				UINT32 Flags = 0, UINT32 DeviceIndex = 0,
				const XAUDIO2_EFFECT_CHAIN* pEffectChain = nullptr) noexcept
			{
				if (!audio || !ppMasteringVoice)
					return E_FAIL;

				*ppMasteringVoice = new IXAudio2MasteringVoiceProxy();
				if (!(*ppMasteringVoice)) return E_OUTOFMEMORY;

				EnsureEffectChainCompat(pEffectChain);
				HRESULT hr = audio->CreateMasteringVoice(reinterpret_cast<::IXAudio2MasteringVoice**>(std::addressof((*ppMasteringVoice)->data)),
					InputChannels, InputSampleRate, Flags, nullptr,
					reinterpret_cast<const ::XAUDIO2_EFFECT_CHAIN*>(pEffectChain));
				if (FAILED(hr))
				{
					delete *ppMasteringVoice;
					*ppMasteringVoice = nullptr;
				}
				return hr;
			}
			
			// NAME: IXAudio2::StartEngine
			// DESCRIPTION: Creates and starts the audio processing thread.
			//
			virtual HRESULT StartEngine() noexcept
			{
				if (!audio) return E_FAIL;
				return audio->StartEngine();
			}

			// NAME: IXAudio2::StopEngine
			// DESCRIPTION: Stops and destroys the audio processing thread.
			//
			virtual void StopEngine() noexcept
			{
				if (!audio) return;
				return audio->StopEngine();
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
				return audio->CommitChanges(OperationSet);
			}

			// NAME: IXAudio2::GetPerformanceData
			// DESCRIPTION: Returns current resource usage details: memory, CPU, etc.
			//
			// ARGUMENTS:
			//  pPerfData - Returns the performance data structure.
			//
			virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* pPerfData) const noexcept
			{
				if (!audio || !pPerfData) return;
				audio->GetPerformanceData(reinterpret_cast<::XAUDIO2_PERFORMANCE_DATA*>(pPerfData));
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
				if (!audio || !pDebugConfiguration) return;
				audio->SetDebugConfiguration(reinterpret_cast<const ::XAUDIO2_DEBUG_CONFIGURATION*>(pDebugConfiguration), nullptr);
			}
		};

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

		static HRESULT Hook_X3DAudioInitialize([[maybe_unused]] UINT32 SpeakerChannelMask, [[maybe_unused]] float SpeedOfSound,
			X3DAUDIO_HANDLE* Instance) noexcept
		{
			auto graph = RE::BSXAudio2Graph::GetSingleton();
			if (!graph || !graph->masteringVoice)
				return X3DAudioInitialize(0, X3DAUDIO_SPEED_OF_SOUND, reinterpret_cast<LPBYTE>(Instance));
			else
			{
				DWORD dwChannelMask;
				graph->masteringVoice->GetChannelMask(std::addressof(dwChannelMask));
				return X3DAudioInitialize(dwChannelMask, X3DAUDIO_SPEED_OF_SOUND, reinterpret_cast<LPBYTE>(Instance));
			}
		}
	}

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

	static HRESULT XAudio2Reverb_CoCreateInstance([[maybe_unused]] REFCLSID rclsid, [[maybe_unused]] LPUNKNOWN pUnkOuter,
		[[maybe_unused]] DWORD dwClsContext, [[maybe_unused]] REFIID riid, LPVOID* ppv) noexcept
	{
		return XAudio2CreateReverb(reinterpret_cast<IUnknown**>(ppv));
	}

	ModuleAudioProxy::ModuleAudioProxy() :
		Module("Audio Proxy", &bPatchesAudioProxy)
	{}

	bool ModuleAudioProxy::DoQuery() const noexcept
	{
		// Wine does not have a proper XAudio2_9 implementation
		if (UserUseWine() && IsWineBuiltinDLL("XAudio2_9.dll"))
		{
			REX::WARN("Audio Proxy: Wine XAudio2_9.dll detected, disabling...");
			return false;
		}

		return true;
	}

	bool ModuleAudioProxy::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		RELEX::DetourCall(REL::Relocation(REL::ID{ 303985, 2267547 }, REL::Offset{ 0x43, 0x108 }).address(),
			(uintptr_t)&XAudio2_CoCreateInstance);
		RELEX::DetourCall(REL::Relocation(REL::ID{ 1288546, 2267579 }, REL::Offset{ 0x5D, 0xFD }).address(),
			(uintptr_t)&XAudio2Reverb_CoCreateInstance);
		RELEX::DetourCall(REL::Relocation(REL::ID{ 1537694, 2267536 }, REL::Offset{ 0xA2, 0x123 }).address(),
			(uintptr_t)&detail::Hook_X3DAudioInitialize);

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
