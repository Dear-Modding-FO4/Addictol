#include <Modules\AdModuleImageSpaceAdapterWarning.h>
#include <AdUtils.h>

#include <RE/T/TESImageSpaceModifier.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bWarningsImageSpaceAdapter{ "Warnings", "bImageSpaceAdapter", true };

	namespace detail
	{
		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t a_dst)
			{
				Xbyak::Label dst;

				mov(r8, RELEX::IsRuntimeOG() ? r14 : r15);
				jmp(ptr[rip + dst]);

				L(dst);
				dq(a_dst);
			}
		};

		using func_t = RE::NiFloatInterpolator* (RE::TESFile*, std::uint32_t&, bool, float);
		REL::Relocation<func_t> _original;

		RE::NiFloatInterpolator* LoadChunk(RE::TESFile* a_file, std::uint32_t& a_size, RE::TESImageSpaceModifier* a_imad, float a_default)
		{
			if (a_size == 0)
			{
				auto ichunk = a_file->GetTESChunk();
				const auto cchunk = reinterpret_cast<char*>(std::addressof(ichunk));
				if (cchunk[0] < '@')
				{
					cchunk[0] += 'a';
				}
				const std::string_view schunk{ cchunk, 4 };
				REX::FAIL("IMAD with ID: [0x{:08X}] from \"{}\" with subrecord \"{}\" has an invalid key size of zero. "
					"This will result in memory corruption. "
					"Please open the form in xEdit and correct it or remove the mod from your load order."sv,
					a_imad->GetFormID(), a_file->GetFilename(), schunk);

				return nullptr;
			}
			else
			{
				return _original(a_file, a_size, a_imad->data.animatable, a_default);
			}
		}
	}

	ModuleImageSpaceAdapterWarning::ModuleImageSpaceAdapterWarning() :
		Module("Module Image Space Adapter Warning", &bWarningsImageSpaceAdapter)
	{}

	bool ModuleImageSpaceAdapterWarning::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleImageSpaceAdapterWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();
		REL::Relocation<std::uintptr_t> target{ REL::ID{ 231868, 2199987 }, REL::Offset{ 0x57F, 0x573 } };
		detail::Patch p{ reinterpret_cast<std::uintptr_t>(&detail::LoadChunk) };
		p.ready();
		detail::_original = trampoline.write_call<5>(target.address(), trampoline.allocate(p));

		return true;
	}

	bool ModuleImageSpaceAdapterWarning::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleImageSpaceAdapterWarning::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}