#include <Zlib/AdZlibStreamTracker.h>

namespace Addictol
{
	ZlibStreamInput ZlibStreamInput::Read(const ZlibInflate::Stream& a_stream) noexcept
	{
		return {
			a_stream.state,
			reinterpret_cast<uintptr_t>(a_stream.next_in) + a_stream.avail_in,
			a_stream.avail_in,
			a_stream.total_in == 0 && a_stream.total_out == 0
		};
	}

	ZlibStreamResult ZlibStreamProgress::Classify(uint32_t a_totalInput) const noexcept
	{
		return !refilled && a_totalInput <= initialAvailable ?
			ZlibStreamResult::InputComplete : ZlibStreamResult::InputRefilled;
	}
}
