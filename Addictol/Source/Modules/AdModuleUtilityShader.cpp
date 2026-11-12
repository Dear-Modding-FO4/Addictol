#include <Modules/AdModuleUtilityShader.h>
#include <Core/AdUtils.h>
#include <Core/AdAssert.h>

#include <RE/B/BSGraphics.h>

#include <Windows.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	namespace utilityShaderDetail
	{
		inline static std::uint64_t CreateVertexShaderFromBuffer(void* a_renderer, void* a_buffer, std::uint32_t a_size, std::uint32_t a_flags, std::uint64_t a_a5, std::uint64_t a_a6, std::uint64_t a_a7) noexcept
		{
			using func_t = std::uint64_t(*)(void*, void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t);
			static REL::Relocation<func_t> func{ RE::ID::BSGraphics::Renderer::CreateVertexShaderFromBuffer };
			return func(a_renderer, a_buffer, a_size, a_flags, a_a5, a_a6, a_a7);
		}

		inline static std::uint64_t CreatePixelShaderFromBuffer(void* a_renderer, const void* a_buffer, std::uint32_t a_size, std::uint32_t a_flags, std::uint64_t a_a5, std::uint64_t a_a6) noexcept
		{
			using func_t = decltype(&CreatePixelShaderFromBuffer);
			static REL::Relocation<func_t> func{ RE::ID::BSGraphics::Renderer::CreatePixelShaderFromBuffer };
			return func(a_renderer, a_buffer, a_size, a_flags, a_a5, a_a6);
		}

		inline std::uint64_t CreateShaders()
		{
			auto Renderer = reinterpret_cast<void*>(RE::ID::BSGraphics::Renderer::Singleton.address());
			auto VertexBuffer = reinterpret_cast<void*>(RE::ID::MoviePlayer::VertexShaderData.address());
			auto PixelBuffer = reinterpret_cast<void*>(RE::ID::MoviePlayer::PixelShaderData.address());
			
			auto VertexShader = CreateVertexShaderFromBuffer(Renderer, VertexBuffer, 0x27C, 0, 0x0C0300000000FFF, 0, 0);
			REL::Relocation<std::uint64_t*> VertexShaderQWord{ RE::ID::MoviePlayer::VertexShader };
			*VertexShaderQWord = VertexShader;

			auto PixelShader = CreatePixelShaderFromBuffer(Renderer, PixelBuffer, 0x2B8, 0, 0, 0);
			REL::Relocation<std::uint64_t*> PixelShaderQWord{ RE::ID::MoviePlayer::PixelShader };
			*PixelShaderQWord = PixelShader;

			return PixelShader;
		}

		inline void CreateShadersOG()
		{
			REL::Relocation<void()> func{ RE::ID::MoviePlayer::CreateShaders };
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
			REL::Relocation<RE::BSGraphics::VertexShader**> vertexShader{ RE::ID::MoviePlayer::VertexShader };
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
					{
						mov(rax, a_data);
						ret();
					}
					else
					{
						// The caller already spilled the result to [rsp+38h], past our return.
						cmp(qword[rsp + 0x40], 0);
						jnz("skip");
						mov(rax, a_data);
						mov(qword[rsp + 0x40], rax);

						L("skip");
						mov(r11, reinterpret_cast<std::uintptr_t>(&::LeaveCriticalSection));
						jmp(r11);
					}
				}
			};

			// Pixel Shader
			REL::Relocation<RE::BSGraphics::PixelShader**> pixelShader{ RE::ID::MoviePlayer::PixelShader };
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
		const auto base = REL::Relocation{ RE::ID::BSUtilityShader::CreateCommandBuffer }.address();

		const bool vanilla = RELEX::IsRuntimeOG() ?
			// mov r13, [rbp+22F0h]
			RELEX::Validate(base + 0x150, { 0x4C, 0x8B, 0xAD, 0xF0, 0x22, 0x00, 0x00 }) &&
				// mov rax, [rbp+22F0h]; mov [rsp+60h], rax
				RELEX::Validate(base + 0x1A4, { 0x48, 0x8B, 0x85, 0xF0, 0x22, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x60 }) :
			// cmovnz r13, rax; lea rcx, [r14+20h]; call [LeaveCriticalSection]
			RELEX::Validate(base + 0x13E, { 0x4C, 0x0F, 0x45, 0xE8, 0x49, 0x8D, 0x4E, 0x20, 0xFF, 0x15 }) &&
				// cmovnz rdx, rax; mov [rsp+38h], rdx; lea rcx, [r14+48h]; call [iat]
				RELEX::Validate(base + 0x1A4, { 0x48, 0x0F, 0x45, 0xD0, 0x48, 0x89, 0x54, 0x24, 0x38, 0x49, 0x8D, 0x4E, 0x48, 0xFF, 0x15 });

		if (!vanilla)
		{
			Skip("BSUtilityShader::CreateCommandBuffer does not match the expected shader lookups"sv);
			return false;
		}

		return true;
	}

	bool ModuleUtilityShader::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REL::Relocation Base{ RE::ID::BSUtilityShader::CreateCommandBuffer };

		if (RELEX::IsRuntimeOG())
			utilityShaderDetail::CreateShadersOG();
		else
			utilityShaderDetail::CreateShaders();

		utilityShaderDetail::PatchVertexShader(Base.address());
		utilityShaderDetail::PatchPixelShader(Base.address());

		return true;
	}
}