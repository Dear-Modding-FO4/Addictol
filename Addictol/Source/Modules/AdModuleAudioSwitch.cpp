#include <Modules/AdModuleAudioSwitch.h>
#include <Core/AdUtils.h>

#include <RE/B/BSAudioManager.h>
#include <RE/B/BSGameSound.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSXAudio2Audio.h>
#include <RE/B/BSXAudio2Graph.h>
#include <RE/M/MemoryManager.h>
#include <RE/M/MonitorAPO.h>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#undef ERROR

namespace Addictol
{
	namespace audioSwitchDetail
	{
		using Graph = RE::BSXAudio2Graph;
		using State = RE::BSAudioManager::State;
		using SoundFlags = RE::BSGameSound::FLAGS;

		// Coalesces the per-role notifications Windows sends for one change.
		inline constexpr uint64_t kDebounceMs = 100;
		// Streaming sounds refresh their queue state every ~66 ms before retiring.
		inline constexpr uint64_t kDrainTimeoutMs = 3000;
		inline constexpr uint64_t kRetryBaseMs = 1000;
		inline constexpr uint64_t kRetryMaxMs = 30000;
		// CreateMonitor recipe: mono 48 kHz submix processed after every other voice.
		inline constexpr uint32_t kMonitorSampleRate = 48000;
		inline constexpr uint32_t kMonitorProcessingStage = 0xFFFFFFFF;

		enum class Phase
		{
			kIdle,
			kDraining
		};

		// Rebuild request: GetTickCount64 deadline, 0 when none.
		static std::atomic<uint64_t> s_requestDue{ 0 };
		// Rebuild even when the default endpoint did not change (device loss).
		static std::atomic_bool s_requestForced{ false };
		static std::atomic_bool s_shutdown{ false };

		// Audio thread only.
		static Phase s_phase{ Phase::kIdle };
		static Graph* s_pendingGraph{ nullptr };
		static uint64_t s_drainDeadline{ 0 };
		static uint32_t s_failures{ 0 };

		// Guards the live graph against telemetry readers during swap and shutdown.
		static std::mutex s_graphLock;

		static std::mutex s_endpointLock;
		static std::wstring s_boundEndpoint;

		static void (*s_signal)() { nullptr };
		static void (*s_onCriticalError)(Graph*, REX::W32::HRESULT) { nullptr };
		static void (*s_audioShutdown)(RE::BSXAudio2Audio*) { nullptr };

		static void Request(uint64_t a_delayMs, bool a_forced) noexcept
		{
			if (a_forced)
				s_requestForced.store(true, std::memory_order_relaxed);

			// Earliest deadline wins: bursts coalesce, a device arrival cuts backoff short.
			const auto due = GetTickCount64() + a_delayMs;
			auto current = s_requestDue.load(std::memory_order_relaxed);
			while ((!current || due < current) && !s_requestDue.compare_exchange_weak(current, due))
				;
		}

		static void RequestRetry() noexcept
		{
			const auto shift = std::min<uint32_t>(s_failures, 5);
			Request(std::min(kRetryBaseMs << shift, kRetryMaxMs), true);
			++s_failures;
		}

		namespace endpoint
		{
			using Microsoft::WRL::ComPtr;

			class NotificationClient final :
				public IMMNotificationClient
			{
			public:
				[[nodiscard]] bool Register() noexcept
				{
					auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
						IID_PPV_ARGS(m_enumerator.ReleaseAndGetAddressOf()));
					if (SUCCEEDED(hr))
						hr = m_enumerator->RegisterEndpointNotificationCallback(this);
					if (FAILED(hr))
					{
						REX::WARN("[AudioSwitch] Endpoint notifications unavailable ({:08X})"sv, static_cast<uint32_t>(hr));
						m_enumerator.Reset();
						return false;
					}
					return true;
				}

