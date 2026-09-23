#include "Harness.h"
#include <Zlib/AdZlibInstallation.h>
#include <Zlib/AdZlibBackendRegistry.h>

namespace vmm_tests
{
	void run_zlib_backend_checks(Runner& runner)
	{
		using namespace Addictol;
		runner.test("zlib registry resolves all rows and stale values", [] {
			for (const auto& row : ZLIB_BACKEND_NAMES)
			{
				require(ResolveZlibBackendSelection(row.name) == row.kind, "registry resolution");
				VisitSelectedZlibBackend([&]<class Backend> { require(Backend::kind == row.kind, "typed row mismatch"); });
			}
			for (auto name : { "libdeflate", "unknown", "" })
				require(ResolveZlibBackendSelection(name) == DEFAULT_ZLIB_BACKEND, "unknown value did not select default");
		});
		runner.test("zlib install validates every entry and anchor before committing", [] {
			std::vector<uint8_t> image(ZLIB_INSTALL_IMAGE_SIZE);
			for (const auto& entry : ZLIB_ENTRIES)
				std::copy(entry.prologue.begin(), entry.prologue.end(), image.begin() + entry.offset);
			using namespace ZlibInflate::Contract;
			for (auto [offset, bytes] : { std::pair{ size_t{0}, PROLOGUE }, { MODE_LOAD_OFFSET, MODE_LOAD },
				{ MODE_BOUNDS_OFFSET, MODE_BOUNDS }, { DONE_STORE_OFFSET, DONE_STORE },
				{ RESET_ZERO_OFFSET, RESET_ZERO }, { RESET_STORE_OFFSET, RESET_STORE } })
				std::copy(bytes.begin(), bytes.end(), image.begin() + offset);
			size_t commits{};
			require(!InstallValidatedZlib(image, [&] { ++commits; return true; }) && commits == 1, "valid image rejected");
			const auto reject = [&](size_t offset) {
				image[offset] ^= 1;
				require(InstallValidatedZlib(image, [&] { ++commits; return true; }) && commits == 1, "invalid image patched");
				image[offset] ^= 1;
			};
			for (const auto& entry : ZLIB_ENTRIES) reject(entry.offset);
			for (auto offset : { size_t{16}, MODE_LOAD_OFFSET, MODE_BOUNDS_OFFSET, DONE_STORE_OFFSET, RESET_ZERO_OFFSET, RESET_STORE_OFFSET })
				reject(offset);
		});
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
