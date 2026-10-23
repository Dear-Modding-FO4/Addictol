#include <Modules/AdModuleRefExtended.h>
#include <Core/AdUtils.h>
#include <Core/AdAssert.h>

#include <RE/B/BSHandleRefObject.h>
#include <RE/B/BSPointerHandle.h>
#include <RE/N/NiRefObject.h>
#include <RE/N/NiPointer.h>
#include <RE/T/TESObjectREFR.h>

#include <algorithm>

namespace Addictol
{
	namespace RefExtendedExtendedDetail
	{
		template<uint32_t RefCountBits>
		class BSHandleRefObject : public RE::NiRefObject
		{
		public:
			//
			// 31             +1       RefCountBits    0
			// |--------------|--------|---------------|
			// | Handle Index | Active |   Ref Count   |
			// |--------------|--------|---------------|
			//
			constexpr static uint32_t ACTIVE_BIT_INDEX	= RefCountBits;
			constexpr static uint32_t HANDLE_BIT_INDEX	= ACTIVE_BIT_INDEX + 1;
			constexpr static uint32_t REF_COUNT_MASK	= (1ull << ACTIVE_BIT_INDEX) - 1ull;

		public:
			BSHandleRefObject() noexcept(true)
			{
				ClearHandleEntryIndex();
			}
			virtual ~BSHandleRefObject() noexcept(true)
			{
				ClearHandleEntryIndex();
			}

			inline uint32_t IncRefCount() noexcept(true)
			{
				AdAssertWithMessage(GetRefCount() < REF_COUNT_MASK, "BSHandleRefObject - IncRefCount is about to cause refcount wraparound to 0.");

				return ((NiRefObject*)this)->IncRefCount();
			}

			inline uint32_t DecRefCount() noexcept(true)
			{
				AdAssertWithMessage(GetRefCount() != 0, "BSHandleRefObject - DecRefCount called with refcount already 0.");

				return ((NiRefObject*)this)->DecRefCount();
			}

			[[nodiscard]] inline uint32_t GetRefCount() const noexcept(true)
			{
				return refCount & REF_COUNT_MASK;
			}

			inline void SetHandleEntryIndex(uint32_t HandleIndex) noexcept(true)
			{
				refCount = static_cast<uint32_t>(((uint64_t)HandleIndex << HANDLE_BIT_INDEX) |
					(1ull << ACTIVE_BIT_INDEX) | GetRefCount());
			}

			[[nodiscard]] inline uint32_t GetHandleEntryIndex() const noexcept(true)
			{
				return static_cast<uint32_t>(refCount) >> HANDLE_BIT_INDEX;
			}

			inline void ClearHandleEntryIndex() noexcept(true)
			{
				refCount &= REF_COUNT_MASK;
			}

			[[nodiscard]] inline bool IsHandleValid() const noexcept(true)
			{
				return (static_cast<uint32_t>(refCount) & (1ull << ACTIVE_BIT_INDEX)) != 0;
			}
		};
		static_assert(sizeof(BSHandleRefObject<10>) == 0x10);

		typedef BSHandleRefObject<10>	BSHandleRefObject_Original;
		typedef BSHandleRefObject<8>	BSHandleRefObject_Extremly;

		// vanilla --- class BSUntypedPointerHandle<21,5>
		template<typename _Ty, int IndexBits = 21, int AgeCountBits = 5>
		class IBSUntypedPointerHandle
		{
		public:
			// NOTE: Handle index bits increased from 21 (vanilla)
			//
			// 27       26       21                   0
			// |--------|--------|--------------------|
			// | Active |    Age |    Handle Index    |
			// |--------|--------|--------------------|
			//
			constexpr static _Ty INDEX_BIT			= IndexBits;
			constexpr static _Ty AGE_BIT			= AgeCountBits;
			constexpr static _Ty UNUSED_BIT_START	= INDEX_BIT + AGE_BIT;								// 26 in vanilla

