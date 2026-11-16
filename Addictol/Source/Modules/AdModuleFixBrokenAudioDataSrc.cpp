#include <Modules/AdModuleFixBrokenAudioDataSrc.h>
#include <Core/AdUtils.h>
#include <REL/REL.h>

#include <RE/N/NiPointer.h>
#include <RE/B/BSResource_AsyncStream.h>
#include <RE/B/BSTArray.h>
#include <RE/IDs.h>
#include <RE/IDs_RTTI.h>
#include <RE/IDs_VTABLE.h>

namespace Addictol
{
	enum class RiffMetaFlags : std::uint32_t
	{
		kXWMA		= 1 << 0, // 0x1 — «это xWMA-поток, нужен спец-разбор» (fmt tag 0x0161 и dpdsCount != 0)
		kFormValid	= 1 << 1, // 0x2 — форма 'XWAM'/'WAVE' уже подтверждена (кэш между вызовами)
		kFmtParsed	= 1 << 2, // 0x4 — fmt-чанк разобран
		kCuesParsed = 1 << 3, // cue-чанк разобран, поля +0x30/+0x34 заполнены
	};

	typedef struct tWAVEFORMATEX
	{
		uint16_t wFormatTag;
		uint16_t nChannels;
		uint32_t nSamplesPerSec;
		uint32_t nAvgBytesPerSec;
		uint16_t nBlockAlign;
		uint16_t wBitsPerSample;
		uint16_t cbSize;
	} WAVEFORMATEX, *PWAVEFORMATEX, *NPWAVEFORMATEX, *LPWAVEFORMATEX;

	typedef struct XMA2WAVEFORMATEX
	{
		WAVEFORMATEX	wfx;		// Базовый заголовок WAVEFORMATEX
		uint16_t		NumStreams;	// Количество аудиопотоков
	} XMA2WAVEFORMAT;
	static_assert(sizeof(XMA2WAVEFORMAT) == 0x18);

	constexpr auto WAVE_FORMAT_WMAUDIO2 = 0x0161;

	namespace RiffFile
	{
		struct RiffMetaData			// живёт по this+0x58
		{
			XMA2WAVEFORMAT* format;
			RE::BSTArray<uint32_t>	cuePoints;
			uint32_t				dataOffset;							// 0x20 — абсолютный оффсет данных ('data'/'smpl'-ветка)
			uint32_t				dataSize;							// 0x24 — размер данных (=[chunk.size])
			uint32_t				dpdsOffset;							// 0x28 — абсолютный оффсет 'dpds'-чанка
			uint32_t				dpdsCount;							// 0x2C — число записей dpds (= chunk.size >> 2)
			uint32_t				unk_30;								// 0x30 — dword из данных чанка ([data+0x2C])
			uint32_t				unk_34;								// 0x34 — dword из данных чанка ([data+0x30])
			REX::TEnumSet<RiffMetaFlags, std::uint32_t>
				flags;	// 0x38 — здесь сбрасывается bit0
		};
		static_assert(sizeof(RiffMetaData) == 0x40);

		struct RiffChunkHeader   // 8 байт
		{
			uint32_t id;    // 0x00 — FourCC: 'fmt ', 'data', 'smpl', 'dpds', 'cue ' ...
			uint32_t size;  // 0x04 — размер полезных данных чанка в байтах
		};
	}

	namespace
	{
		bool ParseChunk(RiffFile::RiffChunkHeader* a_chunk, const uint8_t* a_buffer,
			uint32_t a_size, uint32_t a_offset, RiffFile::RiffMetaData* a_riff)
		{
			using func_t = decltype(&ParseChunk);
			static REL::Relocation<func_t> func{ 2267709 };
			return func(a_chunk, a_buffer, a_size, a_offset, a_riff);
		}
	}

	class BSXAudio2DataSrc
	{
	public:
		static constexpr auto RTTI{ RE::RTTI::BSXAudio2DataSrc };
		static constexpr auto VTABLE{ RE::VTABLE::BSXAudio2DataSrc };

		virtual ~BSXAudio2DataSrc() = default;

		enum class State : uint32_t
		{
			kOpen = 1,
			kScanning,
			kDone,
		};

