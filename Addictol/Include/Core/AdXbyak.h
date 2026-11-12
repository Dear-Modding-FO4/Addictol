#pragma once

#include <cstdint>

#include <xbyak/xbyak.h>

// xbyak includes <windows.h>, whose ERROR macro breaks REX::ERROR.
#ifdef ERROR
#	undef ERROR
#endif

namespace RELEX
{
	// Code cave base whose absolute jumps and calls clobber no register.
	class Stub :
		public Xbyak::CodeGenerator
	{
	public:
		using Xbyak::CodeGenerator::CodeGenerator;

	protected:
		void JumpTo(uintptr_t a_target)
		{
			jmp(ptr[rip]);
			dq(a_target);
		}

		void CallTo(uintptr_t a_target)
		{
			Xbyak::Label target;
			Xbyak::Label after;

			call(ptr[rip + target]);
			jmp(after);

			L(target);
			dq(a_target);

			L(after);
		}
	};
}
