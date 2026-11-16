#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace arcade_ui {

// Positions are UTF-16 code-unit offsets, matching DirectWrite hit testing.
class TextEditModel {
public:
    explicit TextEditModel(std::string_view utf8 = {});
    [[nodiscard]] std::string Utf8() const;
    [[nodiscard]] const std::wstring& Text() const noexcept { return text_; }
    [[nodiscard]] size_t Caret() const noexcept { return caret_; }
    [[nodiscard]] size_t Anchor() const noexcept { return anchor_; }
    [[nodiscard]] bool HasSelection() const noexcept { return caret_ != anchor_; }
    [[nodiscard]] std::wstring SelectedText() const;
    void SetCaret(size_t position, bool extend = false);
    void SelectAll();
    bool Insert(std::wstring_view value, bool multiline);
    bool Backspace(bool word = false);
    bool DeleteForward(bool word = false);
    void MoveHorizontal(bool right, bool extend, bool word = false);
    void MoveLineBoundary(bool end, bool extend, bool document = false);
    bool Undo();
    bool Redo();
    void BeginComposition();
    void UpdateComposition(std::wstring_view value, size_t cursor);
    bool CommitComposition(std::wstring_view value, bool multiline);
    void CancelComposition();
    [[nodiscard]] bool IsComposing() const noexcept { return composing_; }
    [[nodiscard]] std::wstring DisplayText() const;
    [[nodiscard]] size_t DisplayCaret() const noexcept;
    [[nodiscard]] size_t CompositionStart() const noexcept { return (std::min)(caret_, anchor_); }
    [[nodiscard]] size_t CompositionLength() const noexcept { return composition_.size(); }

private:
    struct Snapshot { std::wstring text; size_t caret; size_t anchor; };
    std::wstring text_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool composing_ = false;
    std::wstring composition_;
    size_t composition_cursor_ = 0;
    void SaveUndo();
    bool ReplaceSelection(std::wstring_view value);
    [[nodiscard]] size_t Previous(size_t position, bool word) const;
    [[nodiscard]] size_t Next(size_t position, bool word) const;
};

std::wstring Utf8ToUtf16(std::string_view value);
std::string Utf16ToUtf8(std::wstring_view value);

} // namespace arcade_ui
