#pragma once

#include <DearModdingUI/API.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#if !defined(IMGUI_VERSION) || !defined(IMGUI_VERSION_NUM)
#error "Include imgui.h and imgui_internal.h before ImGuiFingerprint.h"
#endif

namespace DearModdingUIFingerprintDetail
{
	inline constexpr uint64_t kHashOffset{ 14695981039346656037ull };
	inline constexpr uint64_t kHashPrime{ 1099511628211ull };

	inline void HashValue(uint64_t& a_hash, uint64_t a_value) noexcept
	{
		for (uint32_t shift = 0; shift < 64; shift += 8)
		{
			a_hash ^= (a_value >> shift) & 0xFFu;
			a_hash *= kHashPrime;
		}
	}

	template <class Type>
	inline void HashType(uint64_t& a_hash) noexcept
	{
		HashValue(a_hash, sizeof(Type));
		HashValue(a_hash, alignof(Type));
	}

	inline uint64_t BuildLayoutSignature() noexcept
	{
		uint64_t hash{ kHashOffset };
		HashType<ImGuiIO>(hash);
		HashValue(hash, offsetof(ImGuiIO, ConfigFlags));
		HashValue(hash, offsetof(ImGuiIO, DisplaySize));
		HashValue(hash, offsetof(ImGuiIO, Fonts));
		HashValue(hash, offsetof(ImGuiIO, FontDefault));
		HashValue(hash, offsetof(ImGuiIO, WantCaptureMouse));
		HashValue(hash, offsetof(ImGuiIO, InputQueueCharacters));
		HashType<ImGuiStyle>(hash);
		HashValue(hash, offsetof(ImGuiStyle, Alpha));
		HashValue(hash, offsetof(ImGuiStyle, WindowPadding));
		HashValue(hash, offsetof(ImGuiStyle, Colors));
		HashType<ImVec2>(hash);
		HashValue(hash, offsetof(ImVec2, x));
		HashValue(hash, offsetof(ImVec2, y));
		HashType<ImVec4>(hash);
		HashValue(hash, offsetof(ImVec4, x));
		HashValue(hash, offsetof(ImVec4, w));
		HashType<ImDrawVert>(hash);
		HashValue(hash, offsetof(ImDrawVert, pos));
		HashValue(hash, offsetof(ImDrawVert, uv));
		HashValue(hash, offsetof(ImDrawVert, col));
		HashType<ImDrawIdx>(hash);
		HashType<ImWchar>(hash);
		HashType<ImTextureID>(hash);
		HashType<ImGuiID>(hash);
		HashType<ImTextureRef>(hash);
		HashType<ImDrawCmd>(hash);
		HashType<ImDrawList>(hash);
		HashType<ImDrawData>(hash);
		HashType<ImGuiViewport>(hash);
		HashType<ImGuiPlatformIO>(hash);
		HashType<ImFont>(hash);
		HashValue(hash, offsetof(ImFont, LastBaked));
		HashValue(hash, offsetof(ImFont, OwnerAtlas));
		HashValue(hash, offsetof(ImFont, FontId));
		HashValue(hash, offsetof(ImFont, Sources));
		HashValue(hash, offsetof(ImFont, EllipsisChar));
		HashValue(hash, offsetof(ImFont, RemapPairs));
		HashType<ImFontConfig>(hash);
		HashValue(hash, offsetof(ImFontConfig, Name));
		HashValue(hash, offsetof(ImFontConfig, FontData));
		HashValue(hash, offsetof(ImFontConfig, EllipsisChar));
		HashValue(hash, offsetof(ImFontConfig, GlyphRanges));
		HashValue(hash, offsetof(ImFontConfig, GlyphOffset));
		HashValue(hash, offsetof(ImFontConfig, DstFont));
		HashType<ImFontGlyph>(hash);
		HashValue(hash, offsetof(ImFontGlyph, AdvanceX));
		HashValue(hash, offsetof(ImFontGlyph, X0));
		HashValue(hash, offsetof(ImFontGlyph, U0));
		HashValue(hash, offsetof(ImFontGlyph, PackId));
		HashType<ImFontAtlas>(hash);
		HashType<ImFontBaked>(hash);
		HashType<ImGuiContext>(hash);
		HashValue(hash, offsetof(ImGuiContext, IO));
		HashValue(hash, offsetof(ImGuiContext, Style));
		HashValue(hash, offsetof(ImGuiContext, Font));
		HashValue(hash, offsetof(ImGuiContext, CurrentWindow));
		HashValue(hash, offsetof(ImGuiContext, ColorStack));
		HashValue(hash, offsetof(ImGuiContext, NextWindowData));
		HashValue(hash, offsetof(ImGuiContext, NextItemData));
		HashValue(hash, offsetof(ImGuiContext, OpenPopupStack));
		HashValue(hash, offsetof(ImGuiContext, BeginPopupStack));
		HashValue(hash, offsetof(ImGuiContext, StackSizesInNewFrame));
		HashType<ImGuiWindow>(hash);
		HashType<ImGuiTable>(hash);
		HashType<ImGuiTabBar>(hash);
		HashType<ImGuiErrorRecoveryState>(hash);
		HashValue(hash, offsetof(ImGuiErrorRecoveryState, SizeOfWindowStack));
		HashValue(hash, offsetof(ImGuiErrorRecoveryState, SizeOfDisabledStack));
		HashType<ImGuiNextWindowData>(hash);
		HashValue(hash, offsetof(ImGuiNextWindowData, HasFlags));
		HashValue(hash, offsetof(ImGuiNextWindowData, PosVal));
		HashValue(hash, offsetof(ImGuiNextWindowData, WindowClass));
		HashType<ImGuiNextItemData>(hash);
		HashValue(hash, offsetof(ImGuiNextItemData, HasFlags));
		HashValue(hash, offsetof(ImGuiNextItemData, FocusScopeId));
		HashValue(hash, offsetof(ImGuiNextItemData, RefVal));
		HashType<ImGuiPopupData>(hash);
		HashValue(hash, offsetof(ImGuiPopupData, PopupId));
		HashValue(hash, offsetof(ImGuiPopupData, Window));
		HashValue(hash, offsetof(ImGuiPopupData, OpenPopupPos));
		return hash;
	}
}

