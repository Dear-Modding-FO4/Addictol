#include <DearModdingUI/Registry.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <tuple>

namespace Addictol::DearModdingUI
{
	namespace
	{
		inline constexpr size_t kIdCapacity{ 128 };
		inline constexpr size_t kDisplayNameCapacity{ 256 };
		inline constexpr size_t kCategoryCapacity{ 128 };
		inline constexpr size_t kSummaryCapacity{ 1024 };
		inline constexpr size_t kIconNameCapacity{ 128 };

		[[nodiscard]] bool ReadString(
			const char* a_value,
			size_t a_capacity,
			bool a_optional,
			std::string& a_out)
		{
			if (!a_value)
			{
				if (a_optional)
				return true;
				return false;
			}

			size_t length = 0;
			while (length <= a_capacity && a_value[length])
				++length;
			if (length > a_capacity || (!a_optional && length == 0))
				return false;
			a_out.assign(a_value, length);
			return true;
		}

		[[nodiscard]] bool ValidId(std::string_view a_id) noexcept
		{
			if (a_id.empty())
				return false;
			for (const auto character : a_id)
			{
				const auto alpha =
					(character >= 'a' && character <= 'z') ||
					(character >= 'A' && character <= 'Z');
				const auto digit = character >= '0' && character <= '9';
				if (!alpha && !digit && character != '.' && character != '_' && character != '-')
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ValidText(std::string_view a_text, bool a_optional) noexcept
		{
			if (!a_optional && a_text.empty())
				return false;
			for (const auto character : a_text)
			{
				if (static_cast<unsigned char>(character) < 0x20u && character != '\t')
					return false;
			}
			return true;
		}

		[[nodiscard]] bool InvokeReadyCpp(
			DMUI_HostReadyCallback a_callback,
			const DMUI_HostReadyInfo* a_info,
			void* a_userData) noexcept
		{
			try
			{
				a_callback(a_info, a_userData);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		[[nodiscard]] bool InvokeUnavailableCpp(
			DMUI_HostUnavailableCallback a_callback,
			DMUI_UnavailableReason a_reason,
			void* a_userData) noexcept
		{
			try
			{
				a_callback(a_reason, a_userData);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		[[nodiscard]] bool InvokeDrawCpp(
			DMUI_PageDrawCallback a_callback,
			void* a_userData) noexcept
		{
			try
			{
				a_callback(a_userData);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		[[nodiscard]] bool InvokeReady(
			DMUI_HostReadyCallback a_callback,
			const DMUI_HostReadyInfo* a_info,
			void* a_userData) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				return InvokeReadyCpp(a_callback, a_info, a_userData);
			}
			__except (1)
			{
				return false;
			}
#else
			return InvokeReadyCpp(a_callback, a_info, a_userData);
#endif
		}

		[[nodiscard]] bool InvokeUnavailable(
			DMUI_HostUnavailableCallback a_callback,
			DMUI_UnavailableReason a_reason,
			void* a_userData) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				return InvokeUnavailableCpp(a_callback, a_reason, a_userData);
			}
			__except (1)
			{
				return false;
			}
#else
			return InvokeUnavailableCpp(a_callback, a_reason, a_userData);
#endif
		}

		[[nodiscard]] bool InvokeDraw(
			DMUI_PageDrawCallback a_callback,
			void* a_userData) noexcept
		{
#if defined(_MSC_VER)
			__try
			{
				return InvokeDrawCpp(a_callback, a_userData);
			}
			__except (1)
			{
				return false;
			}
#else
			return InvokeDrawCpp(a_callback, a_userData);
#endif
		}
	}

	Registry::Registry(DMUI_ImGuiFingerprint a_fingerprint) :
		m_fingerprint(a_fingerprint)
	{}

	DMUI_Result Registry::RegisterClient(
		const DMUI_ClientDescriptor* a_descriptor,
		DMUI_ClientHandle* a_client,
		ClientOrigin a_origin) noexcept
	{
		if (!a_descriptor || !a_client)
			return DMUI_RESULT_INVALID_ARGUMENT;
		*a_client = DMUI_INVALID_CLIENT_HANDLE;
		if (a_descriptor->structSize < sizeof(DMUI_ClientDescriptor))
			return DMUI_RESULT_STRUCT_TOO_SMALL;
		if (!SupportsVersion(a_descriptor->apiVersion))
			return DMUI_RESULT_UNSUPPORTED_ABI;
		if (!a_descriptor->expectedImGui)
			return DMUI_RESULT_INVALID_DESCRIPTOR;
		if (a_descriptor->expectedImGui->structSize < sizeof(DMUI_ImGuiFingerprint))
			return DMUI_RESULT_STRUCT_TOO_SMALL;
		if (!FingerprintsMatch(*a_descriptor->expectedImGui, m_fingerprint))
			return DMUI_RESULT_FINGERPRINT_MISMATCH;
		if (!a_descriptor->onHostReady || !a_descriptor->onHostUnavailable)
			return DMUI_RESULT_INVALID_DESCRIPTOR;
		if ((a_descriptor->capabilities &
				~DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT) != 0)
			return DMUI_RESULT_INVALID_DESCRIPTOR;

		try
		{
			RegisteredClient client{};
			client.origin = a_origin;
			client.version = a_descriptor->version;
			client.capabilities = a_descriptor->capabilities;
			client.onHostReady = a_descriptor->onHostReady;
			client.onHostUnavailable = a_descriptor->onHostUnavailable;
			client.userData = a_descriptor->userData;
			if (!ReadString(a_descriptor->id, kIdCapacity, false, client.id) ||
				!ReadString(a_descriptor->displayName, kDisplayNameCapacity, false, client.displayName) ||
				!ValidId(client.id) ||
				!ValidText(client.displayName, false))
				return DMUI_RESULT_INVALID_DESCRIPTOR;

			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			if (std::ranges::any_of(m_clients, [&](const auto& a_existing) {
					return a_existing.id == client.id;
				}))
				return DMUI_RESULT_DUPLICATE_CLIENT_ID;
			if (m_nextClient == DMUI_INVALID_CLIENT_HANDLE)
				return DMUI_RESULT_RESOURCE_EXHAUSTED;

			client.handle = m_nextClient++;
			m_clients.push_back(std::move(client));
			*a_client = m_clients.back().handle;
			return DMUI_RESULT_OK;
		}
		catch (...)
		{
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		}
	}

	DMUI_Result Registry::RegisterPage(
		DMUI_ClientHandle a_client,
		const DMUI_PageDescriptor* a_descriptor,
		DMUI_PageHandle* a_page) noexcept
	{
		if (!a_descriptor || !a_page || a_client == DMUI_INVALID_CLIENT_HANDLE)
			return DMUI_RESULT_INVALID_ARGUMENT;
		*a_page = DMUI_INVALID_PAGE_HANDLE;
		if (a_descriptor->structSize < sizeof(DMUI_PageDescriptor))
			return DMUI_RESULT_STRUCT_TOO_SMALL;
		if (!a_descriptor->draw)
			return DMUI_RESULT_INVALID_DESCRIPTOR;
		if (a_descriptor->kind != DMUI_PAGE_KIND_SETTINGS &&
			a_descriptor->kind != DMUI_PAGE_KIND_OVERLAY)
			return DMUI_RESULT_INVALID_PAGE_KIND;

		try
		{
			RegisteredPage page{};
			page.client = a_client;
			page.sortKey = a_descriptor->sortKey;
			page.kind = a_descriptor->kind;
			page.draw = a_descriptor->draw;
			page.userData = a_descriptor->userData;
			if (!ReadString(a_descriptor->id, kIdCapacity, false, page.id) ||
				!ReadString(a_descriptor->displayName, kDisplayNameCapacity, false, page.displayName) ||
				!ReadString(a_descriptor->category, kCategoryCapacity, false, page.category) ||
				!ReadString(a_descriptor->summary, kSummaryCapacity, true, page.summary) ||
				!ValidId(page.id) ||
				!ValidText(page.displayName, false) ||
				!ValidText(page.category, false) ||
				!ValidText(page.summary, true))
				return DMUI_RESULT_INVALID_DESCRIPTOR;

			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			const auto* client = FindClient(a_client);
			if (!client)
				return DMUI_RESULT_CLIENT_NOT_FOUND;
			if (std::ranges::any_of(m_pages, [&](const auto& a_existing) {
					return a_existing.client == a_client && a_existing.id == page.id;
				}))
				return DMUI_RESULT_DUPLICATE_PAGE_ID;
			if (m_nextPage == DMUI_INVALID_PAGE_HANDLE)
				return DMUI_RESULT_RESOURCE_EXHAUSTED;

			page.handle = m_nextPage++;
			page.clientId = client->id;
			page.clientDisplayName = client->displayName;
			page.clientOrigin = client->origin;
			page.imguiLabel = page.displayName + "###" + page.clientId + "/" + page.id;
			m_pages.push_back(std::move(page));
			*a_page = m_pages.back().handle;
			return DMUI_RESULT_OK;
		}
		catch (...)
		{
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		}
	}

	DMUI_Result Registry::RegisterAction(
		DMUI_ClientHandle a_client,
		const DMUI_ActionDescriptor* a_descriptor,
		DMUI_ActionHandle* a_action) noexcept
	{
		if (!a_descriptor || !a_action || a_client == DMUI_INVALID_CLIENT_HANDLE)
			return DMUI_RESULT_INVALID_ARGUMENT;
		*a_action = DMUI_INVALID_ACTION_HANDLE;
		if (a_descriptor->structSize < sizeof(DMUI_ActionDescriptor))
			return DMUI_RESULT_STRUCT_TOO_SMALL;
		if (!a_descriptor->callback)
			return DMUI_RESULT_INVALID_DESCRIPTOR;

		try
		{
			RegisteredAction action{};
			action.client = a_client;
			action.sortKey = a_descriptor->sortKey;
			action.callback = a_descriptor->callback;
			action.userData = a_descriptor->userData;
			if (!ReadString(a_descriptor->id, kIdCapacity, false, action.id) ||
				!ReadString(
					a_descriptor->displayLabel,
					kDisplayNameCapacity,
					false,
					action.displayLabel) ||
				!ReadString(
					a_descriptor->iconName,
					kIconNameCapacity,
					true,
					action.iconName) ||
				!ReadString(
					a_descriptor->tooltip,
					kSummaryCapacity,
					true,
					action.tooltip) ||
				!ValidId(action.id) ||
				!ValidText(action.displayLabel, false) ||
				!ValidText(action.iconName, true) ||
				!ValidText(action.tooltip, true))
				return DMUI_RESULT_INVALID_DESCRIPTOR;

			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			if (!FindClient(a_client))
				return DMUI_RESULT_CLIENT_NOT_FOUND;
			if (std::ranges::any_of(m_actions, [&](const auto& a_existing) {
					return a_existing.client == a_client &&
						a_existing.id == action.id;
				}))
				return DMUI_RESULT_DUPLICATE_ACTION_ID;
			if (m_nextAction == DMUI_INVALID_ACTION_HANDLE)
				return DMUI_RESULT_RESOURCE_EXHAUSTED;

			action.handle = m_nextAction++;
			m_actions.push_back(std::move(action));
			*a_action = m_actions.back().handle;
			return DMUI_RESULT_OK;
		}
		catch (...)
		{
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		}
	}

	bool Registry::Freeze() noexcept
	{
		try
		{
			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return false;
			std::ranges::sort(m_pages, [](const auto& a_left, const auto& a_right) {
				return std::tie(
					a_left.clientOrigin,
					a_left.clientDisplayName,
					a_left.clientId,
					a_left.category,
					a_left.sortKey,
					a_left.displayName,
					a_left.id) <
					std::tie(
						a_right.clientOrigin,
						a_right.clientDisplayName,
						a_right.clientId,
						a_right.category,
						a_right.sortKey,
						a_right.displayName,
						a_right.id);
			});
			std::ranges::sort(m_actions, [](const auto& a_left, const auto& a_right) {
				return std::tie(a_left.client, a_left.sortKey, a_left.id) <
					std::tie(a_right.client, a_right.sortKey, a_right.id);
			});
			m_navigation = BuildNavigationModel(m_clients, m_pages);
			m_open = false;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool Registry::IsOpen() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return m_open;
	}

	bool Registry::Empty() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return m_clients.empty();
	}

	size_t Registry::ClientCount() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return m_clients.size();
	}

	size_t Registry::PageCount() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return m_pages.size();
	}

	size_t Registry::DemandedOverlayCount() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return static_cast<size_t>(std::ranges::count_if(m_pages, [](const auto& a_page) {
			return a_page.kind == DMUI_PAGE_KIND_OVERLAY &&
				a_page.frameDemand != 0 &&
				!a_page.callbackFailed;
		}));
	}

