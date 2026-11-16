// arcade_ui/src/win32/text_engine.cpp
#include <arcade_ui/win32/text_engine.hpp>
#include <windows.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwrite.lib")

namespace arcade_ui {

DirectWriteTextEngine& DirectWriteTextEngine::Instance() {
    static DirectWriteTextEngine s_instance;
    return s_instance;
}

DirectWriteTextEngine::DirectWriteTextEngine() {
    ::DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory_.GetAddressOf())
    );
}

DirectWriteTextEngine::~DirectWriteTextEngine() = default;

TextMetrics DirectWriteTextEngine::MeasureText(std::string_view text, float font_size_dip,
                                              std::wstring_view font_family,
                                              float max_width_dip) {
    TextMetrics result{};
    if (!factory_ || text.empty()) {
        return result;
    }

    // Convert UTF-8 to UTF-16
    const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wlen <= 0) return result;

    std::wstring wtext(wlen, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wtext.data(), wlen);

    // Create Text Format
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory_->CreateTextFormat(
            font_family.data(), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            font_size_dip, L"en-us", &format))) {
        return result;
    }

    // Create Text Layout
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory_->CreateTextLayout(
            wtext.data(), static_cast<UINT32>(wtext.size()),
            format.Get(), max_width_dip, 1e9f, &layout))) {
        return result;
    }

    DWRITE_TEXT_METRICS m{};
    if (SUCCEEDED(layout->GetMetrics(&m))) {
        result.width = m.widthIncludingTrailingWhitespace;
        result.height = m.height;
        result.line_count = m.lineCount;
    }

    return result;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> DirectWriteTextEngine::CreateEditLayout(
    std::wstring_view text, float width, bool multiline) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!factory_) return layout;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &format))) return layout;
    if (FAILED(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.Get(),
        (std::max)(1.0f, width), 100000.0f, &layout))) return {};
    layout->SetWordWrapping(multiline ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return layout;
}

uint32_t DirectWriteTextEngine::HitTestPoint(std::wstring_view text, float x, float y,
                                              float width, bool multiline) {
    auto layout = CreateEditLayout(text, width, multiline);
    if (!layout) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestPoint(x, y, &trailing, &inside, &metrics))) return 0;
    return (std::min)(metrics.textPosition + (trailing ? metrics.length : 0u),
                      static_cast<uint32_t>(text.size()));
}

TextCaretMetrics DirectWriteTextEngine::CaretAt(std::wstring_view text, uint32_t position,
                                                float width, bool multiline) {
    TextCaretMetrics result{};
    auto layout = CreateEditLayout(text, width, multiline);
    if (!layout) return result;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (SUCCEEDED(layout->HitTestTextPosition((std::min)(position, static_cast<uint32_t>(text.size())),
        FALSE, &result.x, &result.y, &metrics))) result.height = metrics.height;
    return result;
}

std::vector<Rect> DirectWriteTextEngine::SelectionRects(std::wstring_view text,
    uint32_t start, uint32_t length, float width, bool multiline) {
    std::vector<Rect> result;
    if (!length) return result;
    auto layout = CreateEditLayout(text, width, multiline);
    if (!layout) return result;
    UINT32 count = 0;
    layout->HitTestTextRange(start, length, 0.0f, 0.0f, nullptr, 0, &count);
    if (!count) return result;
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    if (FAILED(layout->HitTestTextRange(start, length, 0.0f, 0.0f,
        metrics.data(), count, &count))) return result;
    for (UINT32 i = 0; i < count; ++i)
        result.push_back(Rect{metrics[i].left, metrics[i].top, metrics[i].width, metrics[i].height});
    return result;
}

} // namespace arcade_ui
