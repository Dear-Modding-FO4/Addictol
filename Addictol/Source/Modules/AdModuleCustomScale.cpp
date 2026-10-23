// Thanks to WirelessLan: https://github.com/WirelessLan/F4SEPlugins/tree/main/CustomScaleFix

#include <Modules/AdModuleCustomScale.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>

#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/B/BSFurnitureMarker.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/N/NiNode.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormUtil.h>

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

			void ModifyFirstPersonScale(RE::Actor* a_actor, float a_scale)
			{
				RE::NiAVObject* root = a_actor->Get3D(true);
				if (!root)
					return;

				root->local.scale = a_scale;
			}

			float GetActualScale(RE::TESObjectREFR* a_ref)
			{
				if (!a_ref)
					return 0.0f;

				float retScale = GetScale(a_ref);

				RE::NiAVObject* skeletonNode = a_ref->Get3D(false);
				if (!skeletonNode)
					return retScale;

				static const RE::BSFixedString comNodeStr("COM");
				RE::NiAVObject* comNode = skeletonNode->GetObjectByName(comNodeStr);
				if (!comNode)
					return retScale;

				retScale *= comNode->local.scale;

				RE::NiNode* nodePtr = comNode->parent;
				while (nodePtr && nodePtr != skeletonNode)
				{
					retScale *= nodePtr->local.scale;
					nodePtr = nodePtr->parent;
				}

				return retScale;
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

		namespace PlayerAnimEvents
		{
			struct ProcessEvent // Actor::ProcessEvent (BSAnimationGraphEvent)
			{
				static RE::BSEventNotifyControl thunk(RE::Actor* a_actor, const RE::BSAnimationGraphEvent& a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
				{
					RE::PlayerCharacter* playerCharacter = RE::PlayerCharacter::GetSingleton();
					if (playerCharacter)
					{
						if (a_event.tag == "CameraOverrideStart")
							Utils::ModifyFirstPersonScale(playerCharacter, 1.0f);
						else if (a_event.tag == "cameraOverrideStop")
							Utils::ModifyFirstPersonScale(playerCharacter, Utils::GetScale(playerCharacter));
					}

					return func(a_actor, a_event, a_source);
				}

				static inline REL::Relocation<decltype(thunk)> func;
			};
		}

		namespace FurniturePositions
		{
			struct FurnitureKeywordOffset
			{
				RE::BGSKeyword* keyword;
				float x;
				float y;
				float z;
			};

			std::vector<FurnitureKeywordOffset> furnitureOffsets;

			void LoadFurniturePositions()
			{
				auto dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler)
					return;

				// Clear
				furnitureOffsets.clear();

				// Fallout4.esm
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x30BB2, "Fallout4.esm"), 0.0f, 20.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x10013D, "Fallout4.esm"), 0.0f, 20.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x74EC7, "Fallout4.esm"), 0.0f, 12.0f, 55.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x14237C, "Fallout4.esm"), 0.0f, 12.0f, 55.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x139F9F, "Fallout4.esm"), 0.0f, 12.0f, 55.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x139F9E, "Fallout4.esm"), 0.0f, 8.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1B9AF6, "Fallout4.esm"), 0.0f, 12.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x306FD, "Fallout4.esm"), 0.0f, 20.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x140F74, "Fallout4.esm"), 0.0f, 30.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1F7A41, "Fallout4.esm"), 0.0f, 20.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0xBC262, "Fallout4.esm"), 0.0f, 0.0f, 45.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x178F31, "Fallout4.esm"), 0.0f, 0.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x5BC4E, "Fallout4.esm"), 0.0f, 20.0f, 78.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x10C3B7, "Fallout4.esm"), 0.0f, 0.0f, 60.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1865BA, "Fallout4.esm"), 25.0f, 25.0f, 60.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x14D04F, "Fallout4.esm"), 0.0f, 12.0f, 45.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1487BE, "Fallout4.esm"), 0.0f, 12.0f, 45.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x17B3A2, "Fallout4.esm"), -25.0f, 28.0f, 48.0f });

				// DLCworkshop02.esm
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0xAC8, "DLCworkshop02.esm"), 0.0f, 45.0f, 80.0f });

				// DLCworkshop03.esm
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1201, "DLCworkshop03.esm"), 0.0f, 0.0f, 85.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1A52, "DLCworkshop03.esm"), 0.0f, -22.0f, 32.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x58A3, "DLCworkshop03.esm"), 20.0f, 0.0f, 75.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1D51, "DLCworkshop03.esm"), -24.0f, -12.0f, 36.0f });
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x1A5C, "DLCworkshop03.esm"), 0.0f, 12.0f, 55.0f });

				// DLCNukaWorld.esm
				furnitureOffsets.push_back({ dataHandler->LookupForm<RE::BGSKeyword>(0x163FC, "DLCNukaWorld.esm"), 0.0f, 21.0f, 128.0f });
			}

			FurnitureKeywordOffset* GetFurnitureKeywordOffset(RE::TESObjectREFR* a_furn)
			{
				for (std::uint32_t i = 0; i < furnitureOffsets.size(); i++)
				{
					if (a_furn->HasKeyword(furnitureOffsets[i].keyword))
						return &furnitureOffsets[i];
				}

				return nullptr;
			}

			float GetCurrentFurnitureMarkerRotation(RE::TESObjectREFR* a_actorRef, RE::TESObjectREFR* a_furnRef)
			{
				RE::Actor* actor = a_actorRef ? a_actorRef->As<RE::Actor>() : nullptr;
				RE::TESFurniture* furnForm = a_furnRef ? a_furnRef->data.objectReference->As<RE::TESFurniture>() : nullptr;

				if (!actor || !furnForm)
					return 0.0f;

				if (!actor->currentProcess || !actor->currentProcess->middleHigh)
					return 0.0f;

				std::uint32_t currentMarkerId = actor->currentProcess->middleHigh->currentFurnitureMarkerID;
				if (furnForm->markersArray.size() == 0 || furnForm->markersArray.size() < currentMarkerId)
					return 0.0f;

				return furnForm->markersArray[currentMarkerId].heading;
			}

			RE::NiPoint3* GetModifiedFurniturePosition(RE::TESObjectREFR* a_actorRef, RE::TESObjectREFR* a_furnRef)
			{
				static RE::NiPoint3 furniturePos = {};

				if (!a_actorRef || !a_furnRef)
					return nullptr;

				float scale = Utils::GetActualScale(a_actorRef);
				std::uint16_t castedScale = static_cast<std::int16_t>(std::round(scale * 100));
				if (castedScale == 0 || castedScale == 100)
					return nullptr;

				FurnitureKeywordOffset* offset = GetFurnitureKeywordOffset(a_furnRef);
				if (!offset)
					return nullptr;

				float xOffset = offset->x * (1.0f - scale);
				float yOffset = offset->y * (1.0f - scale);
				float zOffset = offset->z * (1.0f - scale);

				float furnitureRot = a_furnRef->data.angle.z + GetCurrentFurnitureMarkerRotation(a_actorRef, a_furnRef);

				furniturePos = a_furnRef->GetPosition();
				furniturePos.x += xOffset * std::cos(furnitureRot) + yOffset * std::sin(furnitureRot);
				furniturePos.y += -xOffset * std::sin(furnitureRot) + yOffset * std::cos(furnitureRot);
				furniturePos.z += zOffset;

				return &furniturePos;
			}

			struct PlayFurnitureAnimationPatch : Xbyak::CodeGenerator // PlayFurnitureAnimation
			{
				PlayFurnitureAnimationPatch(std::uintptr_t a_targetAddr, std::uintptr_t a_funcAddr)
				{
					Xbyak::Label funcLabel;
					Xbyak::Label retnLabel;

					push(rcx);
					push(rdx);
					sub(rsp, 0x10);
					movdqu(ptr[rsp], xmm0);
					sub(rsp, 0x10);
					movdqu(ptr[rsp], xmm1);
					sub(rsp, 0x10);
					movdqu(ptr[rsp], xmm2);
					sub(rsp, 0x20);

					mov(rcx, rbx);
					mov(rdx, r14);
					call(ptr[rip + funcLabel]);

					add(rsp, 0x20);
					movdqu(xmm2, ptr[rsp]);
					add(rsp, 0x10);
					movdqu(xmm1, ptr[rsp]);
					add(rsp, 0x10);
					movdqu(xmm0, ptr[rsp]);
					add(rsp, 0x10);
					pop(rdx);
					pop(rcx);

					test(rax, rax);
					je("RETURN");

					if (RELEX::IsRuntimeOG())
					{
						movss(xmm0, ptr[rax]);
						movss(ptr[rbp - 0x70], xmm0);
						movss(xmm0, ptr[rax + 0x04]);
						movss(ptr[rbp - 0x6C], xmm0);
						movss(xmm0, ptr[rax + 0x08]);
						movss(ptr[rbp - 0x68], xmm0);
					}
					else
					{
						movss(xmm0, ptr[rax]);
						movss(ptr[rbp + 0x30], xmm0);
						movss(xmm0, ptr[rax + 0x04]);
						movss(ptr[rbp + 0x34], xmm0);
						movss(xmm0, ptr[rax + 0x08]);
						movss(ptr[rbp + 0x38], xmm0);
						movss(xmm0, ptr[rdi]);
					}
					jmp(ptr[rip + retnLabel]);

					L("RETURN");
					if (RELEX::IsRuntimeOG())
						movss(ptr[rbp - 0x68], xmm0);
					else
						movss(ptr[rbp + 0x34], xmm1);
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(a_funcAddr);

					L(retnLabel);
					dq(a_targetAddr + 0x05);
				}
			};

			struct InitializeChairBedQuickPositionPatch : Xbyak::CodeGenerator // Actor::InitializeChairBedQuickPosition
			{
				InitializeChairBedQuickPositionPatch(std::uintptr_t a_targetAddr, std::uintptr_t a_funcAddr)
				{
					Xbyak::Label funcLabel;
					Xbyak::Label retnLabel;

					push(rcx);
					push(rdx);
					sub(rsp, 0x20);

					mov(rcx, rsi);
					mov(rdx, rbx);
					call(ptr[rip + funcLabel]);

					add(rsp, 0x20);
					pop(rdx);
					pop(rcx);

					test(rax, rax);
					je("RETURN");

					if (RELEX::IsRuntimeOG())
					{
						movss(xmm0, ptr[rax]);
						movss(ptr[rbp - 0x50 + 0x30], xmm0);
						movss(xmm0, ptr[rax + 0x04]);
						movss(ptr[rbp - 0x50 + 0x34], xmm0);
						movss(xmm0, ptr[rax + 0x08]);
						movss(ptr[rbp - 0x50 + 0x38], xmm0);
					}
					else
					{
						movss(xmm0, ptr[rax]);
						movss(ptr[rbp - 0x60 + 0x30], xmm0);
						movss(xmm0, ptr[rax + 0x04]);
						movss(ptr[rbp - 0x60 + 0x34], xmm0);
						movss(xmm0, ptr[rax + 0x08]);
						movss(ptr[rbp - 0x60 + 0x38], xmm0);
					}

					L("RETURN");
					movss(xmm0, ptr[r14]);
					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(a_funcAddr);

					L(retnLabel);
					dq(a_targetAddr + 0x05);
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
		// Zoom Patch
		{
			auto target = REL::Relocation{ REL::ID{ 1344794, 2248270 }, REL::Offset{ 0x86, 0x84 } }.get();
			RELEX::XbyakJump<customScaleDetail::ZoomData::GetGeometryOffsetPatch>(target, target, (uintptr_t)&customScaleDetail::ZoomData::GetZoomData);

			// NG / AE Inline
			if (!RELEX::IsRuntimeOG())
			{
				target = REL::Relocation{ REL::ID{ 2248262 }, REL::Offset{ 0xB1 } }.get();
				RELEX::XbyakJump<customScaleDetail::ZoomData::GetTranslationPatch>(target, target, (uintptr_t)&customScaleDetail::ZoomData::GetZoomData);
			}
		}

		// Camera Override Scale Patch
		if (bAdditionalUseCameraOverrideScale.GetValue())
		{
			REL::Relocation vtable{ RE::PlayerCharacter::VTABLE[3] };
			customScaleDetail::PlayerAnimEvents::ProcessEvent::func = vtable.write_vfunc(1, customScaleDetail::PlayerAnimEvents::ProcessEvent::thunk);
		}

		// Custom Furniture Position Patches
		if (bAdditionalUseCustomFurniturePosition.GetValue())
		{
			// PlayFurnitureAnimation Patch
			auto target = REL::Relocation{ REL::ID{ 458731, 2232660 }, REL::Offset{ 0x1B6, 0x129 } }.get();
			RELEX::XbyakJump<customScaleDetail::FurniturePositions::PlayFurnitureAnimationPatch>(target, target, (uintptr_t)&customScaleDetail::FurniturePositions::GetModifiedFurniturePosition);

			// InitializeChairBedQuickPosition Patch
			target = REL::Relocation{ REL::ID{ 916591, 2229727 }, REL::Offset{ 0x147, 0x121 } }.get();
			RELEX::XbyakJump<customScaleDetail::FurniturePositions::InitializeChairBedQuickPositionPatch>(target, target, (uintptr_t)&customScaleDetail::FurniturePositions::GetModifiedFurniturePosition);
		}

		return true;
	}

	bool ModuleCustomScale::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kNewGame || a_msg->type == F4SE::MessagingInterface::kPreLoadGame))
		{
			if (bAdditionalUseCustomFurniturePosition.GetValue())
				customScaleDetail::FurniturePositions::LoadFurniturePositions();
		}

		return true;
	}
}