			constexpr static _Ty MASK_INDEX_BIT		= (((_Ty)1) << INDEX_BIT) - ((_Ty)1);				// 03FFFFFF
			constexpr static _Ty MASK_AGE_BIT		= ((((_Ty)1) << AGE_BIT) - ((_Ty)1)) << INDEX_BIT;	// 7C000000
			constexpr static _Ty MASK_ACTIVE_BIT	= ((_Ty)1) << UNUSED_BIT_START;						// 80000000
			constexpr static _Ty MAX_HANDLE_COUNT	= ((_Ty)1) << INDEX_BIT;							// 04000000
		protected:
			_Ty m_Bits;
		public:
			inline void SetBitwiseNull() noexcept { m_Bits = 0; }
			[[nodiscard]] inline bool IsBitwiseNull() const noexcept { return !m_Bits; }
			[[nodiscard]] inline _Ty GetIndex() const noexcept { return m_Bits & MASK_INDEX_BIT; }
			[[nodiscard]] inline _Ty GetAge() const noexcept { return m_Bits & MASK_AGE_BIT; }
			inline void SetInUse() noexcept { m_Bits |= MASK_ACTIVE_BIT; }
			inline void SetNotInUse() noexcept { m_Bits &= ~MASK_ACTIVE_BIT; }
			[[nodiscard]] inline bool IsInUse() const noexcept { return (m_Bits & MASK_ACTIVE_BIT) != 0; }
			inline void IncrementAge() noexcept
			{
				m_Bits = ((m_Bits << INDEX_BIT) & MASK_AGE_BIT) | (m_Bits & ~MASK_AGE_BIT);
			}

			void Set(_Ty Index, _Ty Age) noexcept
			{
				AdAssertWithMessage(Index < MAX_HANDLE_COUNT, "BSUntypedPointerHandle::Set - parameter Index is too large");

				m_Bits = Index | Age;
			}

			void SetIndex(_Ty Index) noexcept
			{
				AdAssertWithMessage(Index < MAX_HANDLE_COUNT, "BSUntypedPointerHandle::Set - parameter Index is too large");

				m_Bits = (Index & MASK_INDEX_BIT) | (m_Bits & ~MASK_INDEX_BIT);
			}


			IBSUntypedPointerHandle& operator=(const IBSUntypedPointerHandle& Other) noexcept
			{
				m_Bits = Other.m_Bits;
				return *this;
			}

			bool operator==(const IBSUntypedPointerHandle& Other) const noexcept
			{
				return m_Bits == Other.m_Bits;
			}

			bool operator!=(const IBSUntypedPointerHandle& Other) const noexcept
			{
				return m_Bits != Other.m_Bits;
			}

			IBSUntypedPointerHandle() : m_Bits(0) {}
		};
		static_assert(sizeof(IBSUntypedPointerHandle<uint32_t>) == 0x4);

		typedef IBSUntypedPointerHandle<uint32_t, 21, 5> BSUntypedPointerHandle_Original;
		typedef IBSUntypedPointerHandle<uint32_t, 23, 5> BSUntypedPointerHandle_Extended;

		template<typename _Ty, typename HandleType, typename HandleRef>
		class IBSPointerHandleManagerEntry : public HandleType
		{
		private:
			RE::NiPointer<HandleRef> m_Pointer;
		public:
			inline void SetNextFreeEntry(_Ty Index) noexcept
			{
				HandleType::m_Bits = (Index & HandleType::MASK_INDEX_BIT) | (HandleType::m_Bits & ~HandleType::MASK_INDEX_BIT);
			}

			[[nodiscard]] inline _Ty GetNextFreeEntry() const noexcept
			{
				return HandleType::m_Bits & HandleType::MASK_INDEX_BIT;
			}

			inline void SetPointer(HandleRef* Pointer) noexcept { m_Pointer = Pointer; }
			[[nodiscard]] inline HandleRef* GetPointer() const noexcept { return m_Pointer.get(); }

			[[nodiscard]] inline bool IsValid(_Ty Age) const noexcept
			{
				return HandleType::IsInUse() && HandleType::GetAge() == Age;
			}
		};

		typedef IBSPointerHandleManagerEntry<uint32_t, BSUntypedPointerHandle_Original,
			BSHandleRefObject_Original> BSPointerHandleManagerEntry_Original;
		typedef IBSPointerHandleManagerEntry<uint32_t, BSUntypedPointerHandle_Extended,
			BSHandleRefObject_Extremly> BSPointerHandleManagerEntry_Extended;

		static_assert(sizeof(BSPointerHandleManagerEntry_Original) == 0x10);
		static_assert(sizeof(BSPointerHandleManagerEntry_Extended) == 0x10);

		inline static uint32_t* data1{ nullptr };
		inline static uint32_t* data2{ nullptr };
		inline static RE::BSReadWriteLock* handleManagerLock{ nullptr };

		template<typename _Ty, typename HandleType, typename HandleRef>
		class IBSPointerHandleManager
		{
		protected:
			inline static _Ty FreeListHead;
			inline static _Ty FreeListTail;
			inline static RE::BSReadWriteLock HandleManagerLock;
			inline static std::vector<IBSPointerHandleManagerEntry<_Ty, HandleType, HandleRef>> HandleEntries;
			inline const static HandleType NullHandle;
		public:
			constexpr static _Ty INVALID_INDEX = (_Ty)-1;
		public:
			[[nodiscard]] inline static _Ty GetHead() noexcept { return data1 ? *data1 : FreeListHead; }
			[[nodiscard]] inline static _Ty GetTail() noexcept { return data2 ? *data2 : FreeListTail; }

