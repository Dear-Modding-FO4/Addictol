#include "Harness.h"

#include <Modules/AdControlSamplerCache.h>

#include <array>

namespace
{
	using Microsoft::WRL::ComPtr;
	using Addictol::ControlSamplerCache;

	struct TestDevice
	{
		ComPtr<REX::W32::ID3D11Device> device;
		ComPtr<REX::W32::ID3D11DeviceContext> context;
	};

	[[nodiscard]] TestDevice CreateDevice()
	{
		TestDevice result;
		const auto hr = REX::W32::D3D11CreateDevice(
			nullptr,
			REX::W32::D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			nullptr,
			0,
			7,
			result.device.GetAddressOf(),
			nullptr,
			result.context.GetAddressOf());
		vmm_tests::require(SUCCEEDED(hr) && result.device, "WARP D3D11 device creation failed");
		return result;
	}

	[[nodiscard]] ComPtr<REX::W32::ID3D11SamplerState> CreateSampler(
		REX::W32::ID3D11Device* a_device,
		REX::W32::D3D11_FILTER a_filter,
		uint32_t a_maxAnisotropy = 1,
		REX::W32::D3D11_COMPARISON_FUNC a_comparison = REX::W32::D3D11_COMPARISON_NEVER)
	{
		REX::W32::D3D11_SAMPLER_DESC desc{};
		desc.filter = a_filter;
		desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_WRAP;
		desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_WRAP;
		desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_WRAP;
		desc.maxAnisotropy = a_maxAnisotropy;
		desc.comparisonFunc = a_comparison;
		desc.maxLOD = 3.402823466e+38f;

		ComPtr<REX::W32::ID3D11SamplerState> sampler;
		const auto hr = a_device->CreateSamplerState(&desc, sampler.GetAddressOf());
		vmm_tests::require(SUCCEEDED(hr) && sampler, "test sampler creation failed");
		return sampler;
	}

	[[nodiscard]] REX::W32::D3D11_SAMPLER_DESC Describe(REX::W32::ID3D11SamplerState* a_sampler)
	{
		REX::W32::D3D11_SAMPLER_DESC desc{};
		a_sampler->GetDesc(&desc);
		return desc;
	}

	[[nodiscard]] ControlSamplerCache::Sampler Translate(
		ControlSamplerCache& a_cache,
		REX::W32::ID3D11Device* a_device,
		REX::W32::ID3D11SamplerState* a_sampler,
		uint32_t a_maxAnisotropy,
		HRESULT a_result = S_OK)
	{
		std::array<REX::W32::ID3D11SamplerState*, 1> input{ a_sampler };
		std::array<ControlSamplerCache::Sampler, 1> output;
		a_cache.Translate(
			a_device,
			{ -1.f, a_maxAnisotropy, false },
			input,
			output,
			[a_device, a_result](const REX::W32::D3D11_SAMPLER_DESC& a_desc, REX::W32::ID3D11SamplerState** a_output) {
				return FAILED(a_result) ? a_result : a_device->CreateSamplerState(&a_desc, a_output);
			});
		return output[0];
	}
}

namespace vmm_tests
{
	void run_control_sampler_checks(Runner& runner)
	{
		runner.test("control samplers create valid comparison-anisotropic replacements", [] {
			auto device = CreateDevice();
			ControlSamplerCache cache;

			auto comparison = CreateSampler(
				device.device.Get(),
				REX::W32::D3D11_FILTER_COMPARISON_ANISOTROPIC,
				4,
				REX::W32::D3D11_COMPARISON_LESS_EQUAL);
			auto comparisonReplacement = Translate(cache, device.device.Get(), comparison.Get(), 16);
			require(comparisonReplacement && comparisonReplacement != comparison,
				"comparison-anisotropic sampler was not replaced");
			const auto comparisonDesc = Describe(comparisonReplacement.Get());
			require(comparisonDesc.maxAnisotropy == 16, "comparison sampler anisotropy was not applied");
		});

		runner.test("control samplers preserve failed replacements", [] {
			auto device = CreateDevice();
			ControlSamplerCache cache;

			auto original = CreateSampler(device.device.Get(), REX::W32::D3D11_FILTER_ANISOTROPIC, 4);
			require(Translate(cache, device.device.Get(), original.Get(), 16, E_FAIL) == original,
				"failed replacement did not preserve the original");
		});

		runner.test("control samplers leave foreign-device samplers unchanged", [] {
			auto device = CreateDevice();
			auto foreignDevice = CreateDevice();
			ControlSamplerCache cache;

			auto foreign = CreateSampler(foreignDevice.device.Get(), REX::W32::D3D11_FILTER_ANISOTROPIC, 4);
			require(Translate(cache, device.device.Get(), foreign.Get(), 16) == foreign,
				"foreign-device sampler was replaced");
		});
	}
}