	bool Registry::HasSettingsPages() const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		return std::ranges::any_of(m_pages, [](const auto& a_page) {
			return a_page.kind == DMUI_PAGE_KIND_SETTINGS;
		});
	}

	const std::vector<RegisteredPage>& Registry::OrderedPages() const noexcept
	{
		return m_pages;
	}

	const std::vector<RegisteredAction>& Registry::OrderedActions() const noexcept
	{
		return m_actions;
	}

	const NavigationModel& Registry::Navigation() const noexcept
	{
		return m_navigation;
	}

	DMUI_Result Registry::RequestFrame(
		DMUI_ClientHandle a_client,
		DMUI_PageHandle a_page) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (!OwnsPage(a_client, a_page))
			return FindClient(a_client) ? DMUI_RESULT_PAGE_NOT_FOUND : DMUI_RESULT_CLIENT_NOT_FOUND;
		auto* page = FindPage(a_page);
		if (page->kind != DMUI_PAGE_KIND_OVERLAY)
			return DMUI_RESULT_INVALID_PAGE_KIND;
		if (page->frameDemand == (std::numeric_limits<uint32_t>::max)())
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		++page->frameDemand;
		return DMUI_RESULT_OK;
	}

	DMUI_Result Registry::ReleaseFrame(
		DMUI_ClientHandle a_client,
		DMUI_PageHandle a_page) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (!OwnsPage(a_client, a_page))
			return FindClient(a_client) ? DMUI_RESULT_PAGE_NOT_FOUND : DMUI_RESULT_CLIENT_NOT_FOUND;
		auto* page = FindPage(a_page);
		if (page->kind != DMUI_PAGE_KIND_OVERLAY)
			return DMUI_RESULT_INVALID_PAGE_KIND;
		if (!page->frameDemand)
			return DMUI_RESULT_NO_FRAME_DEMAND;
		--page->frameDemand;
		return DMUI_RESULT_OK;
	}

	bool Registry::IsFrameDemanded(DMUI_PageHandle a_page) const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		const auto* page = FindPage(a_page);
		return page &&
			page->kind == DMUI_PAGE_KIND_OVERLAY &&
			page->frameDemand != 0 &&
			!page->callbackFailed;
	}

	DMUI_Result Registry::ValidatePage(
		DMUI_ClientHandle a_client,
		DMUI_PageHandle a_page,
		DMUI_PageKind a_kind) const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (!FindClient(a_client))
			return DMUI_RESULT_CLIENT_NOT_FOUND;
		const auto* page = FindPage(a_page);
		if (!page || page->client != a_client)
			return DMUI_RESULT_PAGE_NOT_FOUND;
		return page->kind == a_kind ? DMUI_RESULT_OK : DMUI_RESULT_INVALID_PAGE_KIND;
	}

	DMUI_Result Registry::ValidateSwapChainClient(DMUI_ClientHandle a_client) const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		const auto* client = FindClient(a_client);
		if (!client)
			return DMUI_RESULT_CLIENT_NOT_FOUND;
		return (client->capabilities & DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT) != 0 ?
			DMUI_RESULT_OK :
			DMUI_RESULT_CLIENT_CAPABILITY_REQUIRED;
	}

	DMUI_Result Registry::CopyClientDisplayName(
		DMUI_ClientHandle a_client,
		std::string& a_displayName) const noexcept
	{
		try
		{
			const std::scoped_lock lock{ m_mutex };
			const auto* client = FindClient(a_client);
			if (!client)
				return DMUI_RESULT_CLIENT_NOT_FOUND;
			a_displayName = client->displayName;
			return DMUI_RESULT_OK;
		}
		catch (...)
		{
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		}
	}

	DMUI_Result Registry::InvokePage(DMUI_PageHandle a_page) noexcept
	{
		DMUI_PageDrawCallback callback{ nullptr };
		void* userData{ nullptr };
		{
			const std::scoped_lock lock{ m_mutex };
			auto* page = FindPage(a_page);
			if (!page)
				return DMUI_RESULT_PAGE_NOT_FOUND;
			if (page->callbackFailed)
				return DMUI_RESULT_CALLBACK_FAILED;
			callback = page->draw;
			userData = page->userData;
		}

		if (InvokeDraw(callback, userData))
			return DMUI_RESULT_OK;

		const std::scoped_lock lock{ m_mutex };
		if (auto* page = FindPage(a_page))
			page->callbackFailed = true;
		return DMUI_RESULT_CALLBACK_FAILED;
	}

	bool Registry::PageFailed(DMUI_PageHandle a_page) const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		const auto* page = FindPage(a_page);
		return !page || page->callbackFailed;
	}

	void Registry::MarkPageFailed(DMUI_PageHandle a_page) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (auto* page = FindPage(a_page))
			page->callbackFailed = true;
	}

	DMUI_Result Registry::InvokeAction(DMUI_ActionHandle a_action) noexcept
	{
		DMUI_ActionCallback callback{ nullptr };
		void* userData{ nullptr };
		{
			const std::scoped_lock lock{ m_mutex };
			auto* action = FindAction(a_action);
			if (!action)
				return DMUI_RESULT_ACTION_NOT_FOUND;
			if (action->callbackFailed)
				return DMUI_RESULT_CALLBACK_FAILED;
			callback = action->callback;
			userData = action->userData;
		}

		if (InvokeDraw(callback, userData))
			return DMUI_RESULT_OK;

		const std::scoped_lock lock{ m_mutex };
		if (auto* action = FindAction(a_action))
			action->callbackFailed = true;
		return DMUI_RESULT_CALLBACK_FAILED;
	}

	bool Registry::ActionFailed(DMUI_ActionHandle a_action) const noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		const auto* action = FindAction(a_action);
		return !action || action->callbackFailed;
	}

	void Registry::MarkActionFailed(DMUI_ActionHandle a_action) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (auto* action = FindAction(a_action))
			action->callbackFailed = true;
	}

	void Registry::NotifyReady(const DMUI_HostReadyInfo& a_info) noexcept
	{
		{
			const std::scoped_lock lock{ m_mutex };
			if (m_notification != Notification::kNone)
				return;
			m_notification = Notification::kReady;
		}

		for (;;)
		{
			DMUI_HostReadyCallback callback{ nullptr };
			void* userData{ nullptr };
			DMUI_ClientHandle handle{ DMUI_INVALID_CLIENT_HANDLE };
			{
				const std::scoped_lock lock{ m_mutex };
				const auto found = std::ranges::find_if(m_clients, [](const auto& a_client) {
					return !a_client.notified;
				});
				if (found == m_clients.end())
					return;
				found->notified = true;
				callback = found->onHostReady;
				userData = found->userData;
				handle = found->handle;
			}

			if (!InvokeReady(callback, &a_info, userData))
			{
				const std::scoped_lock lock{ m_mutex };
				if (auto* client = FindClient(handle))
					client->callbackFailed = true;
				for (auto& page : m_pages)
				{
					if (page.client == handle)
						page.callbackFailed = true;
				}
				for (auto& action : m_actions)
				{
					if (action.client == handle)
						action.callbackFailed = true;
				}
			}
		}
	}

	void Registry::NotifyUnavailable(DMUI_UnavailableReason a_reason) noexcept
	{
		{
			const std::scoped_lock lock{ m_mutex };
			if (m_notification != Notification::kNone)
				return;
			m_notification = Notification::kUnavailable;
		}

		for (;;)
		{
			DMUI_HostUnavailableCallback callback{ nullptr };
			void* userData{ nullptr };
			DMUI_ClientHandle handle{ DMUI_INVALID_CLIENT_HANDLE };
			{
				const std::scoped_lock lock{ m_mutex };
				const auto found = std::ranges::find_if(m_clients, [](const auto& a_client) {
					return !a_client.notified;
				});
				if (found == m_clients.end())
					return;
				found->notified = true;
				callback = found->onHostUnavailable;
				userData = found->userData;
				handle = found->handle;
			}

			if (!InvokeUnavailable(callback, a_reason, userData))
			{
				const std::scoped_lock lock{ m_mutex };
				if (auto* client = FindClient(handle))
					client->callbackFailed = true;
			}
		}
	}

	const DMUI_ImGuiFingerprint& Registry::Fingerprint() const noexcept
	{
		return m_fingerprint;
	}

	bool Registry::SupportsVersion(uint32_t a_requestedVersion) noexcept
	{
		return a_requestedVersion == DMUI_API_VERSION_1_0;
	}

	bool Registry::FingerprintsMatch(
		const DMUI_ImGuiFingerprint& a_left,
		const DMUI_ImGuiFingerprint& a_right) noexcept
	{
		return std::memcmp(
				   a_left.upstreamCommit,
				   a_right.upstreamCommit,
				   sizeof(a_left.upstreamCommit)) == 0 &&
			a_left.imguiVersionNum == a_right.imguiVersionNum &&
			a_left.flags == a_right.flags &&
			a_left.sizeOfImGuiIO == a_right.sizeOfImGuiIO &&
			a_left.sizeOfImGuiStyle == a_right.sizeOfImGuiStyle &&
			a_left.sizeOfImVec2 == a_right.sizeOfImVec2 &&
			a_left.sizeOfImVec4 == a_right.sizeOfImVec4 &&
			a_left.sizeOfImDrawVert == a_right.sizeOfImDrawVert &&
			a_left.sizeOfImDrawIdx == a_right.sizeOfImDrawIdx &&
			a_left.alignOfImGuiIO == a_right.alignOfImGuiIO &&
			a_left.alignOfImGuiStyle == a_right.alignOfImGuiStyle &&
			a_left.alignOfImVec2 == a_right.alignOfImVec2 &&
			a_left.alignOfImVec4 == a_right.alignOfImVec4 &&
			a_left.alignOfImDrawVert == a_right.alignOfImDrawVert &&
			a_left.alignOfImDrawIdx == a_right.alignOfImDrawIdx &&
			a_left.sizeOfImWchar == a_right.sizeOfImWchar &&
			a_left.alignOfImWchar == a_right.alignOfImWchar &&
			a_left.sizeOfImTextureID == a_right.sizeOfImTextureID &&
			a_left.alignOfImTextureID == a_right.alignOfImTextureID &&
			a_left.sizeOfImGuiID == a_right.sizeOfImGuiID &&
			a_left.alignOfImGuiID == a_right.alignOfImGuiID &&
			a_left.sizeOfImFont == a_right.sizeOfImFont &&
			a_left.alignOfImFont == a_right.alignOfImFont &&
			a_left.sizeOfImFontConfig == a_right.sizeOfImFontConfig &&
			a_left.alignOfImFontConfig == a_right.alignOfImFontConfig &&
			a_left.sizeOfImFontGlyph == a_right.sizeOfImFontGlyph &&
			a_left.alignOfImFontGlyph == a_right.alignOfImFontGlyph &&
			a_left.sizeOfImGuiContext == a_right.sizeOfImGuiContext &&
			a_left.alignOfImGuiContext == a_right.alignOfImGuiContext &&
			a_left.sizeOfImGuiErrorRecoveryState == a_right.sizeOfImGuiErrorRecoveryState &&
			a_left.alignOfImGuiErrorRecoveryState == a_right.alignOfImGuiErrorRecoveryState &&
			a_left.sizeOfImGuiNextWindowData == a_right.sizeOfImGuiNextWindowData &&
			a_left.alignOfImGuiNextWindowData == a_right.alignOfImGuiNextWindowData &&
			a_left.sizeOfImGuiNextItemData == a_right.sizeOfImGuiNextItemData &&
			a_left.alignOfImGuiNextItemData == a_right.alignOfImGuiNextItemData &&
			a_left.sizeOfImGuiPopupData == a_right.sizeOfImGuiPopupData &&
			a_left.alignOfImGuiPopupData == a_right.alignOfImGuiPopupData &&
			a_left.offsetOfImDrawVertPos == a_right.offsetOfImDrawVertPos &&
			a_left.offsetOfImDrawVertUv == a_right.offsetOfImDrawVertUv &&
			a_left.offsetOfImDrawVertCol == a_right.offsetOfImDrawVertCol &&
			a_left.layoutSignature == a_right.layoutSignature;
	}

	RegisteredClient* Registry::FindClient(DMUI_ClientHandle a_client) noexcept
	{
		const auto found = std::ranges::find_if(m_clients, [&](const auto& a_existing) {
			return a_existing.handle == a_client;
		});
		return found != m_clients.end() ? &*found : nullptr;
	}

	const RegisteredClient* Registry::FindClient(DMUI_ClientHandle a_client) const noexcept
	{
		const auto found = std::ranges::find_if(m_clients, [&](const auto& a_existing) {
			return a_existing.handle == a_client;
		});
		return found != m_clients.end() ? &*found : nullptr;
	}

	RegisteredPage* Registry::FindPage(DMUI_PageHandle a_page) noexcept
	{
		const auto found = std::ranges::find_if(m_pages, [&](const auto& a_existing) {
			return a_existing.handle == a_page;
		});
		return found != m_pages.end() ? &*found : nullptr;
	}

	const RegisteredPage* Registry::FindPage(DMUI_PageHandle a_page) const noexcept
	{
		const auto found = std::ranges::find_if(m_pages, [&](const auto& a_existing) {
			return a_existing.handle == a_page;
		});
		return found != m_pages.end() ? &*found : nullptr;
	}

	RegisteredAction* Registry::FindAction(DMUI_ActionHandle a_action) noexcept
	{
		const auto found = std::ranges::find_if(m_actions, [&](const auto& a_existing) {
			return a_existing.handle == a_action;
		});
		return found != m_actions.end() ? &*found : nullptr;
	}

	const RegisteredAction* Registry::FindAction(DMUI_ActionHandle a_action) const noexcept
	{
		const auto found = std::ranges::find_if(m_actions, [&](const auto& a_existing) {
			return a_existing.handle == a_action;
		});
		return found != m_actions.end() ? &*found : nullptr;
	}

	bool Registry::OwnsPage(
		DMUI_ClientHandle a_client,
		DMUI_PageHandle a_page) const noexcept
	{
		const auto* page = FindPage(a_page);
		return page && page->client == a_client;
	}
}