			static IBSPointerHandleManagerEntry<_Ty, HandleType, HandleRef>* InitSDM()
			{
				HandleEntries.resize(HandleType::MAX_HANDLE_COUNT);
				// Reset
				CleanSDM();
				// Return address
				return HandleEntries.data();
			}

			static void CleanSDM()
			{
				FreeListHead = 0;
				if (data1)
					*data1 = 0;

				for (_Ty i = 0; i < HandleType::MAX_HANDLE_COUNT; i++)
				{
					if ((i + 1) >= HandleType::MAX_HANDLE_COUNT)
						HandleEntries[i].SetNextFreeEntry(static_cast<uint32_t>(i));
					else
						HandleEntries[i].SetNextFreeEntry(static_cast<uint32_t>(i + 1));
				}

				FreeListTail = HandleType::MAX_HANDLE_COUNT - 1;
				if (data2)
					*data2 = static_cast<uint32_t>(HandleType::MAX_HANDLE_COUNT - 1);
			}

			static void KillSDM()
			{
				RE::BSAutoWriteLock guard{ handleManagerLock ? *handleManagerLock : HandleManagerLock };

				for (_Ty i = 0; i < HandleType::MAX_HANDLE_COUNT; i++)
				{
					auto& arrayHandle = HandleEntries[i];

					if (!arrayHandle.IsInUse())
						continue;

					if (arrayHandle.GetPointer())
						arrayHandle.GetPointer()->ClearHandleEntryIndex();

					arrayHandle.SetPointer(nullptr);
					arrayHandle.SetNotInUse();

					const auto tail = data2 ? *data2 : static_cast<uint32_t>(FreeListTail);
					if (tail == UINT32_MAX)
					{
						FreeListHead = i;
						if (data1)
							*data1 = static_cast<uint32_t>(i);
					}
					else
					{
						HandleEntries[tail].SetNextFreeEntry(static_cast<uint32_t>(i));
					}

					arrayHandle.SetNextFreeEntry(static_cast<uint32_t>(i));
					FreeListTail = i;
					if (data2)
						*data2 = static_cast<uint32_t>(i);
				}
			}
		};

		typedef IBSPointerHandleManager<uint32_t, BSUntypedPointerHandle_Original,
			BSHandleRefObject_Original> BSPointerHandleManager_Original;
		typedef IBSPointerHandleManager<uint64_t, BSUntypedPointerHandle_Extended,
			BSHandleRefObject_Extremly> BSPointerHandleManager_Extended;

		template<typename Manager>
		class IBSHandleManager : public Manager
		{
		public:
			static void KillSDM()
			{
				Manager::KillSDM();
			}

			static void WarnForUndestroyedHandles()
			{
				AdAssertWithMessage(false, "Unimplemented");
			}
		};

		typedef IBSHandleManager<BSPointerHandleManager_Original> HandleManager_Original;
		typedef IBSHandleManager<BSPointerHandleManager_Extended> HandleManager_Extended;

		template<typename ObjectType, typename HandleType, typename Manager>
		class IBSPointerHandleManagerInterface : public Manager
		{
		public:
			static HandleType GetCurrentHandle(ObjectType* Refr) noexcept
			{
				HandleType untypedHandle{};

				if (Refr && Refr->IsHandleValid())
				{
					auto& handle = Manager::HandleEntries[Refr->GetHandleEntryIndex()];
					untypedHandle.Set(Refr->GetHandleEntryIndex(), handle.GetAge());

					AdAssertWithMessage(untypedHandle.GetAge() == handle.GetAge(), "BSPointerHandleManagerInterface::GetCurrentHandle - Handle already exists but has wrong age!");
				}

				return untypedHandle;
			}

