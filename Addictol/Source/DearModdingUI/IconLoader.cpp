#include <DearModdingUI/IconLoader.h>
#include <Core/AdUtils.h>

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Addictol::DearModdingUI::IconLoader
{
	namespace
	{
		template <class T>
		using ComPtr = Microsoft::WRL::ComPtr<T>;

		struct CachedIcon
		{
			ComPtr<ID3D11ShaderResourceView> texture;
		};

		struct Resources
		{
			ID3D11Device* device{ nullptr };
			std::unordered_map<std::string, CachedIcon> icons;
		};

		Resources g_resources;

		class ComInitialization
		{
		public:
			ComInitialization() noexcept :
				m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
			{}

			~ComInitialization() noexcept
			{
				if (SUCCEEDED(m_result))
					CoUninitialize();
			}

			[[nodiscard]] bool Available() const noexcept
			{
				return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
			}

			ComInitialization(const ComInitialization&) = delete;
			ComInitialization& operator=(const ComInitialization&) = delete;

		private:
			HRESULT m_result;
		};

		[[nodiscard]] std::filesystem::path IconRoot()
		{
			auto root = std::filesystem::path{ AdGetRuntimeDirectory() };
			root /= L"Data\\F4SE\\Plugins\\DearModdingUI\\Icons";
			return root;
		}

		[[nodiscard]] bool FileExists(const std::filesystem::path& a_path) noexcept
		{
			std::error_code error;
			return std::filesystem::is_regular_file(a_path, error);
		}

		[[nodiscard]] bool CreateFactory(ComPtr<IWICImagingFactory>& a_factory) noexcept
		{
			auto result = CoCreateInstance(
				CLSID_WICImagingFactory2,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(a_factory.ReleaseAndGetAddressOf()));
			if (FAILED(result))
			{
				result = CoCreateInstance(
					CLSID_WICImagingFactory,
					nullptr,
					CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(a_factory.ReleaseAndGetAddressOf()));
			}
			return SUCCEEDED(result) && a_factory;
		}

		[[nodiscard]] bool LoadTexture(
			ID3D11Device* a_device,
			const std::filesystem::path& a_path,
			ComPtr<ID3D11ShaderResourceView>& a_texture) noexcept
		{
			try
			{
				ComInitialization com;
				if (!com.Available())
					return false;

				ComPtr<IWICImagingFactory> factory;
				if (!CreateFactory(factory))
					return false;

				ComPtr<IWICBitmapDecoder> decoder;
				if (FAILED(factory->CreateDecoderFromFilename(
						a_path.c_str(),
						nullptr,
						GENERIC_READ,
						WICDecodeMetadataCacheOnLoad,
						decoder.ReleaseAndGetAddressOf())))
					return false;

				ComPtr<IWICBitmapFrameDecode> frame;
				if (FAILED(decoder->GetFrame(0, frame.ReleaseAndGetAddressOf())))
					return false;

				ComPtr<IWICFormatConverter> converter;
				if (FAILED(factory->CreateFormatConverter(
						converter.ReleaseAndGetAddressOf())) ||
					FAILED(converter->Initialize(
						frame.Get(),
						GUID_WICPixelFormat32bppRGBA,
						WICBitmapDitherTypeNone,
						nullptr,
						0.0,
						WICBitmapPaletteTypeCustom)))
					return false;

				UINT width{};
				UINT height{};
				if (FAILED(converter->GetSize(&width, &height)) ||
					!width ||
					!height ||
					width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
					height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
					return false;

				const auto stride = static_cast<size_t>(width) * 4;
				if (stride > (std::numeric_limits<UINT>::max)() ||
					height > (std::numeric_limits<UINT>::max)() / stride)
					return false;
				const auto byteCount = stride * height;
				std::vector<uint8_t> pixels(byteCount);
				if (FAILED(converter->CopyPixels(
						nullptr,
						static_cast<UINT>(stride),
						static_cast<UINT>(byteCount),
						pixels.data())))
					return false;

				D3D11_TEXTURE2D_DESC description{};
				description.Width = width;
				description.Height = height;
				description.MipLevels = 1;
				description.ArraySize = 1;
				description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				description.SampleDesc.Count = 1;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				const D3D11_SUBRESOURCE_DATA data{
					pixels.data(),
					static_cast<UINT>(stride),
					static_cast<UINT>(byteCount)
				};

				ComPtr<ID3D11Texture2D> texture;
				if (FAILED(a_device->CreateTexture2D(
						&description,
						&data,
						texture.ReleaseAndGetAddressOf())) ||
					FAILED(a_device->CreateShaderResourceView(
						texture.Get(),
						nullptr,
						a_texture.ReleaseAndGetAddressOf())))
					return false;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	void SetDevice(ID3D11Device* a_device) noexcept
	{
		if (g_resources.device == a_device)
			return;
		g_resources = {};
		g_resources.device = a_device;
	}

	void Shutdown() noexcept
	{
		g_resources = {};
	}

	ID3D11ShaderResourceView* Get(
		IconKind a_kind,
		std::string_view a_name) noexcept
	{
		if (!g_resources.device)
			return nullptr;
		try
		{
			const auto root = IconRoot();
			const auto path = BuildIconPath(root, a_kind, a_name);
			if (!path)
				return nullptr;
			const auto key = path->generic_string();
			const auto [entry, inserted] =
				g_resources.icons.try_emplace(key);
			if (inserted && FileExists(*path))
				(void)LoadTexture(g_resources.device, *path, entry->second.texture);
			return entry->second.texture.Get();
		}
		catch (...)
		{
			return nullptr;
		}
	}
}
