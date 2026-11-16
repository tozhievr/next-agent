module;
#include <arcade_ui/win32/messages.hpp>
#include <arcade_ui/win32/text_engine.hpp>
export module arcade_ui.win32;
export import arcade_ui;
export namespace arcade_ui {
    using ::arcade_ui::Win32MessageAdapter;
    using ::arcade_ui::DirectWriteTextEngine;
    using ::arcade_ui::TextMetrics;
    using ::arcade_ui::TextCaretMetrics;
}
