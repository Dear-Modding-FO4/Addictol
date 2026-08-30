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

		void DMUI_CALL DrawSynthesizedHome(void*) noexcept
		{}
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
			a_descriptor->kind != DMUI_PAGE_KIND_OVERLAY &&
			a_descriptor->kind != DMUI_PAGE_KIND_HOME)
			return DMUI_RESULT_INVALID_PAGE_KIND;

		try
		{
			RegisteredPage page{};
			page.client = a_client;
			page.sortKey = a_descriptor->sortKey;
			page.kind = a_descriptor->kind;
			page.draw = a_descriptor->draw;
			page.userData = a_descriptor->userData;
			const auto categoryOptional =
				a_descriptor->kind == DMUI_PAGE_KIND_HOME;
			if (!ReadString(a_descriptor->id, kIdCapacity, false, page.id) ||
				!ReadString(a_descriptor->displayName, kDisplayNameCapacity, false, page.displayName) ||
				!ReadString(a_descriptor->category, kCategoryCapacity, categoryOptional, page.category) ||
				!ReadString(a_descriptor->summary, kSummaryCapacity, true, page.summary) ||
				!ValidId(page.id) ||
				!ValidText(page.displayName, false) ||
				!ValidText(page.category, categoryOptional) ||
				!ValidText(page.summary, true))
				return DMUI_RESULT_INVALID_DESCRIPTOR;

			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return DMUI_RESULT_REGISTRATION_CLOSED;
			auto* client = FindClient(a_client);
			if (!client)
				return DMUI_RESULT_CLIENT_NOT_FOUND;
			if (page.kind == DMUI_PAGE_KIND_HOME &&
				std::ranges::any_of(m_pages, [&](const auto& a_existing) {
					return a_existing.client == a_client &&
						a_existing.kind == DMUI_PAGE_KIND_HOME;
				}))
			{
				if (!client->duplicateHomeLogged)
				{
					client->duplicateHomeLogged = true;
					client->duplicateHomeWarningPending = true;
				}
				return DMUI_RESULT_DUPLICATE_PAGE_ID;
			}
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

	bool Registry::Freeze() noexcept
	{
		try
		{
			const std::scoped_lock lock{ m_mutex };
			if (!m_open)
				return false;
			if (!SynthesizeHomePages())
				return false;
			std::ranges::sort(m_pages, [](const auto& a_left, const auto& a_right) {
				return std::tuple(
					a_left.clientOrigin,
					a_left.clientDisplayName,
					a_left.clientId,
					a_left.kind == DMUI_PAGE_KIND_HOME ? 0 : 1,
					a_left.category,
					a_left.sortKey,
					a_left.displayName,
					a_left.id) <
					std::tuple(
						a_right.clientOrigin,
						a_right.clientDisplayName,
						a_right.clientId,
						a_right.kind == DMUI_PAGE_KIND_HOME ? 0 : 1,
						a_right.category,
						a_right.sortKey,
						a_right.displayName,
						a_right.id);
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
			return a_page.kind == DMUI_PAGE_KIND_HOME ||
				a_page.kind == DMUI_PAGE_KIND_SETTINGS;
		});
	}

	const std::vector<RegisteredPage>& Registry::OrderedPages() const noexcept
	{
		return m_pages;
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

	bool Registry::ConsumeDuplicateHomeWarning(
		DMUI_ClientHandle a_client) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		auto* client = FindClient(a_client);
		if (!client || !client->duplicateHomeWarningPending)
			return false;
		client->duplicateHomeWarningPending = false;
		return true;
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
					if (page.client == handle && !page.synthesized)
						page.callbackFailed = true;
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

	bool Registry::SynthesizeHomePages()
	{
		std::vector<RegisteredPage> homes;
		homes.reserve(m_clients.size());
		auto nextPage = m_nextPage;
		for (const auto& client : m_clients)
		{
			const auto hasHome = std::ranges::any_of(m_pages, [&](const auto& a_page) {
				return a_page.client == client.handle &&
					a_page.kind == DMUI_PAGE_KIND_HOME;
			});
			if (hasHome)
				continue;
			if (nextPage == DMUI_INVALID_PAGE_HANDLE)
				return false;

			RegisteredPage page{};
			page.handle = nextPage++;
			page.client = client.handle;
			page.clientId = client.id;
			page.clientDisplayName = client.displayName;
			page.clientOrigin = client.origin;
			page.id = "#dearmoddingui-home";
			page.displayName = "Home";
			page.summary = "Overview, version, and registered page status.";
			page.imguiLabel =
				page.displayName + "###" + page.clientId + "/" + page.id;
			page.kind = DMUI_PAGE_KIND_HOME;
			page.draw = &DrawSynthesizedHome;
			page.synthesized = true;
			homes.push_back(std::move(page));
		}

		m_pages.reserve(m_pages.size() + homes.size());
		for (auto& home : homes)
			m_pages.push_back(std::move(home));
		m_nextPage = nextPage;
		return true;
	}

	bool Registry::OwnsPage(
		DMUI_ClientHandle a_client,
		DMUI_PageHandle a_page) const noexcept
	{
		const auto* page = FindPage(a_page);
		return page && page->client == a_client;
	}
}