			static HandleType CreateHandle(ObjectType* Refr) noexcept
			{
				HandleType untypedHandle{};

				if (!Refr)
					return untypedHandle;

				// Shortcut: Check if the handle is already valid
				untypedHandle = GetCurrentHandle(Refr);

				if (untypedHandle != Manager::NullHandle)
					return untypedHandle;

				// Wasn't present. Acquire lock and add it (unless someone else inserted it in the meantime)
				RE::BSAutoWriteLock guard{ Manager::HandleManagerLock };
				{
					untypedHandle = GetCurrentHandle(Refr);

					if (untypedHandle == Manager::NullHandle)
					{
						if (Manager::FreeListHead == Manager::INVALID_INDEX)
						{
							untypedHandle.SetBitwiseNull();
							
							// TODO: need rework ctd in case
							//AdAssert(false, "OUT OF HANDLE ARRAY ENTRIES. Null handle created for pointer 0x%p.", Refr);
						}
						else
						{
							auto& newHandle = Manager::HandleEntries[Manager::FreeListHead];
							newHandle.IncrementAge();
							newHandle.SetInUse();

							untypedHandle.Set(newHandle.GetNextFreeEntry(), newHandle.GetAge());
							newHandle.SetPointer(Refr);

							Refr->SetHandleEntryIndex(Manager::FreeListHead);
							AdAssert(Refr->GetHandleEntryIndex() == Manager::FreeListHead);

							if (newHandle.GetNextFreeEntry() == Manager::FreeListHead)
							{
								// Table reached the maximum count
								AdAssert(Manager::FreeListHead == Manager::FreeListTail);

								Manager::FreeListHead = Manager::INVALID_INDEX;
								Manager::FreeListTail = Manager::INVALID_INDEX;
							}
							else
							{
								AdAssert(Manager::FreeListHead != Manager::FreeListTail);
								Manager::FreeListHead = newHandle.GetNextFreeEntry();
							}
						}
					}
				}

				return untypedHandle;
			}

			static void Destroy1(const HandleType& Handle)
			{
				if (Handle.IsBitwiseNull())
					return;

				RE::BSAutoWriteLock guard{ Manager::HandleManagerLock };
				{
					const auto handleIndex = Handle.GetIndex();
					auto& arrayHandle = Manager::HandleEntries[handleIndex];

					if (arrayHandle.IsValid(Handle.GetAge()))
					{
						arrayHandle.GetPointer()->ClearHandleEntryIndex();
						arrayHandle.SetPointer(nullptr);
						arrayHandle.SetNotInUse();

						if (Manager::FreeListTail == Manager::INVALID_INDEX)
							Manager::FreeListHead = handleIndex;
						else
							Manager::HandleEntries[Manager::FreeListTail].SetNextFreeEntry(handleIndex);

						arrayHandle.SetNextFreeEntry(handleIndex);
						Manager::FreeListTail = handleIndex;
					}
				}
			}

			static void Destroy2(HandleType& Handle)
			{
				if (Handle.IsBitwiseNull())
					return;

				RE::BSAutoWriteLock guard{ Manager::HandleManagerLock };
				{
					const auto handleIndex = Handle.GetIndex();
					auto& arrayHandle = Manager::HandleEntries[handleIndex];

					if (arrayHandle.IsValid(Handle.GetAge()))
					{
						arrayHandle.GetPointer()->ClearHandleEntryIndex();
						arrayHandle.SetPointer(nullptr);
						arrayHandle.SetNotInUse();

						if (Manager::FreeListTail == Manager::INVALID_INDEX)
							Manager::FreeListHead = handleIndex;
						else
							Manager::HandleEntries[Manager::FreeListTail].SetNextFreeEntry(handleIndex);

						arrayHandle.SetNextFreeEntry(handleIndex);
						Manager::FreeListTail = handleIndex;
					}

					// Identical to Destroy1 except for this Handle.SetBitwiseNull();
					Handle.SetBitwiseNull();
				}
			}

			static bool GetSmartPointer1(const HandleType& Handle, RE::NiPointer<ObjectType>& Out)
			{
				if (Handle.IsBitwiseNull())
				{
					Out = nullptr;
					return false;
				}

				const auto handleIndex = Handle.GetIndex();
				auto& arrayHandle = Manager::HandleEntries[handleIndex];

				Out = static_cast<ObjectType*>(arrayHandle.GetPointer());

				if (!arrayHandle.IsValid(Handle.GetAge()) || Out->GetHandleEntryIndex() != handleIndex)
					Out = nullptr;

				return Out != nullptr;
			}

			static bool GetSmartPointer2(HandleType& Handle, RE::NiPointer<ObjectType>& Out)
			{
				if (Handle.IsBitwiseNull())
				{
					Out = nullptr;
					return false;
				}

				const auto handleIndex = Handle.GetIndex();
				auto& arrayHandle = Manager::HandleEntries[handleIndex];

				Out = static_cast<ObjectType*>(arrayHandle.GetPointer());

				if (!arrayHandle.IsValid(Handle.GetAge()) || Out->GetHandleEntryIndex() != handleIndex)
				{
					// Identical to GetSmartPointer1 except for this Handle.SetBitwiseNull();
					Handle.SetBitwiseNull();
					Out = nullptr;
				}

				return Out != nullptr;
			}

			static ObjectType* GetPointer(std::uint32_t UniqueId)
			{
				const auto handleIndex = UniqueId & HandleType::MASK_INDEX_BIT;
				auto& arrayHandle = Manager::HandleEntries[handleIndex];
				auto Out = static_cast<ObjectType*>(arrayHandle.GetPointer());

				if (Out->GetHandleEntryIndex() != handleIndex)
					Out = nullptr;

				return Out;
			}

