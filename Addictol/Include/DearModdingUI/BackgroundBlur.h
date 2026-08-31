#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;

namespace Addictol::DearModdingUI::BackgroundBlur
{
	void BeginFrame() noexcept;
	void SetHostWindow(float a_minX, float a_minY, float a_maxX, float a_maxY, float a_rounding) noexcept;
	void InvalidateBackBuffer() noexcept;
	void ResetDeviceResources() noexcept;
	void Render(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_backBuffer,
		ID3D11RenderTargetView* a_backBufferView) noexcept;
}
