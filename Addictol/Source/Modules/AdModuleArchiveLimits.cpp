#include <Modules/AdModuleArchiveLimits.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>
#include <unordered_dense/unordered_dense.h>

#undef MEM_RELEASE
#undef ERROR
#undef MAX_PATH

#include <RE/B/BSTEvent.h>
#include <RE/B/BSResource_Archive2_Index.h>
#include <RE/N/NiTexture.h>

// Thanks WirelessLan for idea: https://github.com/WirelessLan/BSALimitExpander

namespace Addictol
{
	// Off by default: 1.11.240 reworked the archive index layout these patches hardcode.

	constexpr static auto MAX_SIZE = 64ul * 1024ul;

	enum class ArchiveMode : uint8_t
	{
		kOldArchitecture = 0,
		k1kAvailableArchitecture
	};

	// Mode for AE 
	ArchiveMode s_ArchiveArchitecture = ArchiveMode::kOldArchitecture;

	namespace BSGraphics
	{
		struct Texture
		{
			struct Data
			{
				uint32_t refCount;			// 00
				uint8_t dataFileIndex;		// 04
				uint8_t chunkCount;			// 05
				uint8_t chunkOffsetOrType;	// 06
				uint8_t dataFileHighIndex;	// 07
			};

			uint64_t unk00;
			uint64_t unk08;
			uint64_t unk10;
			Data* data;
		};

		struct TextureEx
		{
			struct Data
			{
				uint32_t refCount;			// 00
				uint16_t dataFileIndex;		// 04
				uint8_t chunkCount;			// 06
				uint8_t chunkOffsetOrType;	// 07
			};

			uint64_t unk00;
			uint64_t unk08;
			uint64_t unk10;
			Data* data;
		};
	}

	struct Index
	{
		RE::BSTSmallIndexScatterTable<RE::BSResource::ID, RE::BSResource::Archive2::Index::NameIDAccess>	nameTable;
		RE::BSTSmartPointer<RE::BSResource::Stream>															dataFiles[MAX_SIZE];
		RE::BSTSmartPointer<RE::BSResource::AsyncStream>													asyncDataFiles[MAX_SIZE];
		RE::BSResource::ID																					dataFileNameIDs[MAX_SIZE];
	};

	static std::unique_ptr<Index> g_managerArchiveManager;

	struct IDHash
	{
		[[nodiscard]] std::size_t operator()(const RE::BSResource::ID& a_id) const noexcept
		{
			union hash_t
			{
				uint32_t v[2];
				uint64_t hash;
			};

			hash_t hash{};
			hash.v[0] = a_id.file ^ a_id.ext;
			hash.v[1] = a_id.dir ^ a_id.ext;
			return hash.hash;
		}
	};

	using Storage = ankerl::unordered_dense::map<RE::BSResource::ID, uint16_t, IDHash>;

	enum class StorageType : uint8_t
	{
		kGeneral = 0,
		kTextures,
		kTotal
	};

	Storage Storages[std::to_underlying(StorageType::kTotal)];
	RE::BSReadWriteLock StorageLocks[std::to_underlying(StorageType::kTotal)];