			static bool IsValid(const HandleType& Handle)
			{
				const auto handleIndex = Handle.GetIndex();
				auto& arrayHandle = Manager::HandleEntries[handleIndex];

				// Handle.IsBitwiseNull(); -- This if() is optimized away because the result is irrelevant

				if (!arrayHandle.IsValid(Handle.GetAge()))
					return false;

				return arrayHandle.GetPointer()->GetHandleEntryIndex() == handleIndex;
			}
		};

		typedef IBSPointerHandleManagerInterface<RE::TESObjectREFR,
			BSUntypedPointerHandle_Original, HandleManager_Original>
			BSPointerHandleManagerInterface_Original;
		typedef IBSPointerHandleManagerInterface<RE::TESObjectREFR,
			BSUntypedPointerHandle_Extended, HandleManager_Extended>
			BSPointerHandleManagerInterface_Extended;

		static std::vector<std::uintptr_t> FindsByMask(uintptr_t start_address, uintptr_t max_size,
			const char* mask) noexcept(true)
		{
			std::vector<uintptr_t> results;
			std::vector<std::pair<uint8_t, bool>> pattern;

			for (size_t i = 0; i < strlen(mask);)
			{
				if (mask[i] != '?')
				{
					pattern.emplace_back((uint8_t)strtoul(&mask[i], nullptr, 16), false);
					i += 3;
				}
				else
				{
					pattern.emplace_back(0x00, true);
					i += 2;
				}
			}

			const uint8_t* dataStart = (uint8_t*)start_address;
			const uint8_t* dataEnd = (uint8_t*)start_address + max_size + 1;

			for (const uint8_t* i = dataStart;;)
			{
				auto ret = std::search(i, dataEnd, pattern.begin(), pattern.end(),
					[](uint8_t CurrentByte, std::pair<uint8_t, bool>& Pattern) {
						return Pattern.second || (CurrentByte == Pattern.first);
					});

				if (ret == dataEnd)
					break;

				uintptr_t addr = std::distance(dataStart, ret) + start_address;
				results.push_back(addr);

				i = (uint8_t*)(addr + 1);
			}

			return results;
		}

		std::atomic_bool extendedMode{ true };
	}

	ModuleRefExtended::ModuleRefExtended() :
		Module("Ref BSPointerHandle Extended", &bPatchesRefExtended)
	{}

	bool ModuleRefExtended::DoQuery() const noexcept
	{
		// No supports OG/NG
		return RELEX::IsRuntimeAE();
	}

	bool ModuleRefExtended::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto winMain = REL::Relocation(REL::ID(2228907));
		auto handleTable = REL::Relocation(REL::ID(4796008));
		auto createHandle = REL::Relocation(RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle);

		if (!winMain || !handleTable || !createHandle)
			return false;

		auto winMainAddr = winMain.get();
		auto handleTableAddr = handleTable.get();
		auto createHandleAddr = createHandle.get();

		auto data1 = REL::Relocation(REL::ID(4796005));
		auto data2 = REL::Relocation(REL::ID(4796006));

		if (!data1 || !data2)
			return false;

		RefExtendedExtendedDetail::data1 = (uint32_t*)data1.get();
		RefExtendedExtendedDetail::data2 = (uint32_t*)data2.get();
		RefExtendedExtendedDetail::handleManagerLock = reinterpret_cast<RE::BSReadWriteLock*>(RefExtendedExtendedDetail::data2 + 1);

		auto addr = winMainAddr + 0x6D1;

		// Preparation, removal of all embedded pieces of code
		RELEX::WriteSafeNop(addr + 12, 0x7C);
		
		auto WriteMovFromRax = [](uintptr_t rav_from, uintptr_t rav_to) noexcept(true)
			{
				if (!rav_from || !rav_to) return;

				RELEX::WriteSafe(rav_from, { 0x48, 0x89, 0x05 });
				auto RelOff = static_cast<uint32_t>(rav_to - (rav_from + 7));
				REL::WriteSafe(rav_from + 3, reinterpret_cast<uint8_t*>(std::addressof(RelOff)), 4);
			};
		WriteMovFromRax(addr + 5, handleTableAddr);

