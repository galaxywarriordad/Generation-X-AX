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