[[nodiscard]] inline DMUI_ImGuiFingerprint DMUI_MakeImGuiFingerprint() noexcept
{
	DMUI_ImGuiFingerprint result{};
	result.structSize = sizeof(result);
	std::memcpy(
		result.upstreamCommit,
		DMUI_IMGUI_UPSTREAM_COMMIT,
		sizeof(result.upstreamCommit));
	result.imguiVersionNum = IMGUI_VERSION_NUM;
#if defined(IMGUI_HAS_DOCK)
	result.flags |= DMUI_IMGUI_FINGERPRINT_DOCKING;
#endif
#if defined(IMGUI_USE_WCHAR32)
	result.flags |= DMUI_IMGUI_FINGERPRINT_WCHAR32;
#endif
#if defined(ImTextureID)
	result.flags |= DMUI_IMGUI_FINGERPRINT_CUSTOM_TEXTURE_ID;
#endif
#if defined(IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT)
	result.flags |= DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_VERT;
#endif
#if defined(IMGUI_USE_BGRA_PACKED_COLOR)
	result.flags |= DMUI_IMGUI_FINGERPRINT_BGRA_PACKED_COLOR;
#endif
#if defined(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
	result.flags |= DMUI_IMGUI_FINGERPRINT_OBSOLETE_DISABLED;
#endif
#if defined(IMGUI_ENABLE_TEST_ENGINE)
	result.flags |= DMUI_IMGUI_FINGERPRINT_TEST_ENGINE;
#endif
#if defined(IMGUI_USE_LEGACY_CRC32_ADLER)
	result.flags |= DMUI_IMGUI_FINGERPRINT_LEGACY_CRC32;
#endif
#if defined(IMGUI_ENABLE_FREETYPE)
	result.flags |= DMUI_IMGUI_FINGERPRINT_FREETYPE;
#endif
#if defined(IMGUI_DEFINE_MATH_OPERATORS)
	result.flags |= DMUI_IMGUI_FINGERPRINT_MATH_OPERATORS;
#endif
#if defined(IMGUI_DISABLE_DEBUG_TOOLS)
	result.flags |= DMUI_IMGUI_FINGERPRINT_DEBUG_TOOLS_DISABLED;
#endif
#if defined(ImDrawIdx)
	result.flags |= DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_IDX;
#endif
#if defined(ImDrawCallback)
	result.flags |= DMUI_IMGUI_FINGERPRINT_CUSTOM_DRAW_CALLBACK;
#endif
#if defined(IM_VEC2_CLASS_EXTRA)
	result.flags |= DMUI_IMGUI_FINGERPRINT_VEC2_EXTRA;
#endif
#if defined(IM_VEC4_CLASS_EXTRA)
	result.flags |= DMUI_IMGUI_FINGERPRINT_VEC4_EXTRA;
#endif
	result.sizeOfImGuiIO = sizeof(ImGuiIO);
	result.sizeOfImGuiStyle = sizeof(ImGuiStyle);
	result.sizeOfImVec2 = sizeof(ImVec2);
	result.sizeOfImVec4 = sizeof(ImVec4);
	result.sizeOfImDrawVert = sizeof(ImDrawVert);
	result.sizeOfImDrawIdx = sizeof(ImDrawIdx);
	result.alignOfImGuiIO = alignof(ImGuiIO);
	result.alignOfImGuiStyle = alignof(ImGuiStyle);
	result.alignOfImVec2 = alignof(ImVec2);
	result.alignOfImVec4 = alignof(ImVec4);
	result.alignOfImDrawVert = alignof(ImDrawVert);
	result.alignOfImDrawIdx = alignof(ImDrawIdx);
	result.sizeOfImWchar = sizeof(ImWchar);
	result.alignOfImWchar = alignof(ImWchar);
	result.sizeOfImTextureID = sizeof(ImTextureID);
	result.alignOfImTextureID = alignof(ImTextureID);
	result.sizeOfImGuiID = sizeof(ImGuiID);
	result.alignOfImGuiID = alignof(ImGuiID);
	result.sizeOfImFont = sizeof(ImFont);
	result.alignOfImFont = alignof(ImFont);
	result.sizeOfImFontConfig = sizeof(ImFontConfig);
	result.alignOfImFontConfig = alignof(ImFontConfig);
	result.sizeOfImFontGlyph = sizeof(ImFontGlyph);
	result.alignOfImFontGlyph = alignof(ImFontGlyph);
	result.sizeOfImGuiContext = sizeof(ImGuiContext);
	result.alignOfImGuiContext = alignof(ImGuiContext);
	result.sizeOfImGuiErrorRecoveryState = sizeof(ImGuiErrorRecoveryState);
	result.alignOfImGuiErrorRecoveryState = alignof(ImGuiErrorRecoveryState);
	result.sizeOfImGuiNextWindowData = sizeof(ImGuiNextWindowData);
	result.alignOfImGuiNextWindowData = alignof(ImGuiNextWindowData);
	result.sizeOfImGuiNextItemData = sizeof(ImGuiNextItemData);
	result.alignOfImGuiNextItemData = alignof(ImGuiNextItemData);
	result.sizeOfImGuiPopupData = sizeof(ImGuiPopupData);
	result.alignOfImGuiPopupData = alignof(ImGuiPopupData);
	result.offsetOfImDrawVertPos = offsetof(ImDrawVert, pos);
	result.offsetOfImDrawVertUv = offsetof(ImDrawVert, uv);
	result.offsetOfImDrawVertCol = offsetof(ImDrawVert, col);
	result.layoutSignature = DearModdingUIFingerprintDetail::BuildLayoutSignature();
	return result;
}
