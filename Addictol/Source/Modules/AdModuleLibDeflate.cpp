#include <Modules\AdModuleLibDeflate.h>
#include <AdUtils.h>
#include <AdAssert.h>
#include <libdeflate\libdeflate.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLibDeflate{ "Patches", "bLibDeflate", true };

	struct z_stream_s
	{
		const void* next_in;
		uint32_t avail_in;
		uint32_t total_in;
		void* next_out;
		uint32_t avail_out;
		uint32_t total_out;
		const char* msg;
		struct internal_state* state;
	};

	static int __stdcall HKInflateInit(z_stream_s* stream, const char* version, int mode)
	{
		// Force inflateEnd to error out and skip frees
		stream->state = nullptr;

		return 0;
	}

	static int __stdcall HKInflate(z_stream_s* stream, int flush)
	{
		size_t outBytes = 0;
		libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();

		libdeflate_result result = libdeflate_zlib_decompress(decompressor, stream->next_in, stream->avail_in,
			stream->next_out, stream->avail_out, &outBytes);
		libdeflate_free_decompressor(decompressor);

		if (result == LIBDEFLATE_SUCCESS)
		{
			AdAssert(outBytes < std::numeric_limits<uint32_t>::max());

			stream->total_in = stream->avail_in;
			stream->total_out = (uint32_t)outBytes;

			return 1;
		}

		if (result == LIBDEFLATE_INSUFFICIENT_SPACE)
			return -5;

		return -2;
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("Module LibDeflate", &bPatchesLibDeflate)
	{}

	bool ModuleLibDeflate::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE

			auto Target = REL::ID(2192561).address() + 0x1B2;
			RELEX::DetourCall(Target, (uintptr_t)&HKInflateInit);
			RELEX::DetourCall(Target + 0x32, (uintptr_t)&HKInflate);
		}
		else
		{
			// OG

			auto Target = REL::ID(116758).address() + 0x17D;
			RELEX::DetourCall(Target, (uintptr_t)&HKInflateInit);
			RELEX::DetourCall(Target + 0x32, (uintptr_t)&HKInflate);
		}

		return true;
	}

	bool ModuleLibDeflate::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}