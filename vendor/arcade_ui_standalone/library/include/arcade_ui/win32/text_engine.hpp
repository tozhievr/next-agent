// arcade_ui/win32/text_engine.hpp
//
// DirectWrite text measurement, shaping, and metrics engine (§12).
#pragma once

#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <string_view>
#include <vector>
#include <arcade_ui/core/types.hpp>

namespace arcade_ui {

struct TextMetrics {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    uint32_t line_count = 0;
};

struct TextCaretMetrics { float x = 0.0f; float y = 0.0f; float height = 18.0f; };

class DirectWriteTextEngine {
public:
    static DirectWriteTextEngine& Instance();

    DirectWriteTextEngine();
    ~DirectWriteTextEngine();

    // Measure text dimensions given available width constraint
    TextMetrics MeasureText(std::string_view text, float font_size_dip = 16.0f,
                            std::wstring_view font_family = L"Segoe UI",
                            float max_width_dip = 1e9f);
    uint32_t HitTestPoint(std::wstring_view text, float x, float y, float width, bool multiline);
    TextCaretMetrics CaretAt(std::wstring_view text, uint32_t position, float width, bool multiline);
    std::vector<Rect> SelectionRects(std::wstring_view text, uint32_t start,
                                     uint32_t length, float width, bool multiline);

    [[nodiscard]] bool IsValid() const noexcept { return factory_ != nullptr; }

private:
    Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateEditLayout(std::wstring_view text,
                                                                float width, bool multiline);
    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
};

} // namespace arcade_ui
