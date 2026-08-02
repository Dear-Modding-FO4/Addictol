#pragma once

namespace Addictol
{
	class ModuleDefender
	{
		struct Snapshot
		{
			std::unique_ptr<uint8_t[]> data{};
			REX::FModuleSection section{};
		};

		Snapshot m_code{}, m_rwData{};
		bool m_take{ false };

		ModuleDefender(const ModuleDefender&) = delete;
		ModuleDefender(ModuleDefender&&) = delete;
		ModuleDefender operator=(ModuleDefender&&) = delete;
		ModuleDefender operator=(const ModuleDefender&) = delete;
	public:
		constexpr ModuleDefender() noexcept = default;
		virtual ~ModuleDefender() noexcept;

		[[nodiscard]] virtual bool Initialize() noexcept;
		virtual void Release() noexcept;

		[[nodiscard]] virtual bool TakeSnapshot() noexcept;
		[[nodiscard]] virtual bool RestoreFromSnapshot() noexcept;
	};
}