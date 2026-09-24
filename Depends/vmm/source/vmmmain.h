// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#pragma once

#include "Voltek.MemoryManager.h"
#include "vbase.h"
#include "vmapper.h"
#include "vmmblock.h"
#include "vsimplelock.h"
#include <array>
#include <stddef.h>
#include <thread>

namespace voltek
{
	namespace memory_manager
	{
		enum class pool_type : uint8_t
		{
			pool_8 = 0,
			pool_16,
			pool_32,
			pool_64,
			pool_128,
			pool_256,
			pool_512,
			pool_1024,
			pool_4096,
			pool_8192,
			pool_16384,
			pool_32768,
			pool_65536,
			pool_131072,
			MAX
		};

		// Blocks above the largest pool live in power-of-two slots from 256 KiB to 4 GiB.
		inline constexpr size_t large_slot_minimum = 256ull * 1024;
		inline constexpr size_t large_class_count = 15;

		// Менеджер памяти.
		class memory_manager : public voltek::core::base
		{
		public:
			// Конструктор по умолчанию.
			memory_manager();
			// Деструктор.
			virtual ~memory_manager();
			// Выделяет память требуемого размера.
			// Память всегда выровнена.
			// Вернёт nullptr, если память физически закончилась.
			// Также если размер требуемый объявлен как 0.
			[[nodiscard]] void* alloc(size_t size) noexcept;
			[[nodiscard]] void* aligned_alloc(size_t size, size_t alignment) noexcept;
			[[nodiscard]] void* aligned_realloc(const void* ptr, size_t size, size_t alignment) noexcept;
			// Выделяет память требуемого размера из предыдущего указателя на память.
			// Память всегда выровнена.
			// Вернёт nullptr, если память физически закончилась.
			// Также если размер требуемый объявлен как 0.
			// Адрес памяти может быть изменён.
			[[nodiscard]] void* realloc(const void* ptr, size_t size) noexcept;
			// Освобождает память.
			// Вернёт ложь, если указатель не пренадлежит менеджеру.
			bool free(const void* ptr) noexcept;
			// Возвращает размер выделенной памяти под указатель.
			// Вернёт 0, что значит ошибка.
			[[nodiscard]] size_t msize(const void* ptr) const noexcept;
			[[nodiscard]] bool ready() const noexcept { return zero_size_request_block && pools; }
			// Вывод дампа битовой карты указанного пула
			void dump_map(size_t pool_id, const char* filename) const noexcept;
			// Вывод дампа памяти указанного пула
			void dump(size_t pool_id, const char* filename) const noexcept;
		private:
			memory_manager(const memory_manager&) = delete;
			memory_manager(memory_manager&&) = delete;
			memory_manager& operator=(memory_manager&&) = delete;
			memory_manager& operator=(const memory_manager&) = delete;
		private:
			// Принадлежит ли указатель менеджеру: заголовок внутри зарезервированного диапазона и прошёл проверку.
			[[nodiscard]] static bool owns(const void* ptr) noexcept;
			[[nodiscard]] block_base* large_alloc(size_t size) noexcept;
			void large_free(block_base* block) noexcept;
		private:
			// Блок памяти, если запрашивают 0 размер.
			block_base* zero_size_request_block{ nullptr };
			// Источники страниц пулов и крупных блоков внутри зарезервированного диапазона.
			std::array<core::mapper, std::to_underlying(pool_type::MAX)> page_sources;
			std::array<core::mapper, large_class_count> large_sources;
			// Массив пулов.
			void** pools{ nullptr };
			friend void ::voltek::scalable_get_pool_stats(::voltek::scalable_pool_stats* out);
			// Блокировщик для работы с множеством потоков.
			//voltek::core::_internal::simple_lock lock;
			// События для потока кеширования, чтобы можно выйти
			void* event_close{ nullptr };
			void* event_close_w{ nullptr };
			// Поток для кеширования
			std::thread* thread{ nullptr };
		};

		// Глобальный менеджер памяти, который требует инициализации.
		extern memory_manager* global_memory_manager;
	}
}