		if (RefExtendedExtendedDetail::extendedMode.load())
		{
			RELEX::DetourCall(addr, reinterpret_cast<uintptr_t>(std::addressof(RefExtendedExtendedDetail::BSPointerHandleManager_Extended::InitSDM)));
			RELEX::DetourCall(winMainAddr + 0x2031, reinterpret_cast<uintptr_t>(std::addressof(RefExtendedExtendedDetail::BSPointerHandleManager_Extended::KillSDM)));

			// Cutting a lot is faster this way
			auto textRange = REX::FModule::GetExecutingModule().GetSection(".text");
			
			REX::DEBUG("Debug: textRange {:X} {:X}", textRange.GetAddress(), textRange.GetSize());
			RELEX::ScopeLock safeEditor(textRange.GetPointer(), textRange.GetSize());

			// Specify the size
			memcpy(reinterpret_cast<void*>(addr + 0xA1), std::addressof(RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT), 4);

			auto __InstallPatchByPatternMask = [&textRange](
				const char* pattern_mask,	// pattern "? ? 00 E0 03"
				size_t offset,				// offset from find patterns array
				size_t count,				// count need change
				size_t offset_find,			// offset from in once find pattern
				const void* source,			// buffer
				size_t ssize				// buffer size
				) -> size_t {
					auto patterns = RefExtendedExtendedDetail::FindsByMask(textRange.GetAddress(), textRange.GetSize(), pattern_mask);
					if (patterns.size() > offset)
					{
						size_t max = std::min(patterns.size(), count + offset);
						for (size_t index = offset; index < max; index++)
							memcpy((void*)(patterns[index] + offset_find), source, ssize);

						return max - offset;
					}

					return 0;
				};

			

			auto __InstallPatchByPatternMaskCustom = [&textRange](
				const char* pattern_mask,			// pattern "? ? 00 E0 03"
				size_t offset,						// offset from find patterns array
				size_t count,						// count need change
				bool(callback)(uintptr_t addr)		// callback function
				) -> size_t {
					auto patterns = RefExtendedExtendedDetail::FindsByMask(textRange.GetAddress(), textRange.GetSize(), pattern_mask);
					if (patterns.size() > offset)
					{
						size_t max = std::min(patterns.size(), count + offset);
						size_t total = 0;
						for (size_t index = offset; index < max; index++)
						{
							if (callback(patterns[index]))
								total++;
						}

						return total;
					}

					return 0;
				};

			auto __InstallPatchByPatternMaskEx = [&textRange](
				const char* pattern_mask,					// pattern "? ? 00 E0 03"
				std::size_t offset,							// offset from find patterns array
				std::size_t count,							// count need change
				std::size_t offset_find,					// offset from in once find pattern
				const void* source,							// buffer
				std::size_t ssize,							// buffer size
				std::initializer_list<std::size_t> excludes	// list exclude indexes
				) -> std::size_t {
					auto patterns = RefExtendedExtendedDetail::FindsByMask(textRange.GetAddress(), textRange.GetSize(), pattern_mask);
					if (patterns.size() > offset)
					{
						std::size_t max = std::min(patterns.size(), count + offset);
						std::size_t total = 0;
						for (std::size_t index = offset; index < max; index++)
							if (std::find(excludes.begin(), excludes.end(), index) == std::end(excludes))
							{
								memcpy((void*)(patterns[index] + offset_find), source, ssize);
								total++;
							}

						return total;
					}

					return 0;
				};

			size_t total = 0;

			// Change HANDLE_BIT_INDEX
			std::uint8_t bit_byte = (std::uint8_t)RefExtendedExtendedDetail::BSHandleRefObject_Extremly::HANDLE_BIT_INDEX;
			total = __InstallPatchByPatternMask("C1 ? 0B 3B", 0, -1, 2, &bit_byte, 1);
			total += __InstallPatchByPatternMask("C1 ? 0B 41 3B", 0, -1, 2, &bit_byte, 1);
			//REX::INFO(" HANDLE_BIT_INDEX {} "sv, total);

			// == REF_COUNT_MASK
			total = __InstallPatchByPatternMaskCustom("F7 ? ? FF 03 00 00", 0, -1, [](std::uintptr_t addr) -> bool
				{
					if ((*(std::uint8_t*)(addr + 2) != 0x28) && (*(std::uint8_t*)(addr + 2) != 0x8)) return false;
					memcpy((std::uint8_t*)(addr + 3), &RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);
					return true;
				});
			total += __InstallPatchByPatternMaskCustom("81 ? ? FF 03 00 00", 0, -1, [](std::uintptr_t addr) -> bool
				{
					if ((*(std::uint8_t*)(addr + 2) != 0x28) && (*(std::uint8_t*)(addr + 2) != 0x8)) return false;
					memcpy((std::uint8_t*)(addr + 3), &RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);
					return true;
				});
			total += __InstallPatchByPatternMask("FF C8 A9 FF 03 00 00", 0, -1, 3,
				&RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);
			total += __InstallPatchByPatternMask("28 F7 ? FF 03 00 00", 0, -1, 3,
				&RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);

			// Fix CreateHandle's
			auto shift1 = static_cast<uint8_t>(RefExtendedExtendedDetail::BSHandleRefObject_Extremly::ACTIVE_BIT_INDEX);
			auto shift2 = static_cast<uint8_t>(RefExtendedExtendedDetail::BSHandleRefObject_Extremly::HANDLE_BIT_INDEX);

			auto addr = RE::ID::BSPointerHandle::BSPointerHandleManagerInterface::CreateHandle.address();
			memcpy((void*)(addr + 0x2A), &shift1, 1);
			memcpy((void*)(addr + 0x3A), &shift2, 1);							 
			memcpy((void*)(addr + 0x6D), &shift1, 1);
			memcpy((void*)(addr + 0x7C), &shift2, 1);
			memcpy((void*)(addr + 0xC5), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x133), &shift2, 1);
			memcpy((void*)(addr + 0x137), &shift1, 1);

