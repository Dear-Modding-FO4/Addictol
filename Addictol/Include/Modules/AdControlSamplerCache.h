#pragma once

#include <REX/W32/D3D11.h>

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <wrl/client.h>

namespace Addictol
{
	struct ControlSamplerSettings
	{
		float mipLODBias;
		uint32_t maxAnisotropy;
		bool ignoreExistingBias;
	};

	class ControlSamplerCache
	{
	public:
		using Sampler = Microsoft::WRL::ComPtr<REX::W32::ID3D11SamplerState>;

		void Clear()
		{
			std::unique_lock lock{ m_mutex };
			m_entries.clear();
		}

		template <class Create>
		void Translate(
			REX::W32::ID3D11Device* a_device,
			const ControlSamplerSettings& a_settings,
			std::span<REX::W32::ID3D11SamplerState* const> a_input,
			std::span<Sampler> a_output,
			Create&& a_create)
		{
			const auto count = std::min(a_input.size(), a_output.size());
			for (size_t i = 0; i < count; ++i)
				a_output[i] = Resolve(a_device, a_settings, a_input[i], a_create);
		}

		[[nodiscard]] static constexpr bool IsAnisotropic(REX::W32::D3D11_FILTER a_filter) noexcept
		{
			return (static_cast<uint32_t>(a_filter) & 0x40) != 0;
		}

	private:
		template <class Create>
		[[nodiscard]] Sampler Resolve(
			REX::W32::ID3D11Device* a_device,
			const ControlSamplerSettings& a_settings,
			REX::W32::ID3D11SamplerState* a_sampler,
			Create& a_create)
		{
			if (!a_sampler)
				return {};

			{
				std::shared_lock lock{ m_mutex };
				if (const auto entry = m_entries.find(a_sampler); entry != m_entries.end())
					return entry->second.output;
			}

			Sampler original{ a_sampler };
			Sampler output = original;
			Microsoft::WRL::ComPtr<REX::W32::ID3D11Device> owner;
			a_sampler->GetDevice(owner.GetAddressOf());

			REX::W32::D3D11_SAMPLER_DESC desc{};
			a_sampler->GetDesc(&desc);
			if (owner.Get() == a_device && (!desc.mipLODBias || a_settings.ignoreExistingBias))
			{
				desc.mipLODBias = a_settings.mipLODBias;
				if (IsAnisotropic(desc.filter))
					desc.maxAnisotropy = std::clamp(a_settings.maxAnisotropy, 1u, 16u);

				Sampler replacement;
				if (SUCCEEDED(a_create(desc, replacement.GetAddressOf())) && replacement)
					output = std::move(replacement);
			}

			std::unique_lock lock{ m_mutex };
			const auto [entry, inserted] = m_entries.try_emplace(a_sampler, Entry{ original, output });
			if (inserted && output != original)
				m_entries.try_emplace(output.Get(), Entry{ output, output });
			return entry->second.output;
		}

		struct Entry
		{
			Sampler key;
			Sampler output;
		};

		std::shared_mutex m_mutex;
		std::unordered_map<REX::W32::ID3D11SamplerState*, Entry> m_entries;
	};
}
