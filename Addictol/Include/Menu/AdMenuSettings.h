#pragma once

namespace Addictol::Menu
{
	void BeginSettingsPageFrame() noexcept;
	void EndSettingsPageFrame(bool a_menuVisible) noexcept;
	void CloseSettingsPage() noexcept;
	void DrawSettingsPage(void* a_userData) noexcept;
}
