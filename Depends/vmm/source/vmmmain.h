// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#pragma once

#include "vbase.h"
#include "vmmblock.h"
#include "vsimplelock.h"
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
			void* alloc(size_t size);
			// Выделяет память требуемого размера из предыдущего указателя на память.
			// Память всегда выровнена.
			// Вернёт nullptr, если память физически закончилась.
			// Также если размер требуемый объявлен как 0.
			// Адрес памяти может быть изменён.
			void* realloc(const void* ptr, size_t size);
			// Освобождает память.
			// Вернёт ложь, если указатель не пренадлежит менеджеру.
			bool free(const void* ptr);
			// Возвращает размер выделенной памяти под указатель.
			// Вернёт 0, что значит ошибка.
			size_t msize(const void* ptr) const;
			// Вывод дампа битовой карты указанного пула
			void dump_map(size_t pool_id, const char* filename) const;
			// Вывод дампа памяти указанного пула
			void dump(size_t pool_id, const char* filename) const;
		private:
			// Конструктор копий - НЕДОСТУПЕН.
			// Менеджер один и уникален.
			memory_manager(const memory_manager& ob);
			// Оператор присвоения - НЕДОСТУПЕН.
			// Менеджер один и уникален.
			memory_manager& operator=(const memory_manager& ob);
		private:
			// Блок памяти, если запрашивают 0 размер.
			block8_t zero_size_request_block;
			// Массив пулов.
			void** pools;
			// Блокировщик для работы с множеством потоков.
			voltek::core::_internal::simple_lock lock;
			// События для потока кеширования, чтобы можно выйти
			void* event_close;
			void* event_close_w;
			// Поток для кеширования
			std::thread* thread;
		};

		// Глобальный менеджер памяти, который требует инициализации.
		extern memory_manager* global_memory_manager;
	}
}