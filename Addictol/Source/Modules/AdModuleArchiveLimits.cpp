#include <Modules\AdModuleArchiveLimits.h>
#include <AdUtils.h>
#include <xbyak\xbyak.h>
#include <unordered_dense\unordered_dense.h>

#undef MEM_RELEASE
#undef ERROR
#undef MAX_PATH

#include <RE\B\BSResource_Archive2_Index.h>
#include <RE\N\NiTexture.h>

// Thanks WirelessLan for idea: https://github.com/WirelessLan/BSALimitExpander

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesArchiveLimits{ "Patches"sv, "bArchiveLimits"sv, true };

	constexpr static auto MAX_SIZE = 64 * 1024;

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
		[[nodiscard]] std::size_t operator()(const RE::BSResource::ID& a_id) const noexcept(true)
		{
			return
				((std::hash<uint32_t>()(a_id.file) ^
					(std::hash<uint32_t>()(a_id.ext) << 1)) >> 1) ^
					(std::hash<uint32_t>()(a_id.dir) << 1);
		}
	};

	using Storage = ankerl::unordered_dense::map<RE::BSResource::ID, uint16_t, IDHash>;

	enum class StorageType : uint8_t
	{
		kGeneral = 0,
		kTextures,
		kTotal
	};

	Storage Storages[static_cast<uint8_t>(StorageType::kTotal)];
	RE::BSReadWriteLock StorageLocks[static_cast<uint8_t>(StorageType::kTotal)];

	static void PushArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx, StorageType archType) noexcept(true)
	{
		RE::BSAutoWriteLock lock(StorageLocks[static_cast<uint8_t>(archType)]);
		Storages[static_cast<uint8_t>(archType)][id] = static_cast<uint16_t>(archIdx);
	}

	inline static void PushGeneralArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx) noexcept(true)
	{
		PushArchiveIndex(id, archIdx, StorageType::kGeneral);
	}

	inline static void PushTexturesArchiveIndex(const RE::BSResource::ID& id, uint32_t archIdx) noexcept(true)
	{
		PushArchiveIndex(id, archIdx, StorageType::kTextures);
	}

	[[nodiscard]] static uint16_t FindArchiveIndex(const RE::BSResource::ID& id, StorageType archType) noexcept(true)
	{
		RE::BSAutoWriteLock lock(StorageLocks[static_cast<uint8_t>(archType)]);

		auto it = Storages[static_cast<uint8_t>(archType)].find(id);
		return (it == Storages[static_cast<uint8_t>(archType)].end()) ?
			static_cast<uint16_t>(-1) : it->second;
	}

	[[nodiscard]] inline static uint16_t FindGeneralArchiveIndex(const RE::BSResource::ID& id) noexcept(true)
	{
		return FindArchiveIndex(id, StorageType::kGeneral);
	}

	[[nodiscard]] inline static uint16_t FindTexturesArchiveIndex(const RE::BSResource::ID& id) noexcept(true)
	{
		return FindArchiveIndex(id, StorageType::kTextures);
	}

	using Stream__Assign = void (*)(RE::BSTSmartPointer<RE::BSResource::Stream>&, RE::BSTSmartPointer<RE::BSResource::Stream>&);
	inline static Stream__Assign Stream__Assign_Orig{ nullptr };

	using BSTSmallIndexScatterTableUtil__NewTable = RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
		RE::BSResource::Archive2::Index::NameIDAccess>::entry_type* (*)(uint32_t);
	inline static BSTSmallIndexScatterTableUtil__NewTable BSTSmallIndexScatterTableUtil__NewTable_Orig{ nullptr };

	using BSTSmallIndexScatterTableTraits__Insert = bool (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
		RE::BSResource::Archive2::Index::NameIDAccess>&, uint32_t, RE::BSResource::ID*&);
	inline static BSTSmallIndexScatterTableTraits__Insert BSTSmallIndexScatterTableTraits__Insert_Orig{ nullptr };

	using BSTSmallIndexScatterTableTraits__Resize = void (*)(RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
		RE::BSResource::Archive2::Index::NameIDAccess>&, RE::BSResource::ID*&);
	inline static BSTSmallIndexScatterTableTraits__Resize BSTSmallIndexScatterTableTraits__Resize_Orig{ nullptr };

	inline static RE::BSTSmallIndexScatterTable<RE::BSResource::ID,
		RE::BSResource::Archive2::Index::NameIDAccess>::entry_type* EndTable{ nullptr };

	namespace Archive2
	{
		static void AddDataFile(RE::BSResource::Archive2::Index& self, RE::BSTSmartPointer<RE::BSResource::Stream>& stream,
			RE::BSResource::ID& id, uint32_t index) noexcept
		{
			Stream__Assign_Orig(g_managerArchiveManager->dataFiles[index], stream);
			stream->DoCreateAsync(g_managerArchiveManager->asyncDataFiles[index]);

			if (self.dataFileCount != index)
				return;

			// REX::INFO("DBG: {} {}", self.dataFileCount, index);

			g_managerArchiveManager->dataFileNameIDs[index] = id;
			auto* p_id = g_managerArchiveManager->dataFileNameIDs;

			if (self.nameTable.table == EndTable)
			{
				// constant initialization check that XCell/CKPE is cut out
				constexpr static auto MEMORY_INITIAZE_FLAG = 2;

				self.nameTable.avail = MEMORY_INITIAZE_FLAG;
				self.nameTable.table = BSTSmallIndexScatterTableUtil__NewTable_Orig(MEMORY_INITIAZE_FLAG);
			}
			else
			{
				if (!BSTSmallIndexScatterTableTraits__Insert_Orig(self.nameTable, index, p_id))
					BSTSmallIndexScatterTableTraits__Resize_Orig(self.nameTable, p_id);
				else goto __ll_end;
			}

			BSTSmallIndexScatterTableTraits__Insert_Orig(self.nameTable, index, p_id);
		__ll_end:
			self.dataFileCount++;
		}

		static void Hook_Init()
		{
			*(uintptr_t*)&Stream__Assign_Orig = REL::ID(2192397).address();
			*(uintptr_t*)&BSTSmallIndexScatterTableUtil__NewTable_Orig = REL::ID(2268030).address();
			*(uintptr_t*)&BSTSmallIndexScatterTableTraits__Insert_Orig = REL::ID(2269374).address();
			*(uintptr_t*)&BSTSmallIndexScatterTableTraits__Resize_Orig = REL::ID(2269427).address();
			*(uintptr_t*)&EndTable = REL::ID(2666314).address();

			RELEX::DetourJump(REL::ID(2269366).address(), (uintptr_t)&AddDataFile);

			{
				struct AddDataFromReaderPatch_AE : Xbyak::CodeGenerator
				{
					AddDataFromReaderPatch_AE(std::uintptr_t targetAddr, std::uintptr_t funcAddr)
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
				auto patch = new AddDataFromReaderPatch_AE(target, (std::uintptr_t)&PushGeneralArchiveIndex);
				RELEX::DetourJump(target, (std::uintptr_t)patch->getCode());
			}
		}
	}

	namespace SDirectory2
	{
		static void InsertReplicatedGeneralID(const RE::BSResource::ID& id, uint32_t repDir) noexcept(true)
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
				struct FindGeneralPatch_AE : Xbyak::CodeGenerator
				{
					FindGeneralPatch_AE(uintptr_t target, uintptr_t funcAddr)
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

				auto target = REL::ID(2269311).address() + REL::Offset{ 0xB5 }.offset();
				auto patch = new FindGeneralPatch_AE(target, (uintptr_t)&FindGeneralArchiveIndex);
				RELEX::DetourJump(target, (uintptr_t)patch->getCode());
			}
			{
				struct GetDataFilePatch_AE : Xbyak::CodeGenerator
				{
					GetDataFilePatch_AE(uintptr_t target)
					{
						mov(rcx, (uintptr_t)g_managerArchiveManager->dataFiles);
						mov(rdx, ptr[rcx + rax * 8]);
						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 5);
					}
				};

				auto target = REL::ID(2269311).address() + REL::Offset{ 0xC8 }.offset();
				auto patch = new GetDataFilePatch_AE(target);
				RELEX::DetourJump(target, (uintptr_t)patch->getCode());
			}
			////////////////////////////////////////////////
			// ASYNC
			////////////////////////////////////////////////
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

				auto target = REL::ID(2269323).address() + REL::Offset{ 0x8C }.offset();
				auto patch = new FindGeneralPatch_AE(target, (uintptr_t)&FindGeneralArchiveIndex);
				RELEX::DetourJump(target, (uintptr_t)patch->getCode());
			}
			{
				struct GetAsyncDataFilePatch_AE : Xbyak::CodeGenerator
				{
					GetAsyncDataFilePatch_AE(uintptr_t target)
					{
						mov(rcx, (uintptr_t)g_managerArchiveManager->asyncDataFiles);
						mov(rdx, ptr[rcx + rax * 8]);
						// return back (ret)
						jmp(ptr[rip]);
						dq(target + 5);
					}
				};

				auto target = REL::ID(2269323).address() + REL::Offset{ 0xAD }.offset();
				auto patch = new GetAsyncDataFilePatch_AE(target);
				RELEX::DetourJump(target, (uintptr_t)patch->getCode());
			}

			////////////////////////////////////////////////
			// Replicate Dir
			////////////////////////////////////////////////
			{
				struct ReplicateDirToPatch_AE : Xbyak::CodeGenerator
				{
					ReplicateDirToPatch_AE(uintptr_t targetAddr, uintptr_t funcAddr)
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

				auto target = REL::ID(2269319).address() + REL::Offset{ 0x296 }.offset();
				auto patch = new ReplicateDirToPatch_AE(target, (uintptr_t)InsertReplicatedGeneralID);
				RELEX::DetourJump(target, (uintptr_t)patch->getCode());
			}
		}
	}

	namespace BSScaleformImageLoader
	{
		static void Hook_Init()
		{
			struct BSScaleformImageLoader_AE : Xbyak::CodeGenerator
			{
				BSScaleformImageLoader_AE(uintptr_t target)
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

			auto patch = new BSScaleformImageLoader_AE(REL::ID(2295283).address());
			RELEX::DetourJump(REL::Relocation{ REL::ID(2287494), REL::Offset{ 0x6B } }.get(),
				(uintptr_t)patch->getCode());
		}
	}

	namespace BSTextureIndex
	{
		RE::BSResource::ID dataFileNameIDs[MAX_SIZE];

		static void Hook_Init()
		{
			auto id1 = REL::ID(2275558);
			// movzx r15d, r13b -> mov r15d, r13d; nop;
			RELEX::WriteSafe(REL::Relocation{ id1, REL::Offset{ 0x33A } }.get(),
				{ 0x45, 0x89, 0xEF, 0x90 });

			struct AddDataFilePatch_AE : Xbyak::CodeGenerator
			{
				AddDataFilePatch_AE(std::uintptr_t target)
				{
					// orig
					// mov eax, dword ptr ds : [rsi + 0x28]
					// lea rcx, qword ptr ds : [r15 + r15 * 2]
					// lea rdx, qword ptr ds : [rcx * 4]
					// mov dword ptr ds : [rdx + r13 + 0x98DA8] , eax
					// mov ecx, dword ptr ds : [rsi + 0x24]
					// mov eax, dword ptr ds : [rsi + 0x20]
					// shl rcx, 0x20
					// or rcx, rax
					// mov dword ptr ds : [rdx + r13 + 0x98DA0] , ecx
					// shr rcx, 0x20
					// mov dword ptr ds : [rdx + r13 + 0x98DA4] , ecx

					push(rbx);
					push(rdx);
					mov(rbx, (std::uintptr_t)dataFileNameIDs);
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

			auto target = id1.address() + REL::Offset{ 0x3B2 }.offset();
			auto patch = new AddDataFilePatch_AE(target);
			RELEX::DetourJump(target, (uintptr_t)patch->getCode());
		}
	}

	namespace BSTextureStreamer
	{
		namespace Manager
		{
			struct TextureRequest
			{
				RE::BSResource::Archive2::Index::EntryHeader header;
				char unk10[0x68];
				RE::BSFixedString unk78;
				char unk80[0x48];
				RE::NiTexture* texture;
				RE::BSFixedString texturePath;
				char unkD8[0x38];
			};
			static_assert(sizeof(TextureRequest) == 0x110);

			static void ProcessPath(const char* inputPath, char* outputPath) noexcept(true)
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

			static uint16_t FindArchiveIndexByTextureRequest(const TextureRequest& request) noexcept(true)
			{
				auto fileName = request.texturePath.c_str();
				if (fileName && fileName[0])
				{
					char processedPath[REX::W32::MAX_PATH];
					ProcessPath(fileName, processedPath);

					RE::BSResource::ID id;
					id.GenerateFromPath(processedPath);
					return FindTexturesArchiveIndex(id);
				}

				if (request.texture && request.texture->rendererTexture)
				{
					auto Renderer = (BSGraphics::Texture*)request.texture->rendererTexture;
					if (Renderer->data)
						return (Renderer->data->dataFileHighIndex << 8) | Renderer->data->dataFileIndex;
				}

				RE::BSResource::ID id = request.header.nameID;
				return FindTexturesArchiveIndex(id);
			}

			static void Hook_Init()
			{
				////////////////////////////////////////////////
				// Process Event
				////////////////////////////////////////////////
				{
					struct ProcessEventPatch_AE : Xbyak::CodeGenerator
					{
						ProcessEventPatch_AE(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							mov(ptr[rsp + 0x3C], r13b);

							push(rcx);
							push(rdx);
							sub(rsp, 0x20);

							lea(rcx, ptr[rsp + 0x60]);
							mov(edx, r13d);

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
					auto patch = new ProcessEventPatch_AE(target, (uintptr_t)PushTexturesArchiveIndex);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// Load chunks
				////////////////////////////////////////////////
				{
					struct LoadChunksPatch_AE : Xbyak::CodeGenerator
					{
						LoadChunksPatch_AE(uintptr_t target, uintptr_t func)
						{
							Xbyak::Label retnLabel;
							Xbyak::Label funcLabel;

							push(rax);
							push(rcx);
							push(rdx);
							push(r10);
							push(r11);
							sub(rsp, 0x28);

							lea(rcx, ptr[rdx]);
							call(ptr[rip + funcLabel]);
							mov(ebx, eax);

							add(rsp, 0x28);
							pop(r11);
							pop(r10);
							pop(rdx);
							pop(rcx);
							pop(rax);

							cmp(ebx, 0xFFFF);
							jne("RET");
							movzx(ebx, byte[rdx + 0xC]);

							L("RET");
							movzx(edi, byte[rdx + 0xD]);
							jmp(ptr[rip + retnLabel]);

							L(retnLabel);
							dq(target + 8);

							L(funcLabel);
							dq(func);
						}
					};

					auto target = REL::Relocation{ REL::ID(2275550), REL::Offset{ 0x32 } }.get();
					auto patch = new LoadChunksPatch_AE(target, (uintptr_t)FindTexturesArchiveIndex);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// Start streaming chunks
				////////////////////////////////////////////////
				{
					struct StartStreamingChunksPatch_AE : Xbyak::CodeGenerator
					{
						StartStreamingChunksPatch_AE(uintptr_t target, uintptr_t func)
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
					auto patch = new StartStreamingChunksPatch_AE(target, (uintptr_t)FindArchiveIndexByTextureRequest);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// Decompress streamed load
				////////////////////////////////////////////////
				{
					struct DecompressStreamedLoadPatch_AE : Xbyak::CodeGenerator
					{
						DecompressStreamedLoadPatch_AE(std::uintptr_t target, std::uintptr_t func)
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
					auto patch = new DecompressStreamedLoadPatch_AE(target, (uintptr_t)FindArchiveIndexByTextureRequest);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// BSGraphics::Renderer::CreateStreamingTexture
				////////////////////////////////////////////////
				{
					struct CreateStreamingTexturePatch_AE : Xbyak::CodeGenerator
					{
						CreateStreamingTexturePatch_AE(uintptr_t target, uintptr_t func)
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

					auto target = REL::Relocation{ REL::ID(2276914), REL::Offset{ 0x8B } }.get();
					auto patch = new CreateStreamingTexturePatch_AE(target, (uintptr_t)FindTexturesArchiveIndex);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// BSGraphics::CreateStreamingDDSTexture
				////////////////////////////////////////////////
				{
					struct CreateStreamingDDSTexturePatch_AE : Xbyak::CodeGenerator
					{
						CreateStreamingDDSTexturePatch_AE(uintptr_t target, uintptr_t func)
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
					
					auto target = REL::Relocation{ REL::ID(2277293), REL::Offset{ 0x95 } }.get();
					auto patch = new CreateStreamingDDSTexturePatch_AE(target, (uintptr_t)FindTexturesArchiveIndex);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
				////////////////////////////////////////////////
				// ThreadProc
				////////////////////////////////////////////////
				{
					struct ThreadProcPatch_AE : Xbyak::CodeGenerator
					{
						ThreadProcPatch_AE(uintptr_t a_target, uintptr_t a_funcAddr)
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
					auto patch = new ThreadProcPatch_AE(target, (uintptr_t)FindTexturesArchiveIndex);
					RELEX::DetourJump(target, (uintptr_t)patch->getCode());
				}
			}
		}
	}

	ModuleArchiveLimits::ModuleArchiveLimits() :
		Module("Module Archive Limits", &bPatchesArchiveLimits)
	{
		g_managerArchiveManager = std::make_unique<Index>();
	}

	bool ModuleArchiveLimits::DoQuery() const noexcept
	{
		return RELEX::IsRuntimeAE();
	}

	bool ModuleArchiveLimits::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		Archive2::Hook_Init();
		SDirectory2::Hook_Init();
		BSScaleformImageLoader::Hook_Init();
		BSTextureIndex::Hook_Init();
		BSTextureStreamer::Manager::Hook_Init();

		return true;
	}

	bool ModuleArchiveLimits::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleArchiveLimits::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}