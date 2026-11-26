#pragma once

#include "ui/ax_retro_font.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace ax::ui::retro {

struct FramebufferView {
    std::uint32_t* pixels{nullptr};
    int width{0};
    int height{0};
    int stride_pixels{0};

    bool valid() const noexcept {
        return pixels != nullptr && width > 0 && height > 0 && stride_pixels >= width;
    }
};

struct RetroBevelPalette {
    std::uint32_t fill{0xFF1C202A};
    std::uint32_t light{0xFF4A5268};
    std::uint32_t dark{0xFF0A0C12};
    std::uint32_t accent{0xFFE07A38};
};

inline void PutPixel(const FramebufferView& fb, int x, int y, std::uint32_t argb) noexcept {
    if (!fb.valid()) return;
    if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) return;
    fb.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.stride_pixels) +
              static_cast<std::size_t>(x)] = argb;
}

inline void FillRect(const FramebufferView& fb, int x0, int y0, int x1, int y1, std::uint32_t argb) noexcept {
    if (!fb.valid()) return;
    const int min_x = std::max(0, std::min(x0, x1));
    const int max_x = std::min(fb.width - 1, std::max(x0, x1));
    const int min_y = std::max(0, std::min(y0, y1));
    const int max_y = std::min(fb.height - 1, std::max(y0, y1));
    for (int y = min_y; y <= max_y; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.stride_pixels);
        for (int x = min_x; x <= max_x; ++x) {
            fb.pixels[row + static_cast<std::size_t>(x)] = argb;
        }
    }
}

inline void DrawBox3D(const FramebufferView& fb,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      const RetroBevelPalette& pal = {},
                      bool recessed = false) noexcept {
    if (!fb.valid()) return;
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);

    const std::uint32_t top_left = recessed ? pal.dark : pal.light;
    const std::uint32_t bot_right = recessed ? pal.light : pal.dark;

    FillRect(fb, x0 + 1, y0 + 1, x1 - 1, y1 - 1, pal.fill);
    FillRect(fb, x0, y0, x1, y0, top_left);
    FillRect(fb, x0, y0, x0, y1, top_left);
    FillRect(fb, x0 + 1, y1, x1, y1, bot_right);
    FillRect(fb, x1, y0 + 1, x1, y1, bot_right);
}

inline void DrawTextShadowed(const FramebufferView& fb,
                             const RetroBitmapFont& font,
                             int x,
                             int y,
                             std::string_view text,
                             std::uint32_t fg_argb = 0xFFF4F6FF,
                             std::uint32_t shadow_argb = 0xFF080A14,
                             int scale = 1) noexcept {
    if (!fb.valid() || text.empty()) return;
    const int s = std::max(1, scale);
    const int advance = (RetroBitmapFont::kGlyphWidth + 1) * s;

    int cursor_x = x;
    for (char ch : text) {
        const auto glyph = font.Glyph(font.MapAsciiToGlyph(ch));
        for (int row = 0; row < RetroBitmapFont::kGlyphHeight; ++row) {
            const std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
            for (int col = 0; col < RetroBitmapFont::kGlyphWidth; ++col) {
                if ((bits & (0x80u >> col)) != 0) {
                    const int px = cursor_x + col * s;
                    const int py = y + row * s;
                    FillRect(fb, px + s, py + s, px + 2 * s - 1, py + 2 * s - 1, shadow_argb);
                    FillRect(fb, px, py, px + s - 1, py + s - 1, fg_argb);
                }
            }
        }
        cursor_x += advance;
    }
}

inline void ApplyScanlines(const FramebufferView& fb, int darken_pct) noexcept {
    if (!fb.valid()) return;
    const int clamped = std::clamp(darken_pct, 0, 100);
    if (clamped == 0) return;

    const float factor = 1.0F - (static_cast<float>(clamped) / 100.0F);
    for (int y = 1; y < fb.height; y += 2) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.stride_pixels);
        for (int x = 0; x < fb.width; ++x) {
            const std::uint32_t p = fb.pixels[row + static_cast<std::size_t>(x)];
            const std::uint32_t a = p & 0xFF000000u;
            const auto r = static_cast<std::uint32_t>(static_cast<float>((p >> 16) & 0xFFu) * factor);
            const auto g = static_cast<std::uint32_t>(static_cast<float>((p >> 8) & 0xFFu) * factor);
            const auto b = static_cast<std::uint32_t>(static_cast<float>(p & 0xFFu) * factor);
            fb.pixels[row + static_cast<std::size_t>(x)] =
                a | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
        }
    }
}

} // namespace ax::ui::retro
