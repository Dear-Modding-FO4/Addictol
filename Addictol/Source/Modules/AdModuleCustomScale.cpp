// Thanks to WirelessLan: https://github.com/WirelessLan/F4SEPlugins/tree/main/CustomScaleFix

#include <Modules/AdModuleCustomScale.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>

#include <RE/B/BGSZoomData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectWEAP.h>

namespace Addictol
{
	namespace customScaleDetail
	{
		namespace Utils
		{
			float GetScale(RE::TESObjectREFR* a_reference) noexcept
			{
				using func_t = decltype(&Utils::GetScale);
				static REL::Relocation<func_t> func{ REL::ID{ 911188, 2200892 } };
				return func(a_reference);
			}
		}

		namespace ZoomData
		{
			RE::NiPoint3* GetZoomData(RE::TESObjectWEAP::InstanceData* a_instanceData)
			{
				static RE::NiPoint3 cameraOffset = {};
				RE::PlayerCharacter* playerCharacter = RE::PlayerCharacter::GetSingleton();

				if (!a_instanceData->zoomData || !playerCharacter)
					return nullptr;

				cameraOffset = a_instanceData->zoomData->zoomData.cameraOffset;
				cameraOffset *= Utils::GetScale(playerCharacter);

				return &cameraOffset;
			}

			struct GetGeometryOffsetPatch : Xbyak::CodeGenerator // FirstPersonState::GetGeometryOffset
			{
				GetGeometryOffsetPatch(std::uintptr_t a_targetAddr, std::uintptr_t a_funcAddr)
				{
					Xbyak::Label funcLabel;
					Xbyak::Label jmpLabel;
					Xbyak::Label retnLabel;

					push(rbx);
					push(rcx);
					push(rdx);
					push(rdi);
					sub(rsp, 0x20);
					movdqu(ptr[rsp], xmm6);

					call(ptr[rip + funcLabel]);

					movdqu(xmm6, ptr[rsp]);
					add(rsp, 0x20);
					pop(rdi);
					pop(rdx);
					pop(rcx);
					pop(rbx);

					test(rax, rax);
					je("RETURN");

					if (RELEX::IsRuntimeOG())
					{
						movss(xmm4, ptr[rax + 0x4]);
						movss(xmm5, ptr[rax + 0x8]);
						movaps(ptr[rsp + 0x50], xmm6);
						movaps(xmm1, xmm5);
						movaps(xmm2, xmm4);
						movss(xmm6, ptr[rax]);
					}
					else
					{
						movss(xmm2, ptr[rax + 0x4]);
						movss(xmm4, ptr[rax + 0x8]);
						movaps(xmm5, xmm2);
						mulss(xmm5, ptr[rdx + 0x80]);
						movaps(xmm1, xmm4);
						mulss(xmm1, dword[rdx + 0x90]);
						movaps(ptr[rsp + 0x50], xmm6);
						movss(xmm6, ptr[rax]);
					}
					jmp(ptr[rip + jmpLabel]);

					L("RETURN");
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(a_funcAddr);

					L(retnLabel);
					dq(a_targetAddr + 0x13);

					L(jmpLabel);
					dq(a_targetAddr + (RELEX::IsRuntimeOG() ? 0x33 : 0x43));
				}
			};

			struct GetTranslationPatch : Xbyak::CodeGenerator // FirstPersonState::GetTranslation
			{
				GetTranslationPatch(std::uintptr_t a_targetAddr, std::uintptr_t a_funcAddr)
				{
					Xbyak::Label funcLabel;
					Xbyak::Label jmpLabel;
					Xbyak::Label retnLabel;

					push(rbx);
					push(rcx);
					push(rdx);
					push(rdi);
					sub(rsp, 0x20);
					movdqu(ptr[rsp], xmm6);

					call(ptr[rip + funcLabel]);

					movdqu(xmm6, ptr[rsp]);
					add(rsp, 0x20);
					pop(rdi);
					pop(rdx);
					pop(rcx);
					pop(rbx);

					test(rax, rax);
					je("RETURN");

					movss(xmm6, ptr[rax]);
					movss(xmm2, ptr[rax + 0x4]);
					movaps(xmm0, xmm6);
					mulss(xmm0, ptr[rdx + 0x70]);
					movaps(xmm3, xmm6);
					mulss(xmm6, ptr[rdx + 0x78]);
					movaps(xmm5, xmm2);
					movss(xmm4, ptr[rax + 0x8]);
					jmp(ptr[rip + jmpLabel]);

					L("RETURN");
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(a_funcAddr);

					L(retnLabel);
					dq(a_targetAddr + 0x13);

					L(jmpLabel);
					dq(a_targetAddr + 0x3B);
				}
			};
		}
	}

	ModuleCustomScale::ModuleCustomScale() :
		Module("Custom Scale", &bFixesCustomScale, { F4SE::MessagingInterface::kNewGame, F4SE::MessagingInterface::kPreLoadGame })
	{}

	bool ModuleCustomScale::DoQuery() const noexcept
	{
		if (IsModDLLPresent("CustomScaleFix.dll"))
		{
			Skip("standalone 'CustomScaleFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleCustomScale::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Zoom Patch (GetGeometryOffset)
		auto target = REL::Relocation{ REL::ID{ 1344794, 2248270 }, REL::Offset{ 0x86, 0x84 } }.get();
		RELEX::XbyakJump<customScaleDetail::ZoomData::GetGeometryOffsetPatch>(target, target, (uintptr_t)&customScaleDetail::ZoomData::GetZoomData);

		// AE Inline (GetTranslationPatch)
		if (!RELEX::IsRuntimeOG())
		{
			target = REL::Relocation{ REL::ID{ 2248262 }, REL::Offset{ 0xB1 } }.get();
			RELEX::XbyakJump<customScaleDetail::ZoomData::GetTranslationPatch>(target, target, (uintptr_t)&customScaleDetail::ZoomData::GetZoomData);
		}

		if (bAdditionalUseCameraOverrideScale.GetValue())
		{
			//PlayerAnimEvents::Hooks_PlayerAnimEvents();
		}

		if (bAdditionalUseCustomFurniturePosition.GetValue())
		{
			// FurniturePositions::Hooks_PlayFurnitureAnimation();
			// FurniturePositions::Hooks_InitializeChairBedQuickPosition();
		}

		return true;
	}

	bool ModuleCustomScale::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kNewGame || a_msg->type == F4SE::MessagingInterface::kPreLoadGame))
		{
			if (bAdditionalUseCustomFurniturePosition.GetValue())
			{
				// if (FurniturePositions::ShouldLoadFurnitureConfig)
				// 	FurniturePositions::LoadFurnitureConfig
			}
		}

		return true;
	}
}
