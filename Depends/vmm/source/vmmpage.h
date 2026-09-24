// Copyright © 2023 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#pragma once

#include "valloc.h"
#include "vassert.h"
#include "vmmgeometry.h"
#include "vmapper.h"
#include "vio.h"

namespace voltek
{
	namespace memory_manager
	{
		// Шаблонный класс страницы памяти
		// Является хранилищем блоков, но при этом нет жёсткой зависимости от каких блоков.
		// Служит только в качестве помечания свободный или занятый блок, а также быстрого
		// нахождения свободного блока, при помощи битовой карты.
		template<typename _type, typename _map = page_map<_type>>
		class page_t : public voltek::core::base
		{
		public:
			// Конструктор по умолчанию.
			page_t() : _blocks(nullptr), _size(0), _user_data(0), _mapper(nullptr)
			{}
			// Конструктор.
			// Внимание размер будет округлён до кратности 256.
			// Блоки берутся из слота карты, чтобы принадлежность проверялась по диапазону адресов.
			page_t(size_t new_size, voltek::core::mapper* mapper) : _blocks(nullptr), _size(0), _user_data(0), _mapper(mapper)
			{
				set_size(new_size);
			}
			// Деструктор
			virtual ~page_t()
			{
				map.clear();
				if (_blocks)
				{
					_mapper->release(_blocks);

					_blocks = nullptr;
					_size = 0;
				}
			}
			// Задаёт размер страницы.
			// Одноразовая, не для расширения страницы.
			// Внимание размер будет округлён до кратности 256.
			void set_size(size_t new_size)
			{
				// Число должно быть кратное 256, округляем в меньшую сторону.
				// Это необходимо, учитывая, что bits_regions размер минимум от 65536.
				// Использование SIMD инструкций является приоритетом, а "хвоста" должно 
				// быть немного, а лучше небыло вовсе.
				new_size = (new_size >> 8) << 8;

				map.clear();
				if (new_size && new_size > SIZE_MAX / sizeof(_type))
				{
					_vassert(!_blocks);
					return;
				}

				map.resize(new_size);
				if (map.count() != new_size)
				{
					_vassert(map.count() == new_size);
					return;
				}
				size_t alloc_size = new_size * sizeof(_type);

				_blocks = _mapper ? (_type*)_mapper->allocate(alloc_size) : nullptr;

				if (!_blocks)
				{
					map.clear();
					_vassert(!_blocks);
				}
				else
				{
					_size = new_size;
					// Размещаем везде единицы, 1 - свободных блок, 0 - занят.
					map.all_set();
				}
			}
			// Возвращает допольнительную информацию, что привязана к странице.
			inline uintptr_t get_user_data() const { return _user_data; }
			// Устанавливает дополнительную информацию к странице.
			inline void set_user_data(uintptr_t user_data) { _user_data = user_data; }
			// Возвращает истину, если страница не инициализирована.
			inline bool empty() const { return !_size; }
			// Возвращает истину, если все блоки свободны.
			inline bool is_all_blocks_free() const { return map.is_all_sets(); }
			// Возвращает истину, если все блоки заняты.
			inline bool is_all_blocks_busy() const { return map.is_all_unsets(); }
			// Возвращает истину, если блок за указанным индексом - свободен.
			inline bool is_block_free(size_t index) const { return map.is_set(index); }
			// Возвращает истину, если блок за указанным индексом - занят.
			inline bool is_block_busy(size_t index) const { return map.is_unset(index); }
			// Помечает, что данный блок за указанным индексом - свободен.
			inline bool set_block_free(size_t index) { return map.set(index); }
			// Помечает, что данный блок за указанным индексом - занят.
			inline bool set_block_busy(size_t index) { return map.unset(index); }
			// Возвращает истину, в случае, нахождения первого попавшегося свободного блока.
			// Его индекс будет передан в "index".
			inline bool get_first_free_block_index(size_t& index) { return map.find_first_set_bit(index); }
			// Возвращает кол-во допустимых блоков.
			inline size_t count() const { return _size; }
			// Возвращает кол-во свободных блоков.
			inline size_t free_count() const { return map.get_sets_count(); }
			// Возвращает кол-во знятых блоков.
			inline size_t busy_count() const { return map.get_unsets_count(); }
			// Возвращает блок за указанным индексом. Константа.
			inline const _type c_at(size_t index) const { return _blocks[index]; }
			// Возвращает блок за указанным индексом.
			inline _type& at(size_t index) { return _blocks[index]; }
			// Оператор обращения к объекту класса в качестве массива.
			// Возвращает блок за указанным индексом. Константа.
			inline const _type operator[](size_t index) const { return c_at(index); }
			// Оператор обращения к объекту класса в качестве массива.
			// Возвращает блок за указанным индексом.
			inline _type& operator[](size_t index) { return at(index); }
			// Вывод дампа битовой карты страницы в файл.
			inline void dump_map(const char* filename) const { map.dump(filename); }
			// Вывод дампа памяти страницы в файл.
			void dump(const char* filename) const 
			{ 
#ifndef VMMDLL_EXPORTS
				voltek::core::_internal::memory_to_file(filename, (void*)_blocks, 
					_size * sizeof(_type), _size >> 3);
#endif // !VMMDLL_EXPORTS
			}
		private:
			// Конструктор копий - НЕДОСТУПЕН.
			// Страница одна и уникальна.
			page_t(const page_t& page) : _blocks(nullptr), _size(0), _user_data(0), _mapper(nullptr)
			{}
			// Оператор присвоения - НЕДОСТУПЕН.
			// Страница одна и уникальна.
			page_t& operator=(const page_t& page)
			{}
		private:
			// Блоки.
			_type* _blocks;
			// Кол-во доступных блоков.
			size_t _size;
			// Дополнительная информация.
			uintptr_t _user_data;
			// Битовая карта.
			_map map;
			// Карта памяти.
			voltek::core::mapper* _mapper;
		};
	}
}