				[[nodiscard]] std::wstring DefaultEndpoint() const noexcept
				{
					ComPtr<IMMDevice> device;
					LPWSTR id = nullptr;
					if (!m_enumerator || FAILED(m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
						FAILED(device->GetId(&id)))
						return {};

					std::wstring result{ id };
					CoTaskMemFree(id);
					return result;
				}

				// Never released: MMDevAPI keeps a raw pointer for the process lifetime.
				ULONG STDMETHODCALLTYPE AddRef() noexcept override { return 1; }
				ULONG STDMETHODCALLTYPE Release() noexcept override { return 1; }

				HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_out) noexcept override
				{
					if (a_iid == __uuidof(IUnknown) || a_iid == __uuidof(IMMNotificationClient))
					{
						*a_out = static_cast<IMMNotificationClient*>(this);
						return S_OK;
					}
					*a_out = nullptr;
					return E_NOINTERFACE;
				}

				HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow a_flow, ERole a_role, LPCWSTR a_id) noexcept override
				{
					// XAudio2 2.7 follows the console and multimedia roles, not communications.
					if (a_flow != eRender || a_role == eCommunications || !a_id)
						return S_OK;

					{
						const std::lock_guard lock{ s_endpointLock };
						if (s_boundEndpoint == a_id)
							return S_OK;
					}

					Request(kDebounceMs, false);
					return S_OK;
				}

				HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) noexcept override { return S_OK; }
				HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) noexcept override { return S_OK; }
				HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) noexcept override { return S_OK; }
				HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) noexcept override { return S_OK; }

			private:
				ComPtr<IMMDeviceEnumerator> m_enumerator;
			};

			static NotificationClient* s_client{ nullptr };

			static void Bind() noexcept
			{
				auto id = s_client ? s_client->DefaultEndpoint() : std::wstring{};
				const std::lock_guard lock{ s_endpointLock };
				s_boundEndpoint = std::move(id);
			}

			[[nodiscard]] static bool IsBound() noexcept
			{
				if (!s_client)
					return false;

				const auto id = s_client->DefaultEndpoint();
				const std::lock_guard lock{ s_endpointLock };
				return !id.empty() && id == s_boundEndpoint;
			}
		}

		[[nodiscard]] static Graph** GraphSlot() noexcept
		{
			static REL::Relocation<Graph**> slot{ RE::ID::BSXAudio2Graph::Singleton };
			return slot.get();
		}

		// Tears down what the engine's graph destructor leaves behind, then runs it.
		static void ReleaseGraph(Graph* a_graph) noexcept
		{
			if (a_graph->m_instance)
				a_graph->m_instance->StopEngine();

			for (auto& send : a_graph->m_reverbSendArray)
			{
				if (send.m_reverbVoice)
					std::exchange(send.m_reverbVoice, nullptr)->DestroyVoice();
				// SetupReverb keeps its creation reference and vanilla never drops it.
				if (send.m_effectAPO)
					std::exchange(send.m_effectAPO, nullptr)->Release();
			}

			a_graph->Dtor();
		}

		[[nodiscard]] static Graph* BuildGraph() noexcept
		{
			auto* const graph = static_cast<Graph*>(RE::malloc(sizeof(Graph)));
			if (!graph)
				return nullptr;

			if (graph->Ctor(false)->m_valid)
				return graph;

			ReleaseGraph(graph);
			return nullptr;
		}

		// The vanilla KillAll pass: the manager then retires each sound normally.
		static void StopAllSounds(RE::BSAudioManager* a_manager) noexcept
		{
			for (auto& [id, sound] : a_manager->soundMap)
			{
				if (!sound)
					continue;

				sound->Stop(true);
				sound->flags.reset(SoundFlags::kFading);
				sound->flags.set(SoundFlags::kRetire);
			}
		}

		// Prepared sounds own a source voice on the current engine.
		[[nodiscard]] static bool HasPreparedSounds(RE::BSAudioManager* a_manager) noexcept
		{
			for (auto& [id, sound] : a_manager->soundMap)
				if (sound && sound->flags.all(SoundFlags::kPrepared))
					return true;

			return false;
		}

		static void CopyReverb(const Graph* a_from, Graph* a_to) noexcept
		{
			const auto now = GetTickCount64();
			for (size_t i = 0; i < a_to->m_reverbSendArray.size(); ++i)
			{
				const auto& from = a_from->m_reverbSendArray[i];
				auto& to = a_to->m_reverbSendArray[i];
				if (!to.m_reverbVoice)
					continue;

				to.m_currentParams = from.m_currentParams;
				to.m_targetParams = from.m_targetParams;
				to.m_transitionStart = from.m_transitionEnd ? from.m_transitionStart : now;
				// The engine thread pushes the target once this is set; write it last.
				std::atomic_ref{ to.m_transitionEnd }.store(from.m_transitionEnd ? from.m_transitionEnd : now);
			}
		}

		// Keeps the APOs: the game caches pointers to their peak values.
		static void MoveMonitors(Graph* a_graph) noexcept
		{
			const std::vector<float> mute(a_graph->m_outputChannels, 0.0f);
			const RE::BSAutoLock lock{ RE::BSXAudio2Audio::GetMonitorLock() };

			uint32_t moved = 0;
			uint32_t failed = 0;
			for (auto* const monitors : { RE::BSXAudio2Audio::GetActiveMonitors(), RE::BSXAudio2Audio::GetInactiveMonitors() })
			{
				for (auto& monitor : *monitors)
				{
					if (!monitor.m_apo)
						continue;

					if (monitor.m_voice)
						std::exchange(monitor.m_voice, nullptr)->DestroyVoice();
					monitor.m_apo->m_peak = 0.0f;

					REX::W32::XAUDIO2_EFFECT_DESCRIPTOR effect{ monitor.m_apo, TRUE, 1 };
					REX::W32::XAUDIO2_EFFECT_CHAIN chain{ 1, &effect };
					REX::W32::IXAudio27SubmixVoice* voice = nullptr;
					if (FAILED(a_graph->m_instance->CreateSubmixVoice(&voice, 1, kMonitorSampleRate, 0,
							kMonitorProcessingStage, nullptr, &chain)))
					{
						++failed;
						continue;
					}

					// Monitors only measure; keep them out of the mix.
					voice->SetOutputMatrix(nullptr, 1, a_graph->m_outputChannels, mute.data());
					monitor.m_voice = voice;
					++moved;
				}
			}

			if (failed)
				REX::WARN("[AudioSwitch] {} of {} audio monitors could not be recreated"sv, failed, moved + failed);
		}

		[[nodiscard]] static float SpeedOfSound(const RE::BSAudioManager* a_manager) noexcept
		{
			// Mirrors SetSpeedOfSound, which stores 1000 / speed and applies (int)speed.
			if (a_manager->invSpeedInUnitsPerMS > 0.0f)
				return std::round(1000.0f / a_manager->invSpeedInUnitsPerMS);

			static REL::Relocation<const float*> initial{ RE::ID::BSAudioManager::DefaultSpeedOfSound };
			return *initial;
		}

		// Refreshes everything BSXAudio2Audio::Init derived from the device.
		static void ApplyDeviceState(RE::BSAudioManager* a_manager, RE::BSXAudio2Audio* a_audio, const Graph* a_graph) noexcept
		{
			static REL::Relocation<void (*)()> freeMatrices{ RE::ID::BSXAudio2GameSound::FreeDefaultOutputMatrices };
			static REL::Relocation<void (*)()> setupMatrices{ RE::ID::BSXAudio2GameSound::SetupDefaultOutputMatrices };

			a_audio->m_outputChannels = a_graph->m_outputChannels;
			freeMatrices();
			setupMatrices();
			a_audio->Initialize3D(SpeedOfSound(a_manager));
		}

		static void Swap(RE::BSAudioManager* a_manager, RE::BSXAudio2Audio* a_audio) noexcept
		{
			auto* const graph = std::exchange(s_pendingGraph, nullptr);
			{
				const std::lock_guard lock{ s_graphLock };
				auto* const old = *GraphSlot();

				// Cached sounds keep source voices on the old engine.
				a_manager->ClearCache();
				CopyReverb(old, graph);
				MoveMonitors(graph);

				*GraphSlot() = graph;
				ApplyDeviceState(a_manager, a_audio, graph);
				ReleaseGraph(old);
			}

			endpoint::Bind();
			s_failures = 0;
			REX::INFO("[AudioSwitch] Audio output moved to the default device ({} channels, mask {:#x})"sv,
				graph->m_outputChannels, graph->m_masterChannelMask);
		}

		[[nodiscard]] static RE::BSXAudio2Audio* GetAudio(const RE::BSAudioManager* a_manager) noexcept
		{
			static REL::Relocation<uintptr_t> vtable{ RE::BSXAudio2Audio::VTABLE[0] };

			auto* const platform = RE::BSAudioManager::GetPlatformSingleton();
			if (!a_manager->stateFlags.all(State::ManagerInitialized) || !platform ||
				*reinterpret_cast<const uintptr_t*>(platform) != vtable.address() || !*GraphSlot())
				return nullptr;

			return static_cast<RE::BSXAudio2Audio*>(platform);
		}

		static void Begin(RE::BSAudioManager* a_manager) noexcept
		{
			auto due = s_requestDue.load(std::memory_order_acquire);
			if (!due || GetTickCount64() < due || !s_requestDue.compare_exchange_strong(due, 0))
				return;

			const auto forced = s_requestForced.exchange(false, std::memory_order_relaxed);
			if (!GetAudio(a_manager) || (!forced && endpoint::IsBound()))
				return;

			// Build first so a missing or unready device costs nothing.
			s_pendingGraph = BuildGraph();
			if (!s_pendingGraph)
			{
				REX::WARN("[AudioSwitch] No usable audio device yet; keeping the current output (attempt {})"sv, s_failures + 1);
				RequestRetry();
				return;
			}

			StopAllSounds(a_manager);
			s_drainDeadline = GetTickCount64() + kDrainTimeoutMs;
			s_phase = Phase::kDraining;
		}

		static void Drain(RE::BSAudioManager* a_manager) noexcept
		{
			auto* const audio = GetAudio(a_manager);
			if (audio)
			{
				// Keep retiring sounds that started while draining.
				StopAllSounds(a_manager);
				if (HasPreparedSounds(a_manager))
				{
					if (GetTickCount64() < s_drainDeadline)
						return;

					REX::WARN("[AudioSwitch] Sounds did not stop in time; keeping the current output"sv);
				}
				else
				{
					Swap(a_manager, audio);
					s_phase = Phase::kIdle;
					return;
				}
			}

			ReleaseGraph(std::exchange(s_pendingGraph, nullptr));
			s_phase = Phase::kIdle;
			if (audio)
				RequestRetry();
		}

		// Runs once per audio-thread loop, after messages and before sound updates.
		static void HKSignal() noexcept
		{
			s_signal();

			if (s_shutdown.load(std::memory_order_acquire))
				return;

			auto* const manager = RE::BSAudioManager::GetSingleton();
			if (!manager)
				return;

			if (s_phase == Phase::kIdle)
				Begin(manager);
			else
				Drain(manager);
		}

		// XAudio2 engine thread: the device was lost or invalidated.
		static void HKOnCriticalError(Graph* a_graph, REX::W32::HRESULT a_error) noexcept
		{
			s_onCriticalError(a_graph, a_error);
			REX::WARN("[AudioSwitch] XAudio2 critical error {:08X}; rebuilding output"sv, static_cast<uint32_t>(a_error));
			Request(0, true);
		}

		static void HKAudioShutdown(RE::BSXAudio2Audio* a_audio) noexcept
		{
			{
				const std::lock_guard lock{ s_graphLock };
				s_shutdown.store(true, std::memory_order_release);
			}

			// Same thread as HKSignal, so no rebuild is in flight here.
			if (s_pendingGraph)
				ReleaseGraph(std::exchange(s_pendingGraph, nullptr));

			s_audioShutdown(a_audio);
		}

		[[nodiscard]] static bool ReadTelemetry(AudioPerformanceMetricSource::Values& a_values) noexcept
		{
			const std::lock_guard lock{ s_graphLock };
			if (s_shutdown.load(std::memory_order_acquire))
				return false;

			const auto* const graph = *GraphSlot();
			return graph && ReadAudioPerformanceMetricValues<REX::W32::XAUDIO2_PERFORMANCE_DATA>(
				graph->m_instance, graph->m_masteringVoice, a_values);
		}

		[[nodiscard]] static REL::Relocation<uintptr_t> SignalCallSite() noexcept
		{
			return REL::Relocation<uintptr_t>{ RE::ID::BSAudioManagerThread::ThreadProc, REL::Offset{ 0x34, 0x36 } };
		}

		[[nodiscard]] static bool IsSignalCallSiteVanilla() noexcept
		{
			const auto site = SignalCallSite().address();
			if (!RELEX::Validate(site, { 0xE8 }))
				return false;

			const auto target = site + 5 + *reinterpret_cast<const int32_t*>(site + 1);
			return target == REL::Relocation<uintptr_t>{ RE::ID::BSSystemFileStreamer::Signal }.address();
		}
	}

	ModuleAudioSwitch::ModuleAudioSwitch() :
		Module("Audio Switch", &bPatchesAudioSwitch),
		AudioPerformanceMetricSource(
			kAudioPerformanceMetricSchema, &audioSwitchDetail::ReadTelemetry)
	{}

	bool ModuleAudioSwitch::DoQuery() const noexcept
	{
		if (IsModDLLPresent("AudioDeviceFollowFix.dll"))
		{
			Skip("equivalent mod 'AudioDeviceFollowFix.dll' is installed"sv);
			return false;
		}

		if (!audioSwitchDetail::IsSignalCallSiteVanilla())
		{
			Skip("the audio thread loop is already patched"sv);
			return false;
		}

		return true;
	}

	bool ModuleAudioSwitch::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace audioSwitchDetail;

		s_signal = reinterpret_cast<decltype(s_signal)>(
			RELEX::DetourCall(SignalCallSite().address(), reinterpret_cast<uintptr_t>(&HKSignal)));
		if (!s_signal)
			return false;

		s_onCriticalError = reinterpret_cast<decltype(s_onCriticalError)>(RELEX::DetourVTable(
			REL::Relocation<uintptr_t>{ Graph::VTABLE }.address(), reinterpret_cast<uintptr_t>(&HKOnCriticalError), 2));
		s_audioShutdown = reinterpret_cast<decltype(s_audioShutdown)>(RELEX::DetourVTable(
			REL::Relocation<uintptr_t>{ RE::BSXAudio2Audio::VTABLE[0] }.address(), reinterpret_cast<uintptr_t>(&HKAudioShutdown), 2));
		if (!s_onCriticalError || !s_audioShutdown)
			return false;

		auto* const client = new (std::nothrow) endpoint::NotificationClient();
		if (!client || !client->Register())
		{
			delete client;
			REX::WARN("[AudioSwitch] Following default device changes is disabled; device loss is still handled"sv);
			return true;
		}

		endpoint::s_client = client;
		endpoint::Bind();
		return true;
	}

	bool ModuleAudioSwitch::HasProcessDefender() noexcept
	{
		return true;
	}
}
