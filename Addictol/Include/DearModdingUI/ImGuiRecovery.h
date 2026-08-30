#pragma once

#include <cstring>
#include <new>
#include <optional>

#include <imgui/imgui_internal.h>

namespace Addictol::DearModdingUI
{
	class ImGuiRecoverySnapshot
	{
	public:
		[[nodiscard]] static std::optional<ImGuiRecoverySnapshot> Capture() noexcept
		{
			try
			{
				auto* context = ImGui::GetCurrentContext();
				if (!context)
					return std::nullopt;
				return ImGuiRecoverySnapshot(*context);
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		ImGuiRecoverySnapshot(const ImGuiRecoverySnapshot&) = delete;
		ImGuiRecoverySnapshot& operator=(const ImGuiRecoverySnapshot&) = delete;

		ImGuiRecoverySnapshot(ImGuiRecoverySnapshot&& a_other) noexcept :
			m_context(a_other.m_context),
			m_stackState(a_other.m_stackState),
			m_nextWindowData(a_other.m_nextWindowData),
			m_nextItemData(a_other.m_nextItemData),
			m_openPopupData(a_other.m_openPopupData),
			m_openPopupSize(a_other.m_openPopupSize)
		{
			a_other.m_openPopupData = nullptr;
			a_other.m_openPopupSize = 0;
		}

		~ImGuiRecoverySnapshot()
		{
			if (m_openPopupData)
				ImGui::MemFree(m_openPopupData);
		}

		void RecoverAfterCallback() noexcept
		{
			ImGui::SetCurrentContext(m_context);
			auto& io = ImGui::GetIO();
			const auto assertEnabled = io.ConfigErrorRecoveryEnableAssert;
			io.ConfigErrorRecoveryEnableAssert = false;
			ImGui::ErrorRecoveryTryToRecoverState(&m_stackState);
			io.ConfigErrorRecoveryEnableAssert = assertEnabled;
			m_context->NextWindowData = m_nextWindowData;
			m_context->NextItemData = m_nextItemData;
		}

		void RecoverFailure() noexcept
		{
			RecoverAfterCallback();
			if (m_context->OpenPopupStack.Data)
				ImGui::MemFree(m_context->OpenPopupStack.Data);
			m_context->OpenPopupStack.Data = m_openPopupData;
			m_context->OpenPopupStack.Size = m_openPopupSize;
			m_context->OpenPopupStack.Capacity = m_openPopupSize;
			m_openPopupData = nullptr;
			m_openPopupSize = 0;
			if (!m_context->OpenPopupStack.empty())
				ImGui::ClosePopupToLevel(0, true);
		}

	private:
		explicit ImGuiRecoverySnapshot(ImGuiContext& a_context) :
			m_context(&a_context),
			m_nextWindowData(a_context.NextWindowData),
			m_nextItemData(a_context.NextItemData)
		{
			ImGui::ErrorRecoveryStoreState(&m_stackState);
			if (a_context.OpenPopupStack.empty())
				return;
			m_openPopupSize = a_context.OpenPopupStack.Size;
			m_openPopupData = static_cast<ImGuiPopupData*>(
				ImGui::MemAlloc(static_cast<size_t>(m_openPopupSize) * sizeof(ImGuiPopupData)));
			if (!m_openPopupData)
				throw std::bad_alloc();
			std::memcpy(
				m_openPopupData,
				a_context.OpenPopupStack.Data,
				static_cast<size_t>(m_openPopupSize) * sizeof(ImGuiPopupData));
		}

		ImGuiContext* m_context;
		ImGuiErrorRecoveryState m_stackState{};
		ImGuiNextWindowData m_nextWindowData;
		ImGuiNextItemData m_nextItemData;
		ImGuiPopupData* m_openPopupData{ nullptr };
		int m_openPopupSize{ 0 };
	};
}
