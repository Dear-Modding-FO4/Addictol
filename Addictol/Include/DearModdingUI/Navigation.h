#pragma once

#include <DearModdingUI/API.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Addictol::DearModdingUI
{
	struct RegisteredClient;
	struct RegisteredPage;

	struct NavigationPage
	{
		DMUI_PageHandle handle{ DMUI_INVALID_PAGE_HANDLE };
		DMUI_ClientHandle client{ DMUI_INVALID_CLIENT_HANDLE };
		std::string id;
		std::string displayName;
		std::string category;
		std::string summary;
		int32_t sortKey{ 0 };
	};

	struct NavigationCategory
	{
		std::string displayName;
		std::vector<NavigationPage> pages;
	};

	struct NavigationClient
	{
		DMUI_ClientHandle handle{ DMUI_INVALID_CLIENT_HANDLE };
		std::string id;
		std::string displayName;
		uint32_t version{ 0 };
		std::vector<NavigationCategory> categories;
	};

	struct NavigationModel
	{
		std::vector<NavigationClient> clients;

		[[nodiscard]] const NavigationClient* FindClient(DMUI_ClientHandle a_client) const noexcept;
		[[nodiscard]] const NavigationClient* FindClientForPage(DMUI_PageHandle a_page) const noexcept;
		[[nodiscard]] const NavigationPage* FindPage(DMUI_PageHandle a_page) const noexcept;
		[[nodiscard]] DMUI_PageHandle FirstPage() const noexcept;
	};

	struct ClientSelectionState
	{
		DMUI_ClientHandle activeClient{ DMUI_INVALID_CLIENT_HANDLE };
		DMUI_PageHandle activePage{ DMUI_INVALID_PAGE_HANDLE };
		std::string search;
	};

	enum class PagePresentation : uint32_t
	{
		kEmpty,
		kContent,
		kFailure
	};

	[[nodiscard]] NavigationModel BuildNavigationModel(
		const std::vector<RegisteredClient>& a_clients,
		const std::vector<RegisteredPage>& a_pages);
	[[nodiscard]] DMUI_PageHandle ResolvePageSelection(
		const NavigationModel& a_model,
		DMUI_PageHandle a_requested,
		DMUI_PageHandle a_current) noexcept;
	[[nodiscard]] bool SelectClient(
		const NavigationModel& a_model,
		DMUI_ClientHandle a_client,
		ClientSelectionState& a_state) noexcept;
	[[nodiscard]] PagePresentation DecidePagePresentation(
		const NavigationPage* a_page,
		bool a_callbackFailed) noexcept;
}
