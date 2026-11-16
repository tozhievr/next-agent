#include <arcade_ui/core/text_edit.hpp>
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace arcade_ui {

std::wstring Utf8ToUtf16(std::string_view value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string Utf16ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

TextEditModel::TextEditModel(std::string_view utf8) : text_(Utf8ToUtf16(utf8)) {
    caret_ = anchor_ = text_.size();
}

std::string TextEditModel::Utf8() const { return Utf16ToUtf8(text_); }

std::wstring TextEditModel::SelectedText() const {
    const auto first = (std::min)(caret_, anchor_);
    return text_.substr(first, (std::max)(caret_, anchor_) - first);
}

void TextEditModel::SetCaret(size_t position, bool extend) {
    CancelComposition();
    caret_ = (std::min)(position, text_.size());
    if (caret_ && caret_ < text_.size() && text_[caret_] >= 0xDC00 && text_[caret_] <= 0xDFFF &&
        text_[caret_ - 1] >= 0xD800 && text_[caret_ - 1] <= 0xDBFF) --caret_;
    if (!extend) anchor_ = caret_;
}

void TextEditModel::SelectAll() { CancelComposition(); anchor_ = 0; caret_ = text_.size(); }

void TextEditModel::SaveUndo() {
    if (undo_.size() == 128) undo_.erase(undo_.begin());
    undo_.push_back({text_, caret_, anchor_});
    redo_.clear();
}

bool TextEditModel::ReplaceSelection(std::wstring_view value) {
    const auto first = (std::min)(caret_, anchor_);
    const auto last = (std::max)(caret_, anchor_);
    if (first == last && value.empty()) return false;
    if (text_.size() - (last - first) + value.size() > 16384) return false;
    SaveUndo();
    text_.replace(first, last - first, value);
    caret_ = anchor_ = first + value.size();
    return true;
}

bool TextEditModel::Insert(std::wstring_view value, bool multiline) {
    if (composing_) return false;
    if (value.empty()) return false;
    std::wstring filtered;
    filtered.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'\r') continue;
        if (ch == L'\n' || ch == L'\t') {
            if (multiline) filtered.push_back(ch);
        } else if (ch >= 0x20 || (ch >= 0xD800 && ch <= 0xDFFF)) {
            filtered.push_back(ch);
        }
    }
    return filtered.empty() ? false : ReplaceSelection(filtered);
}

size_t TextEditModel::Previous(size_t position, bool word) const {
    if (!position) return 0;
    --position;
    if (position && text_[position] >= 0xDC00 && text_[position] <= 0xDFFF &&
        text_[position - 1] >= 0xD800 && text_[position - 1] <= 0xDBFF) --position;
    if (word) {
        while (position && iswspace(text_[position])) --position;
        while (position && !iswspace(text_[position - 1])) --position;
    }
    return position;
}

size_t TextEditModel::Next(size_t position, bool word) const {
    if (position >= text_.size()) return text_.size();
    if (text_[position] >= 0xD800 && text_[position] <= 0xDBFF &&
        position + 1 < text_.size() && text_[position + 1] >= 0xDC00 && text_[position + 1] <= 0xDFFF) position += 2;
    else ++position;
    if (word) {
        while (position < text_.size() && !iswspace(text_[position])) ++position;
        while (position < text_.size() && iswspace(text_[position])) ++position;
    }
    return position;
}

bool TextEditModel::Backspace(bool word) {
    if (composing_) return false;
    if (HasSelection()) return ReplaceSelection({});
    if (!caret_) return false;
    const auto old_anchor = anchor_;
    anchor_ = Previous(caret_, word);
    const bool changed = ReplaceSelection({});
    if (changed) undo_.back().anchor = old_anchor;
    return changed;
}

bool TextEditModel::DeleteForward(bool word) {
    if (composing_) return false;
    if (HasSelection()) return ReplaceSelection({});
    if (caret_ == text_.size()) return false;
    const auto old_anchor = anchor_;
    anchor_ = Next(caret_, word);
    const bool changed = ReplaceSelection({});
    if (changed) undo_.back().anchor = old_anchor;
    return changed;
}

void TextEditModel::MoveHorizontal(bool right, bool extend, bool word) {
    if (!extend && HasSelection() && !word) {
        SetCaret(right ? (std::max)(caret_, anchor_) : (std::min)(caret_, anchor_));
    } else SetCaret(right ? Next(caret_, word) : Previous(caret_, word), extend);
}

void TextEditModel::MoveLineBoundary(bool end, bool extend, bool document) {
    if (document) { SetCaret(end ? text_.size() : 0, extend); return; }
    if (end) {
        auto pos = text_.find(L'\n', caret_);
        SetCaret(pos == std::wstring::npos ? text_.size() : pos, extend);
    } else {
        auto pos = caret_ ? text_.rfind(L'\n', caret_ - 1) : std::wstring::npos;
        SetCaret(pos == std::wstring::npos ? 0 : pos + 1, extend);
    }
}

bool TextEditModel::Undo() {
    if (composing_) return false;
    if (undo_.empty()) return false;
    redo_.push_back({text_, caret_, anchor_});
    auto old = std::move(undo_.back()); undo_.pop_back();
    text_ = std::move(old.text); caret_ = old.caret; anchor_ = old.anchor;
    return true;
}

bool TextEditModel::Redo() {
    if (composing_) return false;
    if (redo_.empty()) return false;
    undo_.push_back({text_, caret_, anchor_});
    auto next = std::move(redo_.back()); redo_.pop_back();
    text_ = std::move(next.text); caret_ = next.caret; anchor_ = next.anchor;
    return true;
}

void TextEditModel::BeginComposition() {
    if (composing_) return;
    composing_ = true;
    composition_.clear();
    composition_cursor_ = 0;
}

void TextEditModel::UpdateComposition(std::wstring_view value, size_t cursor) {
    BeginComposition();
    if (value.size() > 16384) return;
    composition_ = value;
    composition_cursor_ = (std::min)(cursor, composition_.size());
    if (composition_cursor_ && composition_cursor_ < composition_.size() &&
        composition_[composition_cursor_] >= 0xDC00 && composition_[composition_cursor_] <= 0xDFFF &&
        composition_[composition_cursor_ - 1] >= 0xD800 && composition_[composition_cursor_ - 1] <= 0xDBFF)
        --composition_cursor_;
}

bool TextEditModel::CommitComposition(std::wstring_view value, bool multiline) {
    CancelComposition();
    return Insert(value, multiline);
}

void TextEditModel::CancelComposition() {
    composing_ = false;
    composition_.clear();
    composition_cursor_ = 0;
}

std::wstring TextEditModel::DisplayText() const {
    if (!composing_) return text_;
    const size_t first = (std::min)(caret_, anchor_);
    const size_t last = (std::max)(caret_, anchor_);
    return text_.substr(0, first) + composition_ + text_.substr(last);
}

size_t TextEditModel::DisplayCaret() const noexcept {
    return composing_ ? CompositionStart() + composition_cursor_ : caret_;
}

} // namespace arcade_ui
