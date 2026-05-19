#include <Modules/AdModuleUtilityShader.h>
#include <AdUtils.h>
#include <AdAssert.h>

#include <RE/B/BSGraphics.h>

#include <Windows.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesUtilityShader{ "Fixes"sv, "bUtilityShader"sv, true };

	namespace utilityShaderDetail
	{
		inline static std::uint64_t CreateVertexShaderFromBuffer(void* a_renderer, void* a_buffer, std::uint32_t a_size, std::uint32_t a_flags, std::uint64_t a_a5, std::uint64_t a_a6, std::uint64_t a_a7) noexcept
		{
			using func_t = std::uint64_t(*)(void*, void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t);
			static REL::Relocation<func_t> func{ REL::ID{ 1385342, 2276944 } };
			return func(a_renderer, a_buffer, a_size, a_flags, a_a5, a_a6, a_a7);
		}

		inline static std::uint64_t CreatePixelShaderFromBuffer(void* a_renderer, const void* a_buffer, std::uint32_t a_size, std::uint32_t a_flags, std::uint64_t a_a5, std::uint64_t a_a6) noexcept
		{
			using func_t = decltype(&CreatePixelShaderFromBuffer);
			static REL::Relocation<func_t> func{ REL::ID{ 798751, 2276950 } };
			return func(a_renderer, a_buffer, a_size, a_flags, a_a5, a_a6);
		}

		inline std::uint64_t CreateShaders()
		{
			auto Renderer = reinterpret_cast<void*>(REL::ID{ 1378294, 2704525 }.address());
			auto VertexBuffer = reinterpret_cast<void*>(REL::ID{ 571231, 2384579 }.address());
			auto PixelBuffer = reinterpret_cast<void*>(REL::ID{ 716718, 2384580 }.address());
			
			auto VertexShader = CreateVertexShaderFromBuffer(Renderer, VertexBuffer, 0x27C, 0, 0x0C0300000000FFF, 0, 0);
			REL::Relocation<std::uint64_t*> VertexShaderQWord{ REL::ID{ 67091, 2710692 } };
			*VertexShaderQWord = VertexShader;

			auto PixelShader = CreatePixelShaderFromBuffer(Renderer, PixelBuffer, 0x2B8, 0, 0, 0);
			REL::Relocation<std::uint64_t*> PixelShaderQWord{ REL::ID{ 286285, 2710693 } };
			*PixelShaderQWord = PixelShader;

			return PixelShader;
		}

		inline void CreateShadersOG()
		{
			REL::Relocation<void()> func{ REL::ID{ 527640 } };
			func();
		}

		void WritePatch(std::uintptr_t a_base, std::size_t a_first, std::size_t a_last, const Xbyak::CodeGenerator& a_code)
		{
			const std::size_t size = a_last - a_first;
			const auto dst = a_base + a_first;
			REL::WriteSafeFill(dst, REL::NOP, size);

			auto& trampoline = F4SE::GetTrampoline();
			AdAssert(size >= 6);
			trampoline.write_call<6>(dst, trampoline.allocate(a_code));
		}

		void PatchVertexShader(std::uintptr_t a_base)
		{
			struct Patch : Xbyak::CodeGenerator
			{
				Patch(std::uintptr_t a_data)
				{
					if (RELEX::IsRuntimeOG())
						mov(r13, a_data);
					else
					{
						test(r13, r13);
						jnz("skip");
						mov(r13, a_data);

						L("skip");
						mov(r11, reinterpret_cast<std::uintptr_t>(&::LeaveCriticalSection));
						jmp(r11);
					}

					ret();
				}
			};

			// Vertex Shader
			REL::Relocation<RE::BSGraphics::VertexShader**> vertexShader{ REL::ID{ 67091, 2710692 } };
			AdAssert(*vertexShader != nullptr);

			// Setup the Patch
			Patch patch{ reinterpret_cast<std::uintptr_t>(*vertexShader) };
			patch.ready();

			// Write the Patch
			if (RELEX::IsRuntimeOG())
				WritePatch(a_base, 0x150, 0x157, patch);
			else
				WritePatch(a_base, 0x146, 0x14C, patch);
		}

		void PatchPixelShader(std::uintptr_t a_base)
		{
			struct Patch : Xbyak::CodeGenerator
			{
				Patch(std::uintptr_t a_data)
				{
					if (RELEX::IsRuntimeOG())
						mov(rax, a_data);
					else
					{
						test(rax, rax);
						jnz("skip");
						mov(rax, a_data);

						L("skip");
						mov(r11, reinterpret_cast<std::uintptr_t>(&::LeaveCriticalSection));
						jmp(r11);
					}

					ret();
				}
			};

			// Pixel Shader
			REL::Relocation<RE::BSGraphics::PixelShader**> pixelShader{ REL::ID{ 286285, 2710693 } };
			AdAssert(*pixelShader != nullptr);

			// Setup the Patch
			Patch patch{ reinterpret_cast<std::uintptr_t>(*pixelShader) };
			patch.ready();

			// Write the Patch
			if (RELEX::IsRuntimeOG())
				WritePatch(a_base, 0x1A4, 0x1AB, patch);
			else
				WritePatch(a_base, 0x1B1, 0x1B7, patch);
		}
	}

	ModuleUtilityShader::ModuleUtilityShader() :
		Module("Utility Shader", &bFixesUtilityShader)
	{}

	bool ModuleUtilityShader::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleUtilityShader::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation<std::uintptr_t> Base{ REL::ID{ 768994, 2319078 } };

		if (RELEX::IsRuntimeOG())
			utilityShaderDetail::CreateShadersOG();
		else
			utilityShaderDetail::CreateShaders();

		utilityShaderDetail::PatchVertexShader(Base.address());
		utilityShaderDetail::PatchPixelShader(Base.address());

		return true;
	}

	bool ModuleUtilityShader::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleUtilityShader::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}