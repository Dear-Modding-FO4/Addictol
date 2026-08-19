// #original: https://github.com/aers/EngineFixesSkyrim64/blob/master/src/fixes/music_overlap.h

#include <Modules/AdModuleMusicOverlap.h>
#include <AdUtils.h>

#include <RE/B/BGSMusicType.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesMusicOverlap{ "Fixes"sv, "bMusicOverlap"sv, true };

	namespace detail
	{
		static void DoFinish(RE::BSIMusicType* a_self, bool a_immediate)
		{
			if (!a_self || !a_self->tracks.data() || a_self->currentTrackIndex >= a_self->tracks.size() || !a_self->tracks[a_self->currentTrackIndex])
				return;
			
			a_self->tracks[a_self->currentTrackIndex]->DoFinish(a_immediate, std::max(a_self->fadeTime, 4.0f));
			a_self->typeStatus = a_self->tracks[a_self->currentTrackIndex]->GetMusicStatus();
		}
	}

	ModuleMusicOverlap::ModuleMusicOverlap() :
		Module("Music Overlap", &bFixesMusicOverlap)
	{}

	bool ModuleMusicOverlap::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto vtable = RE::VTABLE::BGSMusicType[1].address();
		if (!vtable)
		{
			REX::WARN("[MusicOverlap] Could not resolve BGSMusicType vtable; skipping."sv);
			return false;
		}

		return RELEX::DetourVTable(vtable, reinterpret_cast<uintptr_t>(&detail::DoFinish), 3) != 0;
	}

}
