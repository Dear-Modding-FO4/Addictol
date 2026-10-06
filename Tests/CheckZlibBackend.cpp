#include "Harness.h"
#include <Zlib/AdZlibInstallation.h>
#include <Zlib/AdZlibBackendRegistry.h>

namespace vmm_tests
{
	void run_zlib_backend_checks(Runner& runner)
	{
		using namespace Addictol;
		runner.test("zlib routing preserves foreign streams and rejects invalid owned tags", [] {
			using Owned = OwnedInflate<NoWholeInflateDecoder, ZlibDecoder>;
			uint64_t foreign{};
			ZlibInflate::Stream stream{};
			stream.state = &foreign;
			size_t originals{}, owned{};
			const auto route = [&] {
				return RouteZlibStream<Owned>(&stream, [&] { ++originals; return 123; }, [&] { ++owned; return 456; });
			};
			require(route() == 123 && originals == 1 && owned == 0, "foreign state entered owned decoder");
			require(Owned::Init(&stream) == INFLATE_OK, "preseeded init rejected");
			require(route() == 456 && originals == 1 && owned == 1, "owned state reached vanilla");
			auto copied = stream;
			require(RouteZlibStream<Owned>(&copied, [&] { ++originals; return 123; }, [] { return 456; }) == INFLATE_STREAM_ERROR &&
				originals == 1, "invalid tagged state reached vanilla");
			require(Owned::End(&stream) == INFLATE_OK, "owned end");
		});
	}
}
