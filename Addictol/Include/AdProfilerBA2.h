#pragma once

#include <REX/REX.h>

#include <cstddef>
#include <span>
#include <string_view>

#include <AdProfilerBA2Schema.h>

namespace Addictol
{
	using namespace std::literals;

	class ProfilerBA2 :
		public REX::Singleton<ProfilerBA2>
	{
		void Publish(std::string_view a_reason, bool a_closeAdmission) noexcept;

	public:
		[[nodiscard]] bool Start() noexcept;
		[[nodiscard]] bool IsRecording() const noexcept;

		void Record(const BA2Profile::CallObservation& a_observation) noexcept;
		void RecordBatch(std::span<const BA2Profile::CallObservation> a_observations) noexcept;
		void Publish(std::string_view a_reason) noexcept;
		void PublishFinal(std::string_view a_reason) noexcept;

		// Reads the retained publish summary only; live shards and call arenas are never touched.
		[[nodiscard]] bool CopyLatestPublished(BA2PublishedSnapshot& a_out) const noexcept;
	};
}
