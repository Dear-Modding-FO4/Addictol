#include <Modules/AdModuleLibDeflate.h>
#include <AdUtils.h>
#include <AdAssert.h>
#include <AdProfilerBA2.h>
#include <libdeflate/libdeflate.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLibDeflate{ "Patches"sv, "bLibDeflate"sv, true };

	namespace zlibDetail
	{
		constexpr static int32_t Z_OK				= 0;
		constexpr static int32_t Z_STREAM_END		= 1;
		constexpr static int32_t Z_NEED_DICT		= 2;
		constexpr static int32_t Z_ERRNO			= -1;
		constexpr static int32_t Z_STREAM_ERROR		= -2;
		constexpr static int32_t Z_DATA_ERROR		= -3;
		constexpr static int32_t Z_MEM_ERROR		= -4;
		constexpr static int32_t Z_BUF_ERROR		= -5;
		constexpr static int32_t Z_VERSION_ERROR	= -6;

		typedef struct z_stream_s
		{
			uint8_t* next_in;
			uint32_t avail_in;
			uint32_t total_in;
			uint8_t* next_out;
			uint32_t avail_out;
			uint32_t total_out;
			const char* msg;
			struct internal_state* state;
		} z_stream, * z_streamp;

		using TInflate = int32_t(*)(z_streamp, int32_t) noexcept;
		TInflate Inflate;

		namespace Decompression
		{
			struct LibDeflate
			{
				// libdeflate no supported streaming, so...
				// In the case when libdeflate failed to work and ended with an error, 
				// we use the orginal function, as the results show, even if we ignore the registration of the stream as streaming, 
				// the total number of calls in 1k microseconds is half less when loading the save, unlike the original function, 
				// registering the stream as streaming is guaranteed to skip constant libdeflate attempts and it will reduce the time even more.
				// It is assumed that Fallout 4 does not often use block reads, or the size of the block itself is enough for one iteration.
				static int32_t Inflate(z_streamp a_stream, [[maybe_unused]] int32_t a_flush) noexcept
				{
					if (!a_stream) return Z_STREAM_ERROR;

					thread_local static bool streaming = false;

					// If the stream is registered as streaming, we call the original function....
					if (streaming)
					{
						auto ret = zlibDetail::Inflate(a_stream, a_flush);
						// If this was the last iteration, we are unregistering as streaming.
						if (ret == Z_STREAM_END) streaming = false;
						return ret;
					}

					// Just once is enough for the thread (without free)
					thread_local libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
					if (!decompressor) return Z_MEM_ERROR;

					const bool profiling = ProfilerCore::GetSingleton()->IsActive() && ProfilerCore::IsBA2TimingEnabled();
					std::chrono::high_resolution_clock::time_point profStart;
					if (profiling)
						profStart = std::chrono::high_resolution_clock::now();

					size_t inBytes = 0, outBytes = 0;
					libdeflate_result result = libdeflate_zlib_decompress_ex(decompressor, a_stream->next_in, a_stream->avail_in,
						a_stream->next_out, a_stream->avail_out, &inBytes, &outBytes);

					if (result == LIBDEFLATE_SUCCESS)
					{
						if (profiling)
						{
							auto profEnd = std::chrono::high_resolution_clock::now();
							double elapsedMs = std::chrono::duration<double, std::milli>(profEnd - profStart).count();
							ProfilerBA2::GetSingleton()->RecordDecompression(inBytes, outBytes, elapsedMs);
						}

						AdAssert(outBytes < std::numeric_limits<uint32_t>::max());

						a_stream->next_in += (uint32_t)inBytes;
						a_stream->next_out += (uint32_t)outBytes;
						a_stream->avail_in = 0;
						a_stream->avail_out = 0;
						a_stream->total_in = (uint32_t)inBytes;
						a_stream->total_out = (uint32_t)outBytes;

						return Z_STREAM_END;
					}
					else
					{
						auto ret = zlibDetail::Inflate(a_stream, a_flush);
						// We register the stream only when there is more data.
						if (ret == Z_OK) streaming = true;
						return ret;
					}
				}
			};		
		}
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("LibDeflate", &bPatchesLibDeflate)
	{}

	bool ModuleLibDeflate::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg) return false;
		using namespace zlibDetail;
		auto target = REL::ID{ 224011, 2168026 }.address();
		if (!RELEX::Validate(target, { 0x89, 0x54, 0x24, 0x10 }))
			return false;
		*(uintptr_t*)(&Inflate) = RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&Decompression::LibDeflate::Inflate));
		return Inflate != nullptr;
	}

	bool ModuleLibDeflate::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}