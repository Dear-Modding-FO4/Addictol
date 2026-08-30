#include <DearModdingUI/Navigation.h>
#include <DearModdingUI/Registry.h>

#include <algorithm>
#include <tuple>

namespace Addictol::DearModdingUI
{
	const NavigationClient* NavigationModel::FindClient(
		DMUI_ClientHandle a_client) const noexcept
	{
		const auto found = std::ranges::find_if(clients, [&](const auto& a_entry) {
			return a_entry.handle == a_client;
		});
		return found != clients.end() ? &*found : nullptr;
	}

	const NavigationClient* NavigationModel::FindClientForPage(
		DMUI_PageHandle a_page) const noexcept
	{
		for (const auto& client : clients)
		{
			for (const auto& category : client.categories)
			{
				if (std::ranges::any_of(category.pages, [&](const auto& a_entry) {
						return a_entry.handle == a_page;
					}))
					return &client;
			}
		}
		return nullptr;
	}

	const NavigationPage* NavigationModel::FindPage(DMUI_PageHandle a_page) const noexcept
	{
		for (const auto& client : clients)
		{
			for (const auto& category : client.categories)
			{
				const auto found = std::ranges::find_if(category.pages, [&](const auto& a_entry) {
					return a_entry.handle == a_page;
				});
				if (found != category.pages.end())
					return &*found;
			}
		}
		return nullptr;
	}

	DMUI_PageHandle NavigationModel::FirstPage() const noexcept
	{
		for (const auto& client : clients)
		{
			for (const auto& category : client.categories)
			{
				if (!category.pages.empty())
					return category.pages.front().handle;
			}
		}
		return DMUI_INVALID_PAGE_HANDLE;
	}

	NavigationModel BuildNavigationModel(
		const std::vector<RegisteredClient>& a_clients,
		const std::vector<RegisteredPage>& a_pages)
	{
		std::vector<const RegisteredClient*> orderedClients;
		orderedClients.reserve(a_clients.size());
		for (const auto& client : a_clients)
		{
			if (std::ranges::any_of(a_pages, [&](const auto& a_page) {
					return a_page.client == client.handle &&
						a_page.kind == DMUI_PAGE_KIND_SETTINGS;
				}))
				orderedClients.push_back(&client);
		}
		std::ranges::sort(orderedClients, [](const auto* a_left, const auto* a_right) {
			return std::tie(a_left->origin, a_left->displayName, a_left->id) <
				std::tie(a_right->origin, a_right->displayName, a_right->id);
		});

		NavigationModel model;
		model.clients.reserve(orderedClients.size());
		for (const auto* client : orderedClients)
		{
			NavigationClient navigationClient{
				client->handle,
				client->id,
				client->displayName,
				client->version,
				{}
			};

			std::vector<const RegisteredPage*> orderedPages;
			for (const auto& page : a_pages)
			{
				if (page.client == client->handle &&
					page.kind == DMUI_PAGE_KIND_SETTINGS)
					orderedPages.push_back(&page);
			}
			std::ranges::sort(orderedPages, [](const auto* a_left, const auto* a_right) {
				return std::tie(
					a_left->category,
					a_left->sortKey,
					a_left->displayName,
					a_left->id) <
					std::tie(
						a_right->category,
						a_right->sortKey,
						a_right->displayName,
						a_right->id);
			});

			for (const auto* page : orderedPages)
			{
				if (navigationClient.categories.empty() ||
					navigationClient.categories.back().displayName != page->category)
					navigationClient.categories.push_back({ page->category, {} });
				navigationClient.categories.back().pages.push_back({
					page->handle,
					page->client,
					page->id,
					page->displayName,
					page->category,
					page->summary,
					page->sortKey
				});
			}
			model.clients.push_back(std::move(navigationClient));
		}
		return model;
	}

	DMUI_PageHandle ResolvePageSelection(
		const NavigationModel& a_model,
		DMUI_PageHandle a_requested,
		DMUI_PageHandle a_current) noexcept
	{
		if (a_model.FindPage(a_requested))
			return a_requested;
		if (a_model.FindPage(a_current))
			return a_current;
		return a_model.FirstPage();
	}

	bool SelectClient(
		const NavigationModel& a_model,
		DMUI_ClientHandle a_client,
		ClientSelectionState& a_state) noexcept
	{
		const auto* client = a_model.FindClient(a_client);
		if (!client || a_state.activeClient == a_client)
			return false;

		a_state.activeClient = a_client;
		a_state.activePage = DMUI_INVALID_PAGE_HANDLE;
		for (const auto& category : client->categories)
		{
			if (!category.pages.empty())
			{
				a_state.activePage = category.pages.front().handle;
				break;
			}
		}
		a_state.search.clear();
		return true;
	}

	PagePresentation DecidePagePresentation(
		const NavigationPage* a_page,
		bool a_callbackFailed) noexcept
	{
		if (!a_page)
			return PagePresentation::kEmpty;
		return a_callbackFailed ? PagePresentation::kFailure : PagePresentation::kContent;
	}
}