			addr = REL::ID(2234041).address();
			memcpy((void*)(addr + 0x10B), &shift2, 1);
			memcpy((void*)(addr + 0x1C8), &shift1, 1);
			memcpy((void*)(addr + 0x1DA), &shift2, 1);
			memcpy((void*)(addr + 0x210), &shift1, 1);
			memcpy((void*)(addr + 0x21F), &shift2, 1);
			memcpy((void*)(addr + 0x26F), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x2B1), &shift2, 1);
			memcpy((void*)(addr + 0x2B5), &shift1, 1);

			addr = REL::ID(2232227).address();
			memcpy((void*)(addr + 0x2B), &shift1, 1);
			memcpy((void*)(addr + 0x3C), &shift2, 1);
			memcpy((void*)(addr + 0x71), &shift1, 1);
			memcpy((void*)(addr + 0x80), &shift2, 1);
			memcpy((void*)(addr + 0xCF), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x10F), &shift2, 1);
			memcpy((void*)(addr + 0x113), &shift1, 1);

			addr = REL::ID(2232134).address();
			memcpy((void*)(addr + 0x194), &shift1, 1);
			memcpy((void*)(addr + 0x1A3), &shift2, 1);
			memcpy((void*)(addr + 0x1D7), &shift1, 1);
			memcpy((void*)(addr + 0x1E6), &shift2, 1);
			memcpy((void*)(addr + 0x233), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x274), &shift2, 1);
			memcpy((void*)(addr + 0x278), &shift1, 1);
			memcpy((void*)(addr + 0x375), &shift2, 1);

			addr = REL::ID(2229084).address();
			memcpy((void*)(addr + 0xFA), &shift1, 1);
			memcpy((void*)(addr + 0x101), &shift2, 1);
			memcpy((void*)(addr + 0x13B), &shift1, 1);
			memcpy((void*)(addr + 0x142), &shift2, 1);
			memcpy((void*)(addr + 0x194), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x1D2), &shift2, 1);
			memcpy((void*)(addr + 0x1D6), &shift1, 1);

			addr = REL::ID(2200771).address();
			memcpy((void*)(addr + 0xC2), &shift1, 1);
			memcpy((void*)(addr + 0xD0), &shift2, 1);
			memcpy((void*)(addr + 0x102), &shift1, 1);
			memcpy((void*)(addr + 0x110), &shift2, 1);
			memcpy((void*)(addr + 0x158), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x195), &shift2, 1);
			memcpy((void*)(addr + 0x199), &shift1, 1);

			addr = REL::ID(2190460).address();
			memcpy((void*)(addr + 0xDC), &shift1, 1);
			memcpy((void*)(addr + 0xEE), &shift2, 1);
			memcpy((void*)(addr + 0x124), &shift1, 1);
			memcpy((void*)(addr + 0x133), &shift2, 1);
			memcpy((void*)(addr + 0x183), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			memcpy((void*)(addr + 0x1C5), &shift2, 1);
			memcpy((void*)(addr + 0x1C9), &shift1, 1);

			auto others = { 2188675, 2226228, 2228633, 2236714, 2188676 };
			for (auto other : others)
			{
				addr = REL::ID(other).address();
				memcpy((void*)(addr + 0x4C), &shift1, 1);
				memcpy((void*)(addr + 0x5B), &shift2, 1);
				memcpy((void*)(addr + 0x8E), &shift1, 1);
				memcpy((void*)(addr + 0x9D), &shift2, 1);
				memcpy((void*)(addr + 0xEC), &RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
				memcpy((void*)(addr + 0x12C), &shift2, 1);
				memcpy((void*)(addr + 0x130), &shift1, 1);
			}

			addr = REL::ID(2248131).address();
			memcpy((void*)(addr + 0x9E), &shift1, 1);
			memcpy((void*)(addr + 0xB5), &shift2, 1);

			addr = REL::ID(2248134).address();
			memcpy((void*)(addr + 0x8C), &shift1, 1);
			memcpy((void*)(addr + 0xA3), &shift2, 1);

			total += __InstallPatchByPatternMask("FF ? F7 ? FF 03 00 00", 0, -1, 4,
				&RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);
			total += __InstallPatchByPatternMask("41 FF ? 41 F7 ? FF 03 00 00", 0, -1, 6,
				&RefExtendedExtendedDetail::BSHandleRefObject_Extremly::REF_COUNT_MASK, 4);
			
			//REX::INFO(" REF_COUNT_MASK {} "sv, total);

			// Change AGE
			total = __InstallPatchByPatternMask("A9 00 00 E0 03", 0, -1, 1,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_AGE_BIT, 4);
			total += __InstallPatchByPatternMask("F7 ? 00 00 E0 03", 0, -1, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_AGE_BIT, 4);
			total += __InstallPatchByPatternMask("25 00 00 E0 03", 0, -1, 1,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_AGE_BIT, 4);
			total += __InstallPatchByPatternMask("81 ? 00 00 E0 03", 0, -1, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_AGE_BIT, 4);

			//REX::INFO(" AGE {} "sv, total);

			// Change INDEX
			total = __InstallPatchByPatternMask("41 81 ? FF FF 1F 00", 0, 1544, 3,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4);
			total += __InstallPatchByPatternMask("81 E1 FF FF 1F 00", 1, 1, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4);
			total += __InstallPatchByPatternMask("81 E5 FF FF 1F 00", 1, 100, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4);
			total += __InstallPatchByPatternMaskEx("81 E6 FF FF 1F 00", 5, 100, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4, { 7, 10, 11, 12, 13, 17 });
			total += __InstallPatchByPatternMaskEx("81 E2 FF FF 1F 00", 0, 31, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4, { 6, 11 });
			total += __InstallPatchByPatternMask("81 E7 FF FF 1F 00", 6, 8, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4);
			total += __InstallPatchByPatternMask("81 E3 FF FF 1F 00", 0, -1, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT, 4);
			//REX::INFO(" INDEX {} "sv, total);

			// Change NOT MASK_INDEX_BIT
			std::uint32_t not_mask = ~RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_INDEX_BIT;
			total = __InstallPatchByPatternMask("81 ? 00 00 E0 FF", 0, 38, 2, &not_mask, 4);
			//REX::INFO(" NOT MASK_INDEX_BIT {} "sv, total);

			// Change NOT MASK_AGE_BIT
			not_mask = ~RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_AGE_BIT;
			total = __InstallPatchByPatternMask("25 FF FF 1F FC", 0, 38, 1, &not_mask, 4);
			//REX::INFO(" NOT MASK_AGE_BIT {} "sv, total);
		
			// Change NOT MASK_ACTIVE_BIT
			not_mask = ~RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_ACTIVE_BIT;
			total = __InstallPatchByPatternMask("81 ? FF FF FF FB", 0, -1, 2, &not_mask, 4);
			//REX::INFO(" NOT MASK_ACTIVE_BIT {} "sv, total);

			// Change UNUSED_BIT_START
			total = __InstallPatchByPatternMask("0F BA E0 1A", 0, 1500, 3,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::UNUSED_BIT_START, 1);
			total += __InstallPatchByPatternMaskEx("0F BA ? 1A", 1, 124, 3,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::UNUSED_BIT_START, 1,
				{ 19, 20, 28, 40, 66, 67, 70, 71, 90, 91, 92, 93, 94, 97, 98 });
			//REX::INFO(" UNUSED_BIT_START {} "sv, total);

			// Change MAX_HANDLE_COUNT
			total = __InstallPatchByPatternMask("81 ? 00 00 20 00", 0, 1, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MAX_HANDLE_COUNT, 4);
			//REX::INFO(" MAX_HANDLE_COUNT {} "sv, total);

			// Change MASK_ACTIVE_BIT
			total = __InstallPatchByPatternMask("F7 ? 00 00 00 04", 0, 2, 2,
				&RefExtendedExtendedDetail::BSUntypedPointerHandle_Extended::MASK_ACTIVE_BIT, 4);
			//REX::INFO(" MASK_ACTIVE_BIT {} "sv, total);
		}
		else
		{
			RELEX::DetourCall(addr, reinterpret_cast<std::uintptr_t>(std::addressof(RefExtendedExtendedDetail::BSPointerHandleManager_Original::InitSDM)));
			RELEX::DetourCall(winMainAddr + 0x2031, reinterpret_cast<std::uintptr_t>(std::addressof(RefExtendedExtendedDetail::BSPointerHandleManager_Original::KillSDM)));
		}

		return true;
	}
}