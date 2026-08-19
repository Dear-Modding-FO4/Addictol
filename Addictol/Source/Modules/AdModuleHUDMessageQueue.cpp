#include <Modules/AdModuleHUDMessageQueue.h>
#include <AdUtils.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesHUDMessageQueue{ "Fixes"sv, "bHUDMessageQueue"sv, true };

	namespace hudMessageQueueDetail
	{
		struct PrimaryPatch : Xbyak::CodeGenerator
		{
			explicit PrimaryPatch(std::uintptr_t a_resumeAddress, std::uintptr_t a_epilogueAddress)
			{
				// Check for Nullptr
				test(rcx, rcx);
				jz("nullptr");

				// Valid Path
				mov(rdi, ptr[rcx + 0x0C0]);
				jmp(ptr[rip]);
				dq(a_resumeAddress);

				// Nullptr Path
				L("nullptr");
				xor_(eax, eax);
				mov(ptr[rbx + 0x08], rbx); // Next
				mov(ptr[rbx + 0x10], rax); // Tail
				mov(ptr[rbx + 0x20], rax); // End
				mov(ptr[rbx + 0x28], rax); // Begin
				cmp(dword[rbx + 0x30], 0); // Size
				jz("epilogue");
				mov(dword[rbx + 0x30], 1);
				
				// Function Epilogue
				L("epilogue");
				jmp(ptr[rip]);
				dq(a_epilogueAddress);
			}
		};

		struct SecondaryPatch : Xbyak::CodeGenerator
		{
			explicit SecondaryPatch(std::uintptr_t a_resumeAddress)
			{
				// Check for Nullptrs
				test(rcx, rcx);
				jz("nullptr");
				test(rdx, rdx);
				jz("nullptr");

				// Validate Pointers
				cmp(rcx, 0x10000);
				jb("nullptr");
				cmp(rdx, 0x10000);
				jb("nullptr");

				// Valid Path
				mov(qword[rsp + 0x8], rbx);
				jmp(ptr[rip]);
				dq(a_resumeAddress);

				// Nullptr Path
				L("nullptr");
				xor_(eax, eax);
				ret();
			}
		};

		struct ValidateRangePatch : Xbyak::CodeGenerator
		{
			explicit ValidateRangePatch(std::uintptr_t a_resumeAddress)
			{
				// Restore Overwritten Instruction
				mov(r14, qword[rdi + 0x128]);

				// Head of the Arena
				mov(rax, qword[rdi + 0x100]);
				test(rax, rax);
				jz("resume");

				// Validate Range
				cmp(r14, rax);
				jb("adjust");
				lea(rcx, qword[rax + 0xC0]); // 8 elements of size 0x18
				cmp(r14, rcx);
				jb("resume");

				// Keep in Range
				L("adjust");
				mov(r14, rax);
				mov(qword[rdi + 0x128], rax);

				// Valid Path
				L("resume");
				jmp(ptr[rip]);
				dq(a_resumeAddress);
			}
		};
	}

	ModuleHUDMessageQueue::ModuleHUDMessageQueue() :
		Module("HUD Message Queue", &bFixesHUDMessageQueue)
	{}

	bool ModuleHUDMessageQueue::DoQuery() const noexcept
	{
		return !RELEX::IsRuntimeOG();
	}

	bool ModuleHUDMessageQueue::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Primary Patch
		const auto primaryAddress = REL::ID{ 2220806 }.address();
		REL::WriteSafeFill(primaryAddress + 0x10, REL::NOP, 0x7);
		RELEX::XbyakJump<hudMessageQueueDetail::PrimaryPatch>(primaryAddress + 0x10, primaryAddress + 0x17, primaryAddress + 0x66);

		// Secondary Patch
		const auto secondaryAddress = REL::ID{ 2220807 }.address();
		REL::WriteSafeFill(secondaryAddress, REL::NOP, 0x5);
		RELEX::XbyakJump<hudMessageQueueDetail::SecondaryPatch>(secondaryAddress, secondaryAddress + 0x5);

		// Validate Range Patch
		const auto validateRangeAddress = REL::ID{ 2220746 }.address();
		REL::WriteSafeFill(validateRangeAddress + 0x3F0, REL::NOP, 0x7);
		RELEX::XbyakJump<hudMessageQueueDetail::ValidateRangePatch>(validateRangeAddress + 0x3F0, validateRangeAddress + 0x3F7);

		return true;
	}

}
