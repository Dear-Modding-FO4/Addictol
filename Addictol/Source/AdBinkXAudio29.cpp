#include <xaudio2.h>
#include <wrl/client.h>

#pragma comment(lib, "xaudio2.lib")

namespace Addictol
{
	class BinkXAudio29
	{
		BinkXAudio29(const BinkXAudio29&) = delete;
		BinkXAudio29(BinkXAudio29&&) = delete;
		BinkXAudio29 operator=(BinkXAudio29&&) = delete;
		BinkXAudio29 operator=(const BinkXAudio29&) = delete;
	public:
		Microsoft::WRL::ComPtr<IXAudio2> m_XAudio2{};
		IXAudio2MasteringVoice* m_XAudio2MasteringVoice{ nullptr };

		constexpr BinkXAudio29() noexcept = default;
		virtual ~BinkXAudio29() noexcept
		{
			Release();
		}

		[[nodiscard]] virtual bool Initialize() noexcept
		{
			auto hr = CoInitialize(nullptr);
			if (FAILED(hr))
				return false;

			hr = XAudio2Create(m_XAudio2.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
			if (FAILED(hr))
			{
				CoUninitialize();
				return false;
			}

			hr = m_XAudio2->CreateMasteringVoice(&m_XAudio2MasteringVoice);
			if (FAILED(hr)) 
			{
				m_XAudio2.Reset();
				CoUninitialize();
				return false;
			}

			XAUDIO2_VOICE_DETAILS details;
			m_XAudio2MasteringVoice->GetVoiceDetails(&details);

			REX::INFO("test {}", details.InputSampleRate);

			return true;
		}

		virtual void Release() noexcept
		{
			if (m_XAudio2MasteringVoice)
				std::exchange(m_XAudio2MasteringVoice, nullptr)->DestroyVoice();

			if (m_XAudio2)
			{
				m_XAudio2.Reset();
				CoUninitialize();
			}
		}
	};

	BinkXAudio29 g_BinkXAudio29;

	[[nodiscard]] bool BinkXAudio29Create(IUnknown* &a_engine, void* &a_masteringVoice) noexcept
	{
		if (g_BinkXAudio29.Initialize())
		{
			a_engine = g_BinkXAudio29.m_XAudio2.Get();
			a_masteringVoice = g_BinkXAudio29.m_XAudio2MasteringVoice;

			return true;
		}

		return false;
	}

	void BinkXAudio29Destroy() noexcept
	{
		g_BinkXAudio29.Release();
	}
}