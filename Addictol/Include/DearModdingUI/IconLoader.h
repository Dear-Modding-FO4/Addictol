#pragma once

#include <DearModdingUI/IconPaths.h>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace Addictol::DearModdingUI::IconLoader
{
	void SetDevice(ID3D11Device* a_device) noexcept;
	void Shutdown() noexcept;
	[[nodiscard]] ID3D11ShaderResourceView* Get(
		IconKind a_kind,
		std::string_view a_name) noexcept;
}
