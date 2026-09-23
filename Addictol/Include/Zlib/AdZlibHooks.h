#pragma once
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

#include <Core/AdClock.h>
#include <Core/AdDetourBatch.h>
#include <Modules/AdModuleLibDeflate.h>
#include <Zlib/AdZlibInstallation.h>
#include <Zlib/AdZlibOperationProfile.h>
#include <Zlib/AdZlibTelemetry.h>

namespace Addictol::ZlibHooks
{
	inline std::array<RELEX::DetourTarget, static_cast<size_t>(ZlibEntry::Count)> targets{};
	template<ZlibEntry Entry, class... Args>
	int32_t Original(Args... a_args) noexcept
	{
		return reinterpret_cast<int32_t(*)(Args...)>(targets[static_cast<size_t>(Entry)].original)(a_args...);
	}
	template<class Backend, bool Profile>
	struct Selected
	{
		using Owned = OwnedInflate<typename Backend::Whole, typename Backend::Streaming>;
		using Stream = ZlibInflate::Stream;

		template<ZlibEntry Entry, auto Function, class... Args>
		static int32_t Call(Stream* a_stream, Args... a_args) noexcept
		{
			return RouteZlibStream<Owned>(a_stream,
				[&] { return Original<Entry>(a_stream, a_args...); },
				[&] { return Function(a_stream, a_args...); });
		}
		static int32_t Init(Stream* a_stream, const char* a_version, int32_t a_size) noexcept { return Owned::Init(a_stream, a_version, a_size); }
		static int32_t Init2(Stream* a_stream, int32_t a_bits, const char* a_version, int32_t a_size) noexcept { return Owned::Init2(a_stream, a_bits, a_version, a_size); }
		static int32_t Inflate(Stream* a_stream, int32_t a_flush) noexcept
		{
			return RouteZlibStream<Owned>(a_stream, [&] { return Original<ZlibEntry::Inflate>(a_stream, a_flush); }, [&] {
				return ServeProfiledZlib<Backend, Profile>(Profile ? ZlibOperationProfile() : nullptr, [&] {
					return TelemetryDetail::ServeTelemetryZlib<Owned>(a_stream, a_flush,
						[] { return ReadQpc(); }, [] { return GetCurrentThreadId(); },
						[&](const ZlibInflateOutcome& a_outcome, bool a_enabled, uint32_t a_thread) {
							ModuleLibDeflate::Record(a_outcome, a_enabled, a_flush, a_thread, a_outcome.consumed, a_outcome.produced);
						});
				}).zlibResult;
			});
		}
		static int32_t Copy(Stream* a_destination, Stream* a_source) noexcept
		{
			return RouteZlibStream<Owned>(a_source,
				[&] { return Original<ZlibEntry::Copy>(a_destination, a_source); },
				[&] { return Owned::Copy(a_destination, a_source); });
		}
		static int32_t SetDictionary(Stream* a_stream, const uint8_t* a_dictionary, uint32_t a_size) noexcept
		{
			return RouteZlibStream<Owned>(a_stream,
				[&] { return Original<ZlibEntry::SetDictionary>(a_stream, a_dictionary, a_size); },
				[&] { return a_dictionary ? Owned::SetDictionary(a_stream, { a_dictionary, a_size }) : INFLATE_STREAM_ERROR; });
		}
		static void Select() noexcept
		{
			const std::array hooks{
				reinterpret_cast<void*>(&Inflate), reinterpret_cast<void*>(&Copy),
				reinterpret_cast<void*>(&Call<ZlibEntry::End, Owned::End>),
				reinterpret_cast<void*>(&Call<ZlibEntry::GetHeader, Owned::GetHeader, InflateHeader*>),
				reinterpret_cast<void*>(&Init2), reinterpret_cast<void*>(&Init),
				reinterpret_cast<void*>(&Call<ZlibEntry::Mark, Owned::Mark>),
				reinterpret_cast<void*>(&Call<ZlibEntry::Prime, Owned::Prime, int32_t, int32_t>),
				reinterpret_cast<void*>(&Call<ZlibEntry::Reset, Owned::Reset>),
				reinterpret_cast<void*>(&Call<ZlibEntry::Reset2, Owned::Reset2, int32_t>),
				reinterpret_cast<void*>(&Call<ZlibEntry::ResetKeep, Owned::ResetKeep>),
				reinterpret_cast<void*>(&SetDictionary),
				reinterpret_cast<void*>(&Call<ZlibEntry::Sync, Owned::Sync>),
				reinterpret_cast<void*>(&Call<ZlibEntry::SyncPoint, Owned::SyncPoint>),
				reinterpret_cast<void*>(&Call<ZlibEntry::Undermine, Owned::Undermine, int32_t>)
			};
			for (size_t index = 0; index < hooks.size(); ++index)
				targets[index].replacement = hooks[index];
		}
	};
}