		uint32_t unk_08[2];
		RE::NiPointer<RE::BSResource::AsyncStream> srcStream;
		uint64_t unk_18;
		XMA2WAVEFORMATEX xma2format;
		uint64_t unk_30[4];
		RiffFile::RiffMetaData riffMetaData;
		uint64_t unk_98;
		uint32_t bufferSize;
		uint8_t* buffer;
		uint32_t readBytes;
		uint32_t totalBytes;
		uint8_t* dstBuffer;
		uint64_t unk_C0[2];
		uint8_t* doneBuffer;
		REX::TEnum<State> state;
	};
	static_assert(sizeof(BSXAudio2DataSrc) == 0xE0);

	namespace RiffFile
	{
		static void ScanRiff(const void* pData, uint32_t size, uint32_t baseOffset, 
			RiffMetaData& meta, uint32_t& outOffset) noexcept
		{
			bool bContinue = (pData != nullptr);						// setne r9b
			if (bContinue && meta.flags.none(RiffMetaFlags::kFormValid))
			{
				if (size <= 0x3C)										// 60 — минимальный валидный RIFF
					bContinue = false;
				else
				{
					uint32_t form = *(const uint32_t*)((const uint8_t*)pData + 8);
					if (form == 'XWAM' || form == 'WAVE')				// 0x414D5758 / 0x45564157
					{
						bContinue = true;
						meta.flags.set(RiffMetaFlags::kFormValid);      // запоминаем на будущее
					}
					else
						bContinue = false;
				}
			}

			if (!bContinue)
				return;

			uint32_t off = 0xC;                    // первый чанк после RIFF-заголовка
			while ((off + sizeof(RiffChunkHeader)) < size)
			{
				RiffChunkHeader* hdr = (RiffChunkHeader*)((const uint8_t*)pData + off);

				bool ok = ParseChunk(
					hdr,										// rcx
					(const uint8_t*)pData + off + 8,			// rdx — данные чанка
					size - off - 8,								// r8d — остаток буфера
					baseOffset + off + 8,						// r9d — абсолютный оффсет
					std::addressof(meta));						// [rsp+0x20]

				off += hdr->size;

				// ФИКС:
				// чанк не может занимать больше, чем осталось в буфере
				if (off > size)
				{
					off = 0;				// outOffset всегда <= size
					ok = false;				// испорченные данные — останавливаемся
				}

				if (!ok)
				{
					bContinue = false;
					break;
				}
			}
			
			outOffset = off;

			if (!bContinue)
			{
				meta.flags.set(false, RiffMetaFlags::kXWMA);
				return;
			}

			// Хвост: включить флаг xWMA
			if ((meta.flags.any(RiffMetaFlags::kFmtParsed)) && (meta.dpdsCount != 0))
			{
				if (meta.format->wfx.wFormatTag == WAVE_FORMAT_WMAUDIO2)     // xWMA format tag for FO4
					meta.flags.set(meta.dpdsOffset != 0, RiffMetaFlags::kXWMA);
				// tag != 0x0161 → выходим, флаги не трогаем (эпилог на AB8)
			}
			else
				meta.flags.set(false, RiffMetaFlags::kXWMA);
		}
	}

	ModuleFixBrokenAudioDataSrc::ModuleFixBrokenAudioDataSrc() :
		Module("Fix Broken AudioDataSrc", &bFixesBrokenAudioDataSrc)
	{}

	bool ModuleFixBrokenAudioDataSrc::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// When reading wrong audio format, overflow occurs and the negative 
		// value of available is required for reading, we block this.
		//
		// This will result in sound loss.

		auto relOff = REL::Relocation(RE::ID::BSXAudio2DataSrc::DoTestAsyncOpenReady);
		auto off = RELEX::IsRuntimeAE() ? 0x34A : 0x2A9;

		if (!RELEX::Validate(relOff.address() + off, { 0x0F, 0x86 }))
		{
			REX::WARN("Module ModuleFixBrokenAudioDataSrc return failed, no match validate code"sv);
			return false;
		}

		auto relOff2 = REL::Relocation(RE::ID::BSXAudio2DataSrc::DoQueuedSyncOpen);
		auto off2 = RELEX::IsRuntimeAE() ? 0x273 : 218;

		if (!RELEX::Validate(relOff2.address() + off2, { 0x0F, 0x86 }))
		{
			REX::WARN("Module ModuleFixBrokenAudioDataSrc return failed, no match validate code"sv);
			return false;
		}

		// sets as jle (so check sign, if more 2Gb then skips) 
		RELEX::WriteSafe(relOff.address() + off + 1, { 0x8E });
		RELEX::WriteSafe(relOff2.address() + off2 + 1, { 0x8E });

		return true;
	}
}