	static void PushArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx, StorageType archType) noexcept
	{
		RE::BSAutoWriteLock lock(StorageLocks[std::to_underlying(archType)]);
		Storages[std::to_underlying(archType)][id] = static_cast<uint16_t>(archIdx);
	}

	inline static void PushGeneralArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx) noexcept
	{
		PushArchiveIndex(id, archIdx, StorageType::kGeneral);
	}

	inline static void PushTexturesArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx) noexcept
	{
		PushArchiveIndex(id, archIdx, StorageType::kTextures);
	}

	[[nodiscard]] static uint16_t FindArchiveIndex(const RE::BSResource::ID& id, StorageType archType) noexcept
	{
		RE::BSAutoReadLock lock(StorageLocks[std::to_underlying(archType)]);

		auto it = Storages[std::to_underlying(archType)].find(id);
		return (it == Storages[std::to_underlying(archType)].end()) ?
			static_cast<uint16_t>(-1) : it->second;
	}

	[[nodiscard]] inline static uint16_t FindGeneralArchiveIndex(const RE::BSResource::ID& id) noexcept
	{
		return FindArchiveIndex(id, StorageType::kGeneral);
	}

	[[nodiscard]] inline static uint16_t FindTexturesArchiveIndex(const RE::BSResource::ID& id) noexcept
	{
		return FindArchiveIndex(id, StorageType::kTextures);
	}

	namespace Details
	{
		using Stream__Assign = void (*)(RE::BSTSmartPointer<RE::BSResource::Stream>&, RE::BSTSmartPointer<RE::BSResource::Stream>&);
		inline static Stream__Assign Stream__Assign_Orig{ nullptr };

		using BSTSmallIndexScatterTableUtil__NewTable_256 = RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index256::NameIDAccess>::entry_type* (*)(uint32_t);
		inline static BSTSmallIndexScatterTableUtil__NewTable_256 BSTSmallIndexScatterTableUtil__NewTable_256_Orig{ nullptr };

		using BSTSmallIndexScatterTableTraits__Insert_256 = bool (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index256::NameIDAccess>&, uint32_t, RE::BSResource::ID*&);
		inline static BSTSmallIndexScatterTableTraits__Insert_256 BSTSmallIndexScatterTableTraits__Insert_256_Orig{ nullptr };

		using BSTSmallIndexScatterTableTraits__Resize_256 = void (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index256::NameIDAccess>&, RE::BSResource::ID*&);
		inline static BSTSmallIndexScatterTableTraits__Resize_256 BSTSmallIndexScatterTableTraits__Resize_256_Orig{ nullptr };

		inline static RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index256::NameIDAccess>::entry_type* EndTable_256{ nullptr };

		using BSTSmallIndexScatterTableUtil__NewTable_1024 = RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index1024::NameIDAccess>::entry_type* (*)(uint32_t);
		inline static BSTSmallIndexScatterTableUtil__NewTable_1024 BSTSmallIndexScatterTableUtil__NewTable_1024_Orig{ nullptr };

		using BSTSmallIndexScatterTableTraits__Insert_1024 = bool (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index1024::NameIDAccess>&, uint32_t, RE::BSResource::ID*&);
		inline static BSTSmallIndexScatterTableTraits__Insert_1024 BSTSmallIndexScatterTableTraits__Insert_1024_Orig{ nullptr };

		using BSTSmallIndexScatterTableTraits__Resize_1024 = void (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
			RE::BSResource::Archive2::Index1024::NameIDAccess>&, RE::BSResource::ID*&);
		inline static BSTSmallIndexScatterTableTraits__Resize_1024 BSTSmallIndexScatterTableTraits__Resize_1024_Orig{ nullptr };

		inline static RE::BSTSmallIndexScatterTable<RE::BSResource::ID, RE::BSResource::Archive2::Index1024::NameIDAccess>::entry_type* EndTable_1024{ nullptr };
	}

	namespace Archive2
	{
		template<typename T>
		static void AddDataFile(T& self, RE::BSTSmartPointer<RE::BSResource::Stream>& stream,
			const RE::BSResource::ID& id, uint32_t index) noexcept
		{
			Details::Stream__Assign_Orig(g_managerArchiveManager->dataFiles[index], stream);
			stream->DoCreateAsync(g_managerArchiveManager->asyncDataFiles[index]);

			if (self.dataFileCount != index)
				return;

			// REX::INFO("DBG: {} {}", self.dataFileCount, index);

			g_managerArchiveManager->dataFileNameIDs[index] = id;
			auto* p_id = g_managerArchiveManager->dataFileNameIDs;

			if constexpr (std::is_same_v<T, RE::BSResource::Archive2::Index256>)
			{
				if (self.nameTable.table == Details::EndTable_256)
				{
					// constant initialization check that XCell/CKPE is cut out
					constexpr static auto MEMORY_INITIAZE_FLAG = 2;

					self.nameTable.avail = MEMORY_INITIAZE_FLAG;
					self.nameTable.table = Details::BSTSmallIndexScatterTableUtil__NewTable_256_Orig(MEMORY_INITIAZE_FLAG);
				}
				else
				{
					if (!Details::BSTSmallIndexScatterTableTraits__Insert_256_Orig(self.nameTable, index, p_id))
						Details::BSTSmallIndexScatterTableTraits__Resize_256_Orig(self.nameTable, p_id);
					else goto __ll_end;
				}

				Details::BSTSmallIndexScatterTableTraits__Insert_256_Orig(self.nameTable, index, p_id);
			}
			else if constexpr (std::is_same_v<T, RE::BSResource::Archive2::Index1024>)
			{
				if (self.nameTable.table == Details::EndTable_1024)
				{
					// constant initialization check that XCell/CKPE is cut out
					constexpr static auto MEMORY_INITIAZE_FLAG = 2;

					self.nameTable.avail = MEMORY_INITIAZE_FLAG;
					self.nameTable.table = Details::BSTSmallIndexScatterTableUtil__NewTable_1024_Orig(MEMORY_INITIAZE_FLAG);
				}
				else
				{
					if (!Details::BSTSmallIndexScatterTableTraits__Insert_1024_Orig(self.nameTable, index, p_id))
						Details::BSTSmallIndexScatterTableTraits__Resize_1024_Orig(self.nameTable, p_id);
					else goto __ll_end;
				}

				Details::BSTSmallIndexScatterTableTraits__Insert_1024_Orig(self.nameTable, index, p_id);
			}
		__ll_end:
			self.dataFileCount++;
		}

		static void Hook_Init()
		{
			*(uintptr_t*)&Details::Stream__Assign_Orig = REL::ID{ 933944, 2192397 }.address();

			if (RELEX::IsRuntimeAE() && (s_ArchiveArchitecture == ArchiveMode::k1kAvailableArchitecture))
			{
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableUtil__NewTable_1024_Orig = REL::ID{ 908309, 2268030 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Insert_1024_Orig = REL::ID{ 1541972, 2269374 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Resize_1024_Orig = REL::ID{ 91377, 2269427 }.address();
				*(uintptr_t*)&Details::EndTable_1024 = REL::ID{ 916672, 2666314 }.address();

				RELEX::DetourJump(REL::ID(2269366).address(), (uintptr_t)&AddDataFile<RE::BSResource::Archive2::Index1024>);

				struct AddDataFromReaderPatch_AE : Xbyak::CodeGenerator
				{
					AddDataFromReaderPatch_AE(std::uintptr_t targetAddr, std::uintptr_t funcAddr)
					{
						// run erase code
						mov(ptr[rsp + 0x4C], r12w);

						push(rax);
						push(rcx);
						push(rdx);
						sub(rsp, 0x28);

						// get ID
						lea(rcx, ptr[rsp + 0x80]);
						// get index arch
						mov(edx, ptr[rbp + 0x108]);
						// call link ID with arch
						mov(rax, funcAddr);
						call(rax);

						add(rsp, 0x28);
						pop(rdx);
						pop(rcx);
						pop(rax);

						// return back (ret)
						jmp(ptr[rip]);
						dq(targetAddr + 5);
					}
				};

				auto target = REL::ID(2269367).address() + 0x14F;
				RELEX::XbyakJump<AddDataFromReaderPatch_AE>(target, target, (uintptr_t)&PushGeneralArchiveIndex);
			}
			else if 
				(RELEX::IsRuntimeNG() || 
				(RELEX::IsRuntimeAE() && (s_ArchiveArchitecture == ArchiveMode::kOldArchitecture)))
			{
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableUtil__NewTable_256_Orig = REL::ID{ 908309, 2268030 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Insert_256_Orig = REL::ID{ 1541972, 2269374 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Resize_256_Orig = REL::ID{ 91377, 2269427 }.address();
				*(uintptr_t*)&Details::EndTable_256 = REL::ID{ 916672, 2666314 }.address();

				RELEX::DetourJump(REL::ID(2269366).address(), (uintptr_t)&AddDataFile<RE::BSResource::Archive2::Index256>);

				struct AddDataFromReaderPatch_NG : Xbyak::CodeGenerator
				{
					AddDataFromReaderPatch_NG(std::uintptr_t targetAddr, std::uintptr_t funcAddr)
					{
						// run erase code
						mov(ptr[rsp + 0x3C], r12b);

						push(rax);
						push(rcx);
						push(rdx);
						sub(rsp, 0x28);

						// get ID
						lea(rcx, ptr[rsp + 0x70]);
						// get index arch
						mov(edx, ptr[rbp + 0xE8]);
						// call link ID with arch
						mov(rax, funcAddr);
						call(rax);

						add(rsp, 0x28);
						pop(rdx);
						pop(rcx);
						pop(rax);

						// return back (ret)
						jmp(ptr[rip]);
						dq(targetAddr + 5);
					}
				};

				auto target = REL::ID(2269367).address() + 0xF5;
				RELEX::XbyakJump<AddDataFromReaderPatch_NG>(target, target, (uintptr_t)&PushGeneralArchiveIndex);
			}
			else
			{
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableUtil__NewTable_256_Orig = REL::ID{ 908309, 2268030 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Insert_256_Orig = REL::ID{ 1541972, 2269374 }.address();
				*(uintptr_t*)&Details::BSTSmallIndexScatterTableTraits__Resize_256_Orig = REL::ID{ 91377, 2269427 }.address();
				*(uintptr_t*)&Details::EndTable_256 = REL::ID{ 916672, 2666314 }.address();

				RELEX::DetourJump(REL::ID(270048).address(), (uintptr_t)&AddDataFile<RE::BSResource::Archive2::Index256>);

				struct AddDataFromReaderPatch_OG : Xbyak::CodeGenerator
				{
					AddDataFromReaderPatch_OG(std::uintptr_t targetAddr, std::uintptr_t funcAddr)
					{
						// run erase code
						mov(ptr[rsp + 0x4C], dil);

						push(rax);
						push(rcx);
						push(rdx);
						sub(rsp, 0x28);

						// get ID
						lea(rcx, ptr[rsp + 0x80]);
						// get index arch
						mov(edx, edi);
						// call link ID with arch
						mov(rax, funcAddr);
						call(rax);

						add(rsp, 0x28);
						pop(rdx);
						pop(rcx);
						pop(rax);

						// return back (ret)
						jmp(ptr[rip]);
						dq(targetAddr + 5);
					}
				};

				auto target = REL::ID(960903).address() + 0x207;
				RELEX::XbyakJump<AddDataFromReaderPatch_OG>(target, target, (uintptr_t)&PushGeneralArchiveIndex);
			}
		}
	}

	namespace SDirectory2
	{
		static void InsertReplicatedGeneralID(const RE::BSResource::ID& id, uint32_t repDir) noexcept
		{
			uint16_t index = FindGeneralArchiveIndex(id);
			if (index == static_cast<uint16_t>(-1))
				return;

			RE::BSResource::ID repId = id;
			repId.dir = repDir;
			PushGeneralArchiveIndex(repId, index);
		}

		static void Hook_Init()
		{
			////////////////////////////////////////////////
			// DEFAULT
			////////////////////////////////////////////////
			{
				struct FindGeneralPatch : Xbyak::CodeGenerator
				{
					FindGeneralPatch(uintptr_t target, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rcx);
						sub(rsp, 0x28);
						lea(rcx, ptr[rbp + 0x148]);
						call(ptr[rip + funcLabel]);
						add(rsp, 0x28);
						pop(rcx);

						cmp(eax, 0xFFFF);
						jne("RET");
						movzx(eax, byte[rbp + 0x154]);

						L("RET");
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(target + 7);

						L(funcLabel);
						dq(funcAddr);
					}
				};

				auto target = REL::Relocation(REL::ID{ 1298455, 2269311 }, 0xB5).address();
				RELEX::XbyakJump<FindGeneralPatch>(target, target, (uintptr_t)&FindGeneralArchiveIndex);
			}
			{
				struct GetDataFilePatch : Xbyak::CodeGenerator
				{
					explicit GetDataFilePatch(uintptr_t target)
					{
						mov(rcx, (uintptr_t)g_managerArchiveManager->dataFiles);
						mov(rdx, ptr[rcx + rax * 8]);
						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 5);
					}
				};

				auto target = REL::Relocation(REL::ID{ 1298455, 2269311 }, REL::Offset{ 0xD6, 0xC8 }).address();
				RELEX::XbyakJump<GetDataFilePatch>(target, target);
			}
			{
				struct FindGeneralPatch2 : Xbyak::CodeGenerator
				{
					FindGeneralPatch2(uintptr_t target, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rcx);
						sub(rsp, 0x28);

						lea(rcx, ptr[rbp + 0x148]);
						call(ptr[rip + funcLabel]);

						add(rsp, 0x28);
						pop(rcx);

						cmp(eax, 0xFFFF);
						jne("RET");
						movzx(eax, byte[rbp + 0x154]);

						L("RET");
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(target + 7);

						L(funcLabel);
						dq(funcAddr);
					}
				};

				auto target = REL::Relocation(REL::ID{ 1298455, 2269311 }, REL::Offset{ 0x13F, 0x12F }).address();
				RELEX::XbyakJump<FindGeneralPatch2>(target, target, (uintptr_t)&FindGeneralArchiveIndex);
			}
			////////////////////////////////////////////////
			// ASYNC
			////////////////////////////////////////////////
			if (RELEX::IsRuntimeAE())
			{
				struct FindGeneralPatch_AE : Xbyak::CodeGenerator
				{
					FindGeneralPatch_AE(uintptr_t target, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rcx);
						sub(rsp, 0x28);

						lea(rcx, ptr[rdi + 0x148]);
						call(ptr[rip + funcLabel]);

						add(rsp, 0x28);
						pop(rcx);

						cmp(eax, 0xFFFF);
						jne("RET");
						movzx(eax, byte[rdi + 0x154]);

						L("RET");
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(target + 7);

						L(funcLabel);
						dq(funcAddr);
					}
				};

				auto target = REL::Relocation(REL::ID(2269323), 0x8C).address();
				RELEX::XbyakJump<FindGeneralPatch_AE>(target, target, (uintptr_t)&FindGeneralArchiveIndex);
			}
			else
			{
				struct FindGeneralPatch_OG_NG : Xbyak::CodeGenerator
				{
					FindGeneralPatch_OG_NG(uintptr_t target, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rcx);
						sub(rsp, 0x28);

						lea(rcx, ptr[rsi + 0x148]);
						call(ptr[rip + funcLabel]);

						add(rsp, 0x28);
						pop(rcx);

						cmp(eax, 0xFFFF);
						jne("RET");
						movzx(eax, byte[rsi + 0x154]);

						L("RET");
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(target + 7);

						L(funcLabel);
						dq(funcAddr);
					}
				};
				
				auto target = REL::Relocation(REL::ID{ 788223, 2269323 }, 0x8C).address();
				RELEX::XbyakJump<FindGeneralPatch_OG_NG>(target, target, (uintptr_t)&FindGeneralArchiveIndex);
			}
			{
				struct GetAsyncDataFilePatch : Xbyak::CodeGenerator
				{
					explicit GetAsyncDataFilePatch(uintptr_t target)
					{
						mov(rcx, (uintptr_t)g_managerArchiveManager->asyncDataFiles);
						mov(rdx, ptr[rcx + rax * 8]);
						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 5);
					}
				};

				auto target = REL::Relocation(REL::ID{ 788223, 2269323 }, REL::Offset{ 0xB5, 0xAD }).address();
				RELEX::XbyakJump<GetAsyncDataFilePatch>(target, target);
			}
			////////////////////////////////////////////////
			// Replicate Dir
			////////////////////////////////////////////////
			if (!RELEX::IsRuntimeOG())
			{
				struct ReplicateDirToPatch_NG_AE : Xbyak::CodeGenerator
				{
					ReplicateDirToPatch_NG_AE(uintptr_t targetAddr, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rsi);
						push(rcx);
						push(rbx);
						push(r8);
						push(rdx);
						push(rdi);
						sub(rsp, 0x20);

						lea(rcx, ptr[rdi]);
						mov(edx, ebx);
						call(ptr[rip + funcLabel]);

						add(rsp, 0x20);
						pop(rdi);
						pop(rdx);
						pop(r8);
						pop(rbx);
						pop(rcx);
						pop(rsi);

						mov(ptr[rdi + 0x8], ebx);
						mov(ptr[rdi], ecx);
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(targetAddr + 0x5);

						L(funcLabel);
						dq(funcAddr);
					}
				};
				
				auto target = REL::Relocation(REL::ID(2269319), 0x296).address();
				RELEX::XbyakJump<ReplicateDirToPatch_NG_AE>(target, target, (uintptr_t)&InsertReplicatedGeneralID);
			}
			else
			{
				struct ReplicateDirToPatch_OG : Xbyak::CodeGenerator
				{
					ReplicateDirToPatch_OG(uintptr_t targetAddr, uintptr_t funcAddr)
					{
						Xbyak::Label retnLabel;
						Xbyak::Label funcLabel;

						push(rsi);
						push(rcx);
						push(rbx);
						push(r8);
						push(rdx);
						push(rax);
						sub(rsp, 0x20);

						lea(rcx, ptr[rdi]);
						mov(edx, ebx);
						call(ptr[rip + funcLabel]);

						add(rsp, 0x20);
						pop(rax);
						pop(rdx);
						pop(r8);
						pop(rbx);
						pop(rcx);
						pop(rsi);

						mov(ptr[rdi + 0x8], ebx);
						mov(ptr[rbp - 0x48], ebx);
						jmp(ptr[rip + retnLabel]);

						L(retnLabel);
						dq(targetAddr + 0x6);

						L(funcLabel);
						dq(funcAddr);
					}
				};

				auto target = REL::Relocation(REL::ID(338420), 0x2FD).address();
				RELEX::XbyakJump<ReplicateDirToPatch_OG>(target, target, (uintptr_t)&InsertReplicatedGeneralID);
			}
		}
	}

	namespace BSScaleformImageLoader
	{
		static void Hook_Init()
		{
			struct BSScaleformImageLoader : Xbyak::CodeGenerator
			{
				explicit BSScaleformImageLoader(uintptr_t target)
				{
					test(rcx, rcx);
					jne("JMP");
					xor_(al, al);
					ret();
					L("JMP");
					// return back (ret)
					jmp(ptr[rip]);
					dq(target);
				}
			};

			RELEX::XbyakJump<BSScaleformImageLoader>(REL::Relocation{ REL::ID{ 119731, 2287494 }, REL::Offset{ 0xBC, 0x6B }}.address(),
				REL::ID{ 142311, 2295283 }.address());
		}
	}

	namespace BSTextureIndex
	{
		RE::BSResource::ID dataFileNameIDs[MAX_SIZE];

		static void Hook_Init()
		{
			if (!RELEX::IsRuntimeOG())
			{
				auto id1 = REL::ID(2275558);

				if (s_ArchiveArchitecture == ArchiveMode::kOldArchitecture)
				{
					// movzx r15d, r13b -> mov r15d, r13d; nop;
					RELEX::WriteSafe(REL::Relocation{ id1, 0x33A }.get(),
						{ 0x45, 0x89, 0xEF, 0x90 });
				}

				struct AddDataFilePatch_NG_AE : Xbyak::CodeGenerator
				{
					explicit AddDataFilePatch_NG_AE(uintptr_t target)
					{
						// orig
						// mov eax, dword ptr ds:[rsi+0x28]
						// lea rcx, qword ptr ds:[r15+r15*2]
						// lea rdx, qword ptr ds:[rcx*4]
						// mov dword ptr ds:[rdx+r13+0x98DA8], eax
						// mov ecx, dword ptr ds:[rsi+0x24]
						// mov eax, dword ptr ds:[rsi+0x20]
						// shl rcx, 0x20
						// or rcx, rax
						// mov dword ptr ds:[rdx+r13+0x98DA0], ecx
						// shr rcx, 0x20
						// mov dword ptr ds:[rdx+r13+0x98DA4], ecx

						push(rbx);
						push(rdx);
						mov(rbx, (uintptr_t)dataFileNameIDs);
						mov(eax, ptr[rsi + 0x28]);
						lea(rdx, ptr[r15 + r15 * 2]);
						shl(rdx, 2);
						mov(ptr[rbx + rdx + 8], eax);
						mov(ecx, ptr[rsi + 0x24]);
						mov(eax, ptr[rsi + 0x20]);
						shl(rcx, 0x20);
						or_(rcx, rax);
						mov(ptr[rbx + rdx], ecx);
						shr(rcx, 0x20);
						mov(ptr[rbx + rdx + 4], ecx);
						pop(rdx);
						pop(rbx);

						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 0x38);
					}
				};

				auto target = id1.address() + REL::Offset(0x3B2).offset();
				RELEX::XbyakJump<AddDataFilePatch_NG_AE>(target, target);
			}
			else
			{
				// movzx r9d, r12b -> mov r9d, r12d; nop;
				RELEX::WriteSafe(REL::Relocation{ REL::ID(1388147), REL::Offset{ 0x406 } }.get(),
					{ 0x45, 0x8B, 0xCC, 0x90 });

				struct AddDataFilePatch_OG : Xbyak::CodeGenerator
				{
					explicit AddDataFilePatch_OG(std::uintptr_t target)
					{
						// orig
						// mov eax, dword ptr ds:[rsi+0x8]
						// lea rdx, qword ptr ds:[rdi+rdi*2]
						// mov dword ptr ds : [rbx+rdx*4+0x40], eax
						// mov ecx, dword ptr ds:[rsi+0x4]
						// mov eax, dword ptr ds:[rsi]
						// shl rcx, 0x20
						// or rcx, rax
						// mov dword ptr ds:[rbx+rdx*4+0x38], ecx
						// shr rcx, 0x20
						// mov dword ptr ds:[rbx+rdx*4+0x3C], ecx

						push(rbx);
						push(rdx);
						mov(rbx, (uintptr_t)dataFileNameIDs);
						mov(eax, ptr[rsi + 8]);
						lea(rdx, ptr[rdi + rdi * 2]);
						shl(rdx, 2);
						mov(ptr[rbx + rdx + 8], eax);
						mov(ecx, ptr[rsi + 4]);
						mov(eax, ptr[rsi]);
						shl(rcx, 0x20);
						or_(rcx, rax);
						mov(ptr[rbx + rdx], ecx);
						shr(rcx, 0x20);
						mov(ptr[rbx + rdx + 4], ecx);
						pop(rdx);
						pop(rbx);

						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 0x23);
					}
				};
				
				auto target = REL::ID(1004193).address() + REL::Offset{ 0x27 }.offset();
				RELEX::XbyakJump<AddDataFilePatch_OG>(target, target);
			}
		}
	}

	namespace BSTextureStreamer
	{
		namespace Manager
		{
			struct TextureRequest
			{
				RE::BSResource::Archive2::Index1024::EntryHeader header;
				char unk10[0x68];
				RE::BSFixedString unk78;
				char unk80[0x48];
				RE::NiTexture* texture;
				RE::BSFixedString texturePath;
				char unkD8[0x38];
			};
			static_assert(sizeof(TextureRequest) == 0x118);

			struct TextureRequestOld
			{
				RE::BSResource::Archive2::Index256::EntryHeader header;
				char unk10[0x68];
				RE::BSFixedString unk78;
				char unk80[0x48];
				RE::NiTexture* texture;
				RE::BSFixedString texturePath;
				char unkD8[0x38];
			};
			static_assert(sizeof(TextureRequestOld) == 0x110);

			static void ProcessPath(const char* inputPath, char* outputPath) noexcept
			{
				char temp[REX::W32::MAX_PATH]{};
				size_t i = 0;

				for (; inputPath[i] && (i < (size_t)(REX::W32::MAX_PATH - 1)); i++)
				{
					char c = inputPath[i];

					if ((c >= 'A') && (c <= 'Z'))
						c += 32;

					if (c == '/')
						c = '\\';

					temp[i] = c;
				}
				temp[i] = '\0';

				const char* p = temp;
				const char* dataPos = strstr(p, "data\\");

				if (dataPos)
					p = dataPos + 5;

				if (strncmp(p, "textures\\", 9) == 0)
				{
					strcpy_s(outputPath, REX::W32::MAX_PATH, p);
					return;
				}

				strcpy_s(outputPath, REX::W32::MAX_PATH, "textures\\");
				strcat_s(outputPath, REX::W32::MAX_PATH, p);
			}

			static uint16_t FindArchiveIndexByTextureRequestEx(const TextureRequest& request) noexcept
			{
				auto fileName = request.texturePath.c_str();
				if (fileName && fileName[0])
				{
					char processedPath[REX::W32::MAX_PATH];
					ProcessPath(fileName, processedPath);

					RE::BSResource::ID id(processedPath);
					return FindTexturesArchiveIndex(id);
				}

				if (request.texture && request.texture->rendererTexture)
				{
					auto Renderer = (BSGraphics::TextureEx*)request.texture->rendererTexture;
					if (Renderer->data)
						return Renderer->data->dataFileIndex;
				}

				RE::BSResource::ID id = request.header.nameID;
				return FindTexturesArchiveIndex(id);
			}

			static uint16_t FindArchiveIndexByTextureRequest(const TextureRequestOld& request) noexcept
			{
				auto fileName = request.texturePath.c_str();
				if (fileName && fileName[0])
				{
					char processedPath[REX::W32::MAX_PATH];
					ProcessPath(fileName, processedPath);

					RE::BSResource::ID id(processedPath);
					return FindTexturesArchiveIndex(id);
				}

				if (request.texture && request.texture->rendererTexture)
				{
					auto Renderer = (BSGraphics::Texture*)request.texture->rendererTexture;
					if (Renderer->data)
						return (((uint16_t)Renderer->data->dataFileHighIndex) << 8) | Renderer->data->dataFileIndex;
				}

				RE::BSResource::ID id = request.header.nameID;
				return FindTexturesArchiveIndex(id);
			}

			static void Hook_Init()
			{
				////////////////////////////////////////////////
				// Process Event
				////////////////////////////////////////////////
				if (RELEX::IsRuntimeAE() && (s_ArchiveArchitecture == ArchiveMode::k1kAvailableArchitecture))
				{
					struct ProcessEventPatch_AE : Xbyak::CodeGenerator
					{
						ProcessEventPatch_AE(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							mov(ptr[rsp + 0x6C], r13w);

							push(rcx);
							push(rdx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsp + 0x90]);
							movzx(edx, r13w);

							call(ptr[rip + funcLabel]);

							add(rsp, 0x20);
							pop(rdx);
							pop(rcx);

							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 0x5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(2275558), REL::Offset{ 0x2AB } }.get();
					RELEX::XbyakJump<ProcessEventPatch_AE>(target, target, (uintptr_t)&PushTexturesArchiveIndex);
				}
				else if
					(RELEX::IsRuntimeNG() ||
						(RELEX::IsRuntimeAE() && (s_ArchiveArchitecture == ArchiveMode::kOldArchitecture)))
				{
					struct ProcessEventPatch_NG : Xbyak::CodeGenerator
					{
						ProcessEventPatch_NG(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							mov(ptr[rsp + 0x3C], r13b);

							push(rcx);
							push(rdx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsp + 0x60]);
							movzx(edx, r13w);

							call(ptr[rip + funcLabel]);

							add(rsp, 0x20);
							pop(rdx);
							pop(rcx);

							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 0x5);

							L(funcLabel);
							dq(func);
						}
					};
					
					auto target = REL::Relocation{ REL::ID(2275558), REL::Offset{ 0x2BB } }.get();
					RELEX::XbyakJump<ProcessEventPatch_NG>(target, target, (uintptr_t)&PushTexturesArchiveIndex);
				}
				else
				{
					struct ProcessEventPatch_OG : Xbyak::CodeGenerator
					{
						ProcessEventPatch_OG(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							mov(ptr[rsp + 0x5C], r12b);

							push(rcx);
							push(rdx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsp + 0x80]);
							mov(edx, r12d);

							call(ptr[rip + funcLabel]);

							add(rsp, 0x20);
							pop(rdx);
							pop(rcx);

							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 0x5);

							L(funcLabel);
							dq(func);
						}
					};


					auto target = REL::Relocation{ REL::ID(1388147), REL::Offset{ 0x255 } }.get();
					RELEX::XbyakJump<ProcessEventPatch_OG>(target, target, (uintptr_t)&PushTexturesArchiveIndex);
				}
				////////////////////////////////////////////////
				// Start streaming chunks
				////////////////////////////////////////////////
				if (!RELEX::IsRuntimeOG())
				{
					struct StartStreamingChunksPatch_NG_AE : Xbyak::CodeGenerator
					{
						StartStreamingChunksPatch_NG_AE(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rcx);
							push(rdx);
							sub(rsp, 0x20);
							lea(rcx, ptr[r14]);
							call(ptr[rip + funcLabel]);
							mov(r8d, eax);
							add(rsp, 0x20);
							pop(rdx);
							pop(rcx);

							cmp(r8d, 0xFFFF);
							jne("RET");
							movzx(r8d, byte[r14 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(2275576), REL::Offset{ 0x3D } }.get();

					RELEX::XbyakJump<StartStreamingChunksPatch_NG_AE>(target, target, 
						(s_ArchiveArchitecture == ArchiveMode::kOldArchitecture) ?
							(uintptr_t)&FindArchiveIndexByTextureRequest :
							(uintptr_t)&FindArchiveIndexByTextureRequestEx);
				}
				else
				{
					struct StartStreamingChunksPatch_OG : Xbyak::CodeGenerator
					{
						StartStreamingChunksPatch_OG(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rcx);
							push(rax);
							sub(rsp, 0x20);
							lea(rcx, ptr[r14]);
							call(ptr[rip + funcLabel]);
							mov(edx, eax);
							add(rsp, 0x20);
							pop(rax);
							pop(rcx);

							cmp(edx, 0xFFFF);
							jne("RET");
							movzx(edx, byte[r14 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(844246), REL::Offset{ 0x3D } }.get();
					RELEX::XbyakJump<StartStreamingChunksPatch_OG>(target, target, (uintptr_t)&FindArchiveIndexByTextureRequest);
				}
				////////////////////////////////////////////////
				// Decompress streamed load
				////////////////////////////////////////////////
				if (!RELEX::IsRuntimeOG())
				{
					struct DecompressStreamedLoadPatch_NG_AE : Xbyak::CodeGenerator
					{
						DecompressStreamedLoadPatch_NG_AE(std::uintptr_t target, std::uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rcx);
							push(rdx);
							sub(rsp, 0x28);
							lea(rcx, ptr[r13]);
							call(ptr[rip + funcLabel]);
							mov(r8d, eax);
							add(rsp, 0x28);
							pop(rdx);
							pop(rcx);
							pop(rax);

							cmp(r8d, 0xFFFF);
							jne("RET");
							movzx(r8d, byte[r13 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(2275577), REL::Offset{ 0x9AD } }.get();
					RELEX::XbyakJump<DecompressStreamedLoadPatch_NG_AE>(target, target,
						(s_ArchiveArchitecture == ArchiveMode::kOldArchitecture) ?
						(uintptr_t)&FindArchiveIndexByTextureRequest :
						(uintptr_t)&FindArchiveIndexByTextureRequestEx);
				}
				else
				{
					struct DecompressStreamedLoadPatch_OG : Xbyak::CodeGenerator
					{
						DecompressStreamedLoadPatch_OG(std::uintptr_t target, std::uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rdx);
							sub(rsp, 0x20);
							lea(rcx, ptr[r15]);
							call(ptr[rip + funcLabel]);
							mov(ecx, eax);
							add(rsp, 0x20);
							pop(rdx);
							pop(rax);

							cmp(ecx, 0xFFFF);
							jne("RET");
							movzx(ecx, byte[r15 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(1296411), REL::Offset{ 0x62 } }.get();
					RELEX::XbyakJump<DecompressStreamedLoadPatch_OG>(target, target,
						(s_ArchiveArchitecture == ArchiveMode::kOldArchitecture) ?
						(uintptr_t)&FindArchiveIndexByTextureRequest :
						(uintptr_t)&FindArchiveIndexByTextureRequestEx);
				}
				////////////////////////////////////////////////
				// BSGraphics::Renderer::CreateStreamingTexture
				////////////////////////////////////////////////
				{
					struct CreateStreamingTexturePatch_256 : Xbyak::CodeGenerator
					{
						CreateStreamingTexturePatch_256(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rcx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsi]);
							call(ptr[rip + funcLabel]);

							mov(edx, eax);

							add(rsp, 0x20);
							pop(rcx);
							pop(rax);

							cmp(edx, 0xFFFF);
							je("RET");

							mov(byte[rax + 4], dl);
							mov(byte[rax + 7], dh);

							L("RET");
							movzx(edx, byte[rcx + 0x3C]);
							mov(ptr[rax + 6], dl);
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 0xD);

							L(funcLabel);
							dq(func);
						}
					};

					struct CreateStreamingTexturePatch_1024 : Xbyak::CodeGenerator
					{
						CreateStreamingTexturePatch_1024(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rcx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsi]);
							call(ptr[rip + funcLabel]);

							mov(edx, eax);

							add(rsp, 0x20);
							pop(rcx);
							pop(rax);

							cmp(edx, 0xFFFF);
							je("RET");

							mov(byte[rax + 4], dx);

							L("RET");
							movzx(edx, byte[rcx + 0x3C]);
							mov(ptr[rax + 8], dx);
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 0xD);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID{ 917602, 2276914 }, REL::Offset{ 0x8B } }.get();
					if (s_ArchiveArchitecture == ArchiveMode::kOldArchitecture)
						RELEX::XbyakJump<CreateStreamingTexturePatch_256>(target, target, (uintptr_t)&FindTexturesArchiveIndex);
					else
						RELEX::XbyakJump<CreateStreamingTexturePatch_1024>(target, target, (uintptr_t)&FindTexturesArchiveIndex);
				}
				////////////////////////////////////////////////
				// BSGraphics::CreateStreamingDDSTexture
				////////////////////////////////////////////////
				{
					struct CreateStreamingDDSTexturePatch : Xbyak::CodeGenerator
					{
						CreateStreamingDDSTexturePatch(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rcx);
							sub(rsp, 0x28);
							lea(rcx, ptr[rsi]);
							call(ptr[rip + funcLabel]);
							add(rsp, 0x28);
							pop(rcx);

							cmp(eax, 0xFFFF);
							jne("RET");
							movzx(eax, byte[rsi + 0xC]);

							L("RET");
							mov(ptr[r14 + 0x12], ax);
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 5);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID{ 823682, 2277293 }, REL::Offset{ 0xA5, 0x95 } }.get();
					RELEX::XbyakJump<CreateStreamingDDSTexturePatch>(target, target, (uintptr_t)&FindTexturesArchiveIndex);
				}
				////////////////////////////////////////////////
				// ThreadProc
				////////////////////////////////////////////////
				if (!RELEX::IsRuntimeOG())
				{
					struct ThreadProcPatch_NG_AE : Xbyak::CodeGenerator
					{
						ThreadProcPatch_NG_AE(uintptr_t a_target, uintptr_t a_funcAddr)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							sub(rsp, 0x20);
							lea(rcx, ptr[r13]);
							call(ptr[rip + funcLabel]);
							mov(r8d, eax);
							add(rsp, 0x20);

							cmp(r8d, 0xFFFF);
							jne("RET");
							movzx(r8d, byte[r13 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(a_target + 5);

							L(funcLabel);
							dq(a_funcAddr);
						}
					};

					auto target = REL::Relocation{ REL::ID(2275577), REL::Offset{ 0x10CB } }.get();
					RELEX::XbyakJump<ThreadProcPatch_NG_AE>(target, target, (uintptr_t)&FindTexturesArchiveIndex);
				}
				else
				{
					struct ThreadProcPatch_OG : Xbyak::CodeGenerator
					{
						ThreadProcPatch_OG(uintptr_t a_target, uintptr_t a_funcAddr)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							sub(rsp, 0x28);
							lea(rcx, ptr[r14]);
							call(ptr[rip + funcLabel]);
							mov(ecx, eax);
							add(rsp, 0x28);
							pop(rax);

							cmp(ecx, 0xFFFF);
							jne("RET");
							movzx(ecx, byte[r14 + 0xC]);

							L("RET");
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(a_target + 5);

							L(funcLabel);
							dq(a_funcAddr);
						}
					};

					auto target = REL::Relocation{ REL::ID(989173), REL::Offset{ 0x48F } }.get();
					RELEX::XbyakJump<ThreadProcPatch_OG>(target, target, (uintptr_t)&FindTexturesArchiveIndex);
				}
				////////////////////////////////////////////////
				// LoadChunks
				////////////////////////////////////////////////
				{
					struct LoadChunksPatch : Xbyak::CodeGenerator
					{
						LoadChunksPatch(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rcx);
							push(rdx);
							push(r8);
							push(r9);
							push(r10);
							push(r11);
							sub(rsp, 0x28);
							lea(rcx, ptr[rdx]);
							call(ptr[rip + funcLabel]);
							mov(ebx, eax);
							add(rsp, 0x28);
							pop(r11);
							pop(r10);
							pop(r9);
							pop(r8);
							pop(rdx);
							pop(rcx);
							pop(rax);

							cmp(ebx, 0xFFFF);
							jne("RET");
							movzx(ebx, byte[rdx + 0xC]);

							L("RET");
							if (s_ArchiveArchitecture == ArchiveMode::kOldArchitecture)
								movzx(edi, byte[rdx + 0xD]);
							else
								movzx(edi, byte[rdx + 0xE]);
							if (RELEX::IsRuntimeOG()) 
								cmp(dword[rdx + 0x104], 1);
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 8);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID{ 979945, 2275550 }, REL::Offset{ 0x27, 0x32 } }.get();
					RELEX::XbyakJump<LoadChunksPatch>(target, target,
						(s_ArchiveArchitecture == ArchiveMode::kOldArchitecture) ?
						(uintptr_t)&FindArchiveIndexByTextureRequest :
						(uintptr_t)&FindArchiveIndexByTextureRequestEx);
				}
			}
		}
	}

	ModuleArchiveLimits::ModuleArchiveLimits() :
		Module("Archive Limits", &bPatchesArchiveLimits)
	{
		g_managerArchiveManager = std::make_unique<Index>();
	}

	bool ModuleArchiveLimits::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (REX::FModule::GetExecutingModule().GetFileVersion() >= F4SE::RUNTIME_1_11_240)
			s_ArchiveArchitecture = ArchiveMode::k1kAvailableArchitecture;

		Archive2::Hook_Init();
		SDirectory2::Hook_Init();
		BSScaleformImageLoader::Hook_Init();
		BSTextureIndex::Hook_Init();
		BSTextureStreamer::Manager::Hook_Init();

		return true;
	}

	bool ModuleArchiveLimits::HasProcessDefender() noexcept
	{
		return true;
	}
}
