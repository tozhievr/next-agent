#include "harness.hpp"
#include "src/platform.h"
#include "vendor/arcade_ui_standalone/examples/native_graphics.hpp"
#include <algorithm>
#include <arcade_ui/core/canvas.hpp>
#include <arcade_ui/dx12/ui_renderer.hpp>
#include <arcade_ui/win32/messages.hpp>
#include <chrono>
#include <commdlg.h>
#include <iomanip>
#include <map>
#include <psapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <sstream>
#include <windows.h>

using namespace arcade_ui;
using namespace nextagent::harness;
static bool automated = false;
using Object = lx::host::JsonObject;
using Array = lx::host::JsonArray;
namespace {
constexpr uint32_t BG = 0x101116ff, PANEL = 0x16171dff, LINE = 0x2d2e38ff, TEXT = 0xe9e9efff,
                   MUTED = 0x9396a5ff, VIOLET = 0xad98ffff, GREEN = 0x78df8eff;
std::string utf8(std::wstring_view text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string value(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), value.data(), n, nullptr, nullptr);
    return value;
}
std::wstring wide(std::string_view text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring value(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), value.data(), n);
    return value;
}
std::string clip(std::string text, size_t count) {
    size_t byte = cp_to_byte(text.data(), text.size(), count);
    if (byte != size_t(-1) && byte < text.size())
        return text.substr(0, byte) + "…";
    return text;
}
std::string number(double value, int precision = 1) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}
struct App {
    HWND window = nullptr;
    float width = 1520, height = 900, dpi = 96;
    bool dirty = true, settings = false, readonly = false, smoke = false;
    int tab = 0, selected = 0, file_offset = 0, session_offset = 0;
    uint64_t pending = 0, id = 20;
    float scroll = 0, max_scroll = 0;
    bool follow_tail = true;
    size_t observed_changes = 0;
    Config config;
    std::string state_dir, protected_key, form_model, form_key, error, viewer, viewer_title;
    double cpu = 0, memory = 0;
    std::shared_ptr<Workspace> workspace, state;
    struct Chat {
        std::shared_ptr<Workspace> workspace;
        std::shared_ptr<Session> session;
    };
    std::vector<Chat> chats;
    std::vector<Json> files;
    std::map<uint64_t, std::function<void()>> actions;
    UiCanvas main{CanvasDocument{}}, composer{CanvasDocument{}}, dialog{CanvasDocument{}};
    Win32MessageAdapter main_input, composer_input, dialog_input;
    enum Focus { Main, Composer, Dialog } focus = Composer;
    std::chrono::steady_clock::time_point sample = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    uint64_t previous_cpu = 0;
    float left = 238, right = 370;
    float center() const { return width - left - right; }
    View view() const { return chats.empty() ? View{} : chats[selected].session->snapshot(); }
    std::string input(UiCanvas &canvas, uint64_t element) {
        auto instance = canvas.Instance(element);
        return instance ? canvas.Host().Storage().GetText(*instance, 0) : "";
    }
    ~App() {
        for (auto &chat : chats)
            chat.session->cancel();
        for (auto &chat : chats)
            chat.session->wait();
        if (window && IsWindow(window))
            DestroyWindow(window);
    }
    void notify() { dirty = true; }
    void attach(std::shared_ptr<Workspace> root, std::shared_ptr<Session> session) {
        session->set_notify([this] {
            if (window)
                PostMessageW(window, WM_APP + 1, 0, 0);
        });
        chats.push_back({std::move(root), std::move(session)});
    }
    void fresh() {
        auto session =
            std::make_shared<Session>(workspace, state, smoke ? offline_transport() : http_transport());
        attach(workspace, session);
        selected = int(chats.size()) - 1;
        viewer.clear();
        viewer_title.clear();
        scroll = 0;
        follow_tail = true;
        dirty = true;
    }
    void load(std::string root) {
        state = std::make_shared<Workspace>(state_dir);
        try {
            auto text = state->read("settings.json");
            if (!text.empty()) {
                auto data = parse(text);
                if (root.empty())
                    root = string(data, "workspace");
                if (!string(data, "model").empty())
                    config.model = string(data, "model");
                protected_key = string(data, "protected_key");
                config.api_key = unprotect_key(protected_key);
                readonly = data.at("read_only").as_bool();
            }
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (auto key = getenv("OPENROUTER_API_KEY"))
            config.api_key = key;
        if (root.empty())
            root = ".";
        workspace = std::make_shared<Workspace>(root);
        if (!smoke) {
            for (auto &file : state->tree("sessions").at("entries").as_array()) {
                auto path = string(file, "path");
                if (!path.ends_with(".json"))
                    continue;
                try {
                    auto data = parse(state->read(path));
                    auto w = std::make_shared<Workspace>(string(data, "workspace"));
                    auto session = std::make_shared<Session>(w, state, http_transport(), string(data, "id"));
                    session->restore(data);
                    attach(w, session);
                } catch (...) {
                }
            }
        }
        if (chats.empty())
            fresh();
        else {
            selected = int(chats.size()) - 1;
            workspace = chats[selected].workspace;
        }
        refresh_files();
        reset_composer();
    }
    void refresh_files() {
        files = workspace->tree().at("entries").as_array();
        file_offset = 0;
        dirty = true;
    }
    void save_settings() {
        if (!form_model.empty())
            config.model = form_model;
        if (!form_key.empty()) {
            config.api_key = form_key;
            protected_key = protect_key(form_key);
            form_key.clear();
        }
        state->write("settings.json", Json(Object{{"model", config.model},
                                                  {"workspace", workspace->root()},
                                                  {"protected_key", protected_key},
                                                  {"read_only", readonly}})
                                          .dump());
        settings = false;
        dirty = true;
    }
    void choose_workspace() {
        IFileDialog *picker = nullptr;
        if (FAILED(
                CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
            return;
        picker->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(picker->Show(window))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(picker->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    try {
                        workspace = std::make_shared<Workspace>(utf8(path));
                        fresh();
                        refresh_files();
                    } catch (const std::exception &e) {
                        error = e.what();
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        picker->Release();
    }
    void send() {
        auto prompt = input(composer, 1);
        if (prompt.empty() || view().running)
            return;
        config.read_only = readonly;
        if (smoke) {
            config.endpoint = "offline://self-test";
            config.model = "offline-self-test";
        }
        if (chats[selected].session->start(prompt, config)) {
            reset_composer(true);
            dirty = true;
            scroll = 0;
            follow_tail = true;
        }
    }
    void reset_composer(bool clear = false) {
        std::string draft = clear ? "" : input(composer, 1);
        float cw = center();
        CanvasDocument d;
        d.width = width;
        d.height = height;
        d.background = BG;
        d.elements = {{1,
                       CanvasKind::Textarea,
                       "prompt",
                       draft,
                       {left + 24, height - 116, cw - 112, 82},
                       0x1a1b23ff,
                       TEXT,
                       LINE,
                       10},
                      {2,
                       CanvasKind::Button,
                       "send",
                       "➤",
                       {left + cw - 76, height - 116, 52, 52},
                       0x9c86f3ff,
                       0x14121eff,
                       0,
                       10}};
        composer.Reset(std::move(d));
        composer.SetOnAction([this](uint64_t) { pending = 2; });
    }
    void open_settings() {
        settings = true;
        form_model = config.model;
        form_key.clear();
        build_dialog();
        dirty = true;
        focus = Dialog;
    }
    void build_dialog() {
        float x = width * .5f - 260, y = height * .5f - 215;
        CanvasDocument d;
        d.width = width;
        d.height = height;
        d.background = BG;
        auto add = [&](uint64_t n, CanvasKind kind, std::string text, Rect bounds, uint32_t bg,
                       uint32_t fg = TEXT) {
            d.elements.push_back({n, kind, "settings", std::move(text), bounds, bg, fg,
                                  kind == CanvasKind::Text ? 0 : LINE, 8});
        };
        add(401, CanvasKind::Panel, "", {x, y, 520, 430}, PANEL);
        add(402, CanvasKind::Text, "Настройки OpenRouter", {x + 24, y + 18, 470, 30}, 0);
        add(410, CanvasKind::Text, "Model ID · любой OpenRouter model с tools", {x + 24, y + 64, 470, 22}, 0,
            MUTED);
        add(403, CanvasKind::Input, form_model, {x + 24, y + 94, 470, 42}, BG);
        add(411, CanvasKind::Text,
            config.api_key.empty() ? "API key · не настроен" : "API key настроен · введите новый для замены",
            {x + 24, y + 151, 470, 22}, 0, MUTED);
        add(404, CanvasKind::Input, form_key, {x + 24, y + 181, 470, 42}, BG);
        add(405, CanvasKind::Button, readonly ? "Профиль: только чтение" : "Профиль: работа с workspace",
            {x + 24, y + 244, 470, 36}, 0x28233fff);
        add(412, CanvasKind::Text, "Ключ хранится через Windows DPAPI. Файлы — через next-agent.",
            {x + 24, y + 292, 470, 40}, 0, MUTED);
        add(406, CanvasKind::Button, "Сохранить", {x + 24, y + 357, 220, 42}, 0x554485ff);
        add(407, CanvasKind::Button, "Закрыть", {x + 274, y + 357, 220, 42}, BG);
        dialog.Reset(std::move(d));
        dialog.SetOnAction([this](uint64_t n) { pending = n; });
    }
    void process() {
        if (!pending)
            return;
        auto action = std::exchange(pending, 0);
        try {
            if (action == 2)
                send();
            else if (action >= 405 && action <= 407) {
                form_model = input(dialog, 403);
                form_key = input(dialog, 404);
                if (action == 405) {
                    readonly = !readonly;
                    build_dialog();
                }
                if (action == 406)
                    save_settings();
                if (action == 407)
                    settings = false;
                dirty = true;
            } else if (auto item = actions.find(action); item != actions.end())
                item->second();
        } catch (const std::exception &e) {
            error = e.what();
            dirty = true;
        }
    }
    void resources() {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - sample).count();
        if (dt < 1)
            return;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            auto ticks = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            auto total = ticks(kernel) + ticks(user);
            if (previous_cpu)
                cpu = (total - previous_cpu) / 1e7 / dt * 100. /
                      (std::max)(1u, std::thread::hardware_concurrency());
            previous_cpu = total;
        }
        PROCESS_MEMORY_COUNTERS data{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &data, sizeof(data)))
            memory = data.WorkingSetSize / 1048576.;
        sample = now;
        dirty = true;
    }
    void build() {
        CanvasDocument d;
        d.width = width;
        d.height = height;
        d.background = BG;
        actions.clear();
        id = 20;
        float cw = center(), rx = width - right;
        auto v = view();
        auto node = [&](CanvasKind kind, std::string text, Rect bounds, uint32_t bg = 0, uint32_t fg = TEXT,
                        uint32_t border = 0, float radius = 7) {
            uint64_t n = ++id;
            d.elements.push_back({n, kind, "ui", std::move(text), bounds, bg, fg, border, radius});
            return n;
        };
        auto text = [&](std::string value, float x, float y, float w, float h = 22, uint32_t color = TEXT) {
            return node(CanvasKind::Text, std::move(value), {x, y, w, h}, 0, color);
        };
        auto panel = [&](float x, float y, float w, float h, uint32_t bg = PANEL) {
            node(CanvasKind::Panel, "", {x, y, w, h}, bg, TEXT, LINE, 9);
        };
        auto button = [&](std::string label, Rect r, std::function<void()> fn, uint32_t bg = PANEL,
                          uint32_t color = TEXT) {
            auto n = node(CanvasKind::Button, std::move(label), r, bg, color, LINE);
            actions[n] = std::move(fn);
        };
        panel(0, 0, width, 52, 0x13141aff);
        node(CanvasKind::Avatar, "N", {18, 11, 29, 29}, 0x433561ff, VIOLET);
        text("NextAgent Harness", 60, 15, 225);
        text("● NextLangServer · 30 модулей", (std::max)(320.f, width - 750), 15, 310, 22, GREEN);
        button(smoke      ? "Offline self-test"
               : readonly ? "Только чтение"
                          : "Workspace",
               {width - 350, 10, 190, 32}, [this] { open_settings(); });
        button("—", {width - 147, 10, 38, 32}, [this] { ShowWindow(window, SW_MINIMIZE); });
        button("□", {width - 101, 10, 38, 32},
               [this] { ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE); });
        button("×", {width - 55, 10, 38, 32}, [this] { PostMessageW(window, WM_CLOSE, 0, 0); });
        panel(0, 53, left, height - 53, 0x111218ff);
        button("＋  Новая сессия", {14, 66, left - 28, 40}, [this] { fresh(); });
        text("СЕССИИ", 20, 127, left - 35, 20, MUTED);
        float sy = 159;
        int begin = (std::max)(0, int(chats.size()) - 5 - session_offset);
        for (int i = begin; i < (std::min)(begin + 5, int(chats.size())); i++) {
            auto cv = chats[i].session->snapshot();
            button(
                clip(cv.title, 20), {14, sy, left - 28, 34},
                [this, i] {
                    selected = i;
                    workspace = chats[i].workspace;
                    viewer.clear();
                    refresh_files();
                    scroll = 0;
                },
                i == selected ? 0x2b2445ff : PANEL, i == selected ? VIOLET : TEXT);
            sy += 41;
        }
        float wy = (std::max)(sy + 15, 330.f);
        text("WORKSPACE", 20, wy, left - 40, 22, MUTED);
        button("⌂", {15, wy + 32, 34, 34}, [this] { refresh_files(); });
        button("↗ Открыть папку", {56, wy + 32, left - 71, 34}, [this] { choose_workspace(); });
        text(clip(workspace->root(), 25), 20, wy + 75, left - 35, 38, MUTED);
        float fy = wy + 124;
        for (size_t i = size_t(file_offset); i < files.size() && fy < height - 120; i++) {
            auto f = files[i];
            bool dir = f.at("directory").as_bool();
            auto path = string(f, "path");
            button(
                std::string(dir ? "▸  " : "   ") + clip(string(f, "name"), 23), {16, fy, left - 30, 25},
                [this, path, dir] {
                    if (dir) {
                        files = workspace->tree(path).at("entries").as_array();
                        file_offset = 0;
                        dirty = true;
                    } else {
                        auto data = workspace->invoke(
                            "view", Object{{"target", path}, {"start_line", 1}, {"end_line", 220}});
                        viewer = string(data, "text");
                        viewer_title = path;
                        dirty = true;
                        scroll = 0;
                        follow_tail = false;
                    }
                },
                BG, dir ? TEXT : MUTED);
            fy += 27;
        }
        panel(14, height - 96, left - 28, 77);
        text("Ресурсы процесса", 26, height - 86, left - 50, 20);
        text(number(cpu) + "% CPU    " + number(memory, 0) + " MB RAM", 26, height - 54, left - 50, 23,
             MUTED);
        panel(left + 1, 54, cw - 2, 46, 0x13141aff);
        text(viewer_title.empty() ? clip(v.title, 55) : clip(viewer_title, 55), left + 23, 67, cw - 190, 22);
        button(viewer_title.empty() ? "История" : "Диалог", {left + cw - 145, 61, 120, 32}, [this] {
            if (!viewer_title.empty()) {
                viewer.clear();
                viewer_title.clear();
            } else {
                viewer = workspace->invoke("history", Object{{"op", "log"}}).dump(true);
                viewer_title = "История транзакций";
            }
            dirty = true;
            scroll = 0;
        });
        float chat_top = 121, chat_bottom = height - 140, cy = chat_top - scroll;
        float tx = left + 28, tw = cw - 56;
        auto chat_text = [&](std::string value, uint32_t color = TEXT) {
            float chars = (std::max)(25.f, tw / 7.5f);
            size_t count = utf8_cp_count(value.data(), value.size());
            float rows = float(count) / chars + 1 + float(std::count(value.begin(), value.end(), '\n'));
            float h = (std::max)(25.f, rows * 19.f + 12);
            if (cy + h > chat_top && cy < chat_bottom) {
                float top = (std::max)(cy, chat_top);
                if (top == cy && top + h < chat_bottom)
                    text(value, tx, top, tw, h, color);
                else {
                    size_t start = cy < chat_top ? size_t((chat_top - cy) / 19.f * chars) : 0;
                    size_t byte = cp_to_byte(value.data(), value.size(), start);
                    if (byte == size_t(-1))
                        byte = 0;
                    text(clip(value.substr(byte), size_t((chat_bottom - top) / 19.f * chars)), tx, top, tw,
                         chat_bottom - top, color);
                }
            }
            cy += h + 12;
        };
        if (!viewer_title.empty()) {
            chat_text(viewer, MUTED);
        } else if (v.messages.as_array().empty()) {
            chat_text("Рабочее пространство готово", VIOLET);
            chat_text("Опишите задачу — NextAgent изучит код, выполнит изменения транзакциями и покажет "
                      "результат.");
            chat_text("Все обращения к исходникам идут через next-agent. Языковой модуль выбирается "
                      "автоматически по файлу.",
                      MUTED);
            chat_text("OpenRouter · " + config.model, MUTED);
            if (config.api_key.empty() && !smoke)
                chat_text("Добавьте API key и выберите модель в настройках ↗", VIOLET);
        } else {
            auto &messages = v.messages.as_array();
            size_t start = messages.size() > 12 ? messages.size() - 12 : 0;
            for (size_t i = start; i < messages.size(); i++) {
                const auto &message = messages[i];
                auto role = string(message, "role");
                auto content = string(message, "content");
                if (role == "tool") {
                    chat_text("✓  " + string(message, "name") + " · результат сохранён", MUTED);
                    continue;
                }
                if (content.empty())
                    continue;
                chat_text(role == "user" ? "Вы" : "NextAgent", role == "user" ? MUTED : VIOLET);
                chat_text(clip(content, 7000));
            }
            if (v.running && !v.answer.empty())
                chat_text(clip(v.answer, 7000));
        }
        if (!v.error.empty())
            chat_text(v.error, 0xffa0a0ff);
        if (!error.empty())
            text(clip(error, 100), left + 28, height - 145, cw - 56, 25, 0xffa0a0ff);
        text("Ctrl+Enter — отправить     Esc — остановить", left + 27, height - 27, cw - 40, 18, MUTED);
        panel(rx, 54, right, height - 54, 0x111218ff);
        for (int i = 0; i < 3; i++)
            button(
                i == 0   ? "Процессы"
                : i == 1 ? "Контекст"
                         : "Логи",
                {rx + 12 + i * (right - 24) / 3, 65, (right - 30) / 3, 33},
                [this, i] {
                    tab = i;
                    dirty = true;
                },
                i == tab ? 0x2b2445ff : PANEL);
        panel(rx + 12, 112, right - 24, 300);
        text(v.running ? "●  Выполнение задачи" : "◉  " + v.status, rx + 25, 127, right - 50, 26,
             v.running ? VIOLET : GREEN);
        if (v.running)
            button(
                "Остановить", {rx + 22, 166, right - 44, 32},
                [this] {
                    chats[selected].session->cancel();
                    dirty = true;
                },
                0x39232aff, 0xffb9c4ff);
        float ey = v.running ? 213 : 170;
        if (tab == 0 || tab == 2) {
            int first = (std::max)(0, int(v.events.size()) - 5);
            for (int i = first; i < int(v.events.size()); i++) {
                auto e = v.events[i];
                text(clip(e.title, 36), rx + 25, ey, right - 50, 22, e.error ? 0xffa0a0ff : TEXT);
                text(e.seconds > 0 ? number(e.seconds, 2) + " с" : clip(e.detail, 40), rx + 25, ey + 22,
                     right - 50, 20, MUTED);
                ey += 47;
            }
        } else {
            auto modules = workspace->invoke("language_modules");
            size_t loaded = 0;
            for (auto &m : modules.at("modules").as_array())
                loaded += m.at("loaded").as_bool();
            text("Native-модули: " + std::to_string(loaded) + " / 30 загружено", rx + 25, ey, right - 50, 32);
            text(clip(workspace->root(), 38), rx + 25, ey + 45, right - 50, 70, MUTED);
        }
        panel(rx + 12, 427, right - 24, 145);
        text("Исполнитель", rx + 25, 443, right - 50, 25);
        text("OpenRouter", rx + 25, 480, 120, 23, VIOLET);
        text(clip(config.model, 30), rx + 25, 509, right - 50, 35, MUTED);
        panel(rx + 12, 586, right - 24, 176);
        text("Метрики сессии", rx + 25, 600, right - 50, 26);
        text("Время                         " + number(v.elapsed, 2) + " с", rx + 25, 637, right - 50);
        text("Токены                       " + std::to_string(v.input_tokens + v.output_tokens), rx + 25, 668,
             right - 50);
        text("Инструменты              " + std::to_string(v.tool_calls), rx + 25, 699, right - 50);
        text("Стоимость                  " + (v.cost_reported ? "$" + number(v.cost, 5) : "—"), rx + 25, 730,
             right - 50, 22, GREEN);
        if (height > 840) {
            panel(rx + 12, 776, right - 24, height - 791);
            text("Изменено файлов: " + std::to_string(v.changed.size()), rx + 25, 788, right - 50, 25);
            if (!v.changed.empty())
                button(
                    clip(v.changed.back(), 35), {rx + 22, 822, right - 44, 28},
                    [this] {
                        viewer = workspace->invoke("history", Object{{"op", "diff"}}).dump(true);
                        viewer_title = "Изменения";
                        scroll = 0;
                        dirty = true;
                    },
                    BG, VIOLET);
        }
        max_scroll = (std::max)(0.f, cy + scroll - chat_bottom + 15);
        bool reposition = false;
        if (follow_tail && std::abs(scroll - max_scroll) > 1) {
            scroll = max_scroll;
            reposition = true;
        } else if (scroll > max_scroll) {
            scroll = max_scroll;
            reposition = true;
        }
        main.Reset(std::move(d));
        main.SetOnAction([this](uint64_t n) { pending = n; });
        dirty = reposition;
    }
};
LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window = w;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (!app)
        return DefWindowProcW(w, msg, wp, lp);
    if (msg == WM_NCHITTEST) {
        auto hit = DefWindowProcW(w, msg, wp, lp);
        if (hit != HTCLIENT)
            return hit;
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &point);
        if (point.y * 96 / app->dpi < 52 && point.x * 96 / app->dpi < app->width - 370)
            return HTCAPTION;
        return hit;
    }
    if (msg == WM_APP + 1) {
        if (app->view().changed.size() != app->observed_changes) {
            app->observed_changes = app->view().changed.size();
            app->refresh_files();
        }
        app->dirty = true;
        return 0;
    }
    if (msg == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO *>(lp);
        info->ptMinTrackSize = {1160, 760};
        return 0;
    }
    if (msg == WM_SIZE) {
        app->width = float(LOWORD(lp)) * 96 / app->dpi;
        app->height = float(HIWORD(lp)) * 96 / app->dpi;
        if (app->width > 0 && app->height > 0) {
            app->reset_composer();
            if (app->settings)
                app->build_dialog();
            app->dirty = true;
        }
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        app->dpi = float(HIWORD(wp));
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->dirty = true;
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && wp == VK_RETURN) {
            app->pending = 2;
            return 0;
        }
        if (ctrl && wp == 'N') {
            app->fresh();
            return 0;
        }
        if (ctrl && wp == VK_OEM_COMMA) {
            app->open_settings();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (app->settings)
                app->settings = false;
            else if (!app->chats.empty())
                app->chats[app->selected].session->cancel();
            app->dirty = true;
            return 0;
        }
    }
    if (msg == WM_LBUTTONDOWN) {
        float y = GET_Y_LPARAM(lp) * 96 / app->dpi;
        app->focus =
            app->settings
                ? App::Dialog
                : ((GET_X_LPARAM(lp) * 96 / app->dpi > app->left &&
                    GET_X_LPARAM(lp) * 96 / app->dpi < app->width - app->right && y > app->height - 125)
                       ? App::Composer
                       : App::Main);
    }
    if (msg == WM_MOUSEWHEEL && !app->settings) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &point);
        float x = point.x * 96 / app->dpi, y = point.y * 96 / app->dpi;
        int step = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -2 : 2;
        if (x < app->left) {
            if (y < 330)
                app->session_offset =
                    std::clamp(app->session_offset - step, 0, (std::max)(0, int(app->chats.size()) - 5));
            else
                app->file_offset =
                    std::clamp(app->file_offset + step, 0, (std::max)(0, int(app->files.size()) - 1));
        } else if (x < app->width - app->right && y > app->height - 125)
            return app->composer_input.ProcessMessage(app->composer.Host(), w, msg, wp, lp, app->dpi)
                       ? 0
                       : DefWindowProcW(w, msg, wp, lp);
        else {
            app->scroll = std::clamp(app->scroll - GET_WHEEL_DELTA_WPARAM(wp) * .4f, 0.f, app->max_scroll);
            app->follow_tail = app->scroll >= app->max_scroll - 5;
        }
        app->dirty = true;
        return 0;
    }

    if (app->focus == App::Dialog && app->settings) {
        if (app->dialog_input.ProcessMessage(app->dialog.Host(), w, msg, wp, lp, app->dpi))
            return 0;
    } else if (app->focus == App::Composer) {
        if (app->composer_input.ProcessMessage(app->composer.Host(), w, msg, wp, lp, app->dpi))
            return 0;
    } else if (app->main_input.ProcessMessage(app->main.Host(), w, msg, wp, lp, app->dpi))
        return 0;
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace
int main(int argc, char **argv) try {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    App app;
    bool warp = false;
    std::string root, screenshot;
    app.state_dir = default_state_directory();
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; i++) {
        auto arg = utf8(args[i]);
        if (arg == "--smoke")
            app.smoke = automated = true;
        else if (arg == "--warp")
            warp = true;
        else if (arg == "--root" && i + 1 < count)
            root = utf8(args[++i]);
        else if (arg == "--state-dir" && i + 1 < count)
            app.state_dir = utf8(args[++i]);
        else if (arg == "--screenshot" && i + 1 < count)
            screenshot = utf8(args[++i]);
    }
    LocalFree(args);
    if (app.smoke)
        root = app.state_dir + "/smoke-" + std::to_string(pf_unix_ms());
    app.load(root);
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    type.lpszClassName = L"NextAgentHarness";
    RegisterClassW(&type);
    HWND window =
        CreateWindowExW(0, type.lpszClassName, L"NextAgent Harness",
                        (WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
                        CW_USEDEFAULT, CW_USEDEFAULT, 1540, 960, nullptr, nullptr, type.hInstance, &app);
    if (!window)
        throw std::runtime_error("Window creation failed");
    ShowWindow(window, SW_SHOW);
    arcade_ui::examples::Graphics graphics;
    graphics.Create(window, UINT(app.width * app.dpi / 96), UINT(app.height * app.dpi / 96), warp);
    auto created = UiRenderer::Create(graphics.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);
    if (!created)
        throw std::runtime_error(created.error().DescribeUtf8());
    auto renderer = std::move(*created);
    UINT frames = 0;
    bool running = true;
    auto start = std::chrono::steady_clock::now();
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running)
            break;
        app.process();
        app.resources();
        if (app.width <= 0 || app.height <= 0) {
            WaitMessage();
            continue;
        }
        if (app.dirty)
            app.build();
        Vec2 pixels{app.width * app.dpi / 96, app.height * app.dpi / 96};
        Vec2 dip{app.width, app.height};
        app.main.Update(dip, app.dpi);
        app.composer.Update(dip, app.dpi);
        auto packet = app.main.BuildFramePacket(pixels, app.dpi);
        for (auto &c : packet.commands)
            if (c.kind == PrimitiveKind::GlyphRun && c.bounds.height > 34) {
                c.text_multiline = true;
                c.text_top_aligned = true;
            }
        auto compose = app.composer.BuildFramePacket(pixels, app.dpi, false);
        packet.commands.insert(packet.commands.end(), compose.commands.begin(), compose.commands.end());
        if (app.input(app.composer, 1).empty())
            packet.commands.push_back({.kind = PrimitiveKind::GlyphRun,
                                       .bounds = {app.left + 39, app.height - 104, app.center() - 170, 24},
                                       .fill_color = CanvasColor(MUTED),
                                       .text_str = "Введите запрос…",
                                       .text_left_aligned = true,
                                       .text_inset = 0});
        if (app.settings) {
            packet.commands.push_back({.kind = PrimitiveKind::Rect,
                                       .bounds = {0, 0, app.width, app.height},
                                       .fill_color = CanvasColor(0x000000b0)});
            app.dialog.Update(dip, app.dpi);
            auto settings = app.dialog.BuildFramePacket(pixels, app.dpi, false);
            for (auto &c : settings.commands)
                if (c.kind == PrimitiveKind::GlyphRun && c.bounds.y > app.height * .5f - 36 &&
                    c.bounds.y < app.height * .5f + 12)
                    c.text_str = std::string(app.input(app.dialog, 404).size(), '*');
            packet.commands.insert(packet.commands.end(), settings.commands.begin(), settings.commands.end());
        }
        if (app.focus == App::Composer)
            app.composer_input.UpdateImePosition(app.composer.Host(), window, app.dpi);
        else if (app.settings)
            app.dialog_input.UpdateImePosition(app.dialog.Host(), window, app.dpi);
        if (app.smoke && frames == 2) {
            auto input = app.composer.Instance(1);
            app.composer.Host().Focus(*input, SlotRef{1});
            for (char32_t c : U"Проверь нативный цикл Harness")
                app.composer.Host().DispatchText(c);
            app.pending = 2;
        }
        graphics.Resize(UINT(pixels.x), UINT(pixels.y));
        graphics.Wait();
        arcade_ui::examples::Check(graphics.allocator->Reset());
        arcade_ui::examples::Check(graphics.list->Reset(graphics.allocator.Get(), nullptr));
        UINT index = graphics.swap->GetCurrentBackBufferIndex();
        auto barrier = graphics.Barrier(graphics.buffers[index].Get(), D3D12_RESOURCE_STATE_PRESENT,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
        graphics.list->ResourceBarrier(1, &barrier);
        auto rtv = graphics.heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += index * graphics.increment;
        const float clear[] = {.06f, .065f, .085f, 1};
        graphics.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        graphics.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT viewport{0, 0, pixels.x, pixels.y, 0, 1};
        D3D12_RECT scissor{0, 0, LONG(pixels.x), LONG(pixels.y)};
        graphics.list->RSSetViewports(1, &viewport);
        graphics.list->RSSetScissorRects(1, &scissor);
        auto rendered = renderer.Render(graphics.list.Get(), 0, packet);
        if (!rendered)
            throw std::runtime_error(rendered.error().DescribeUtf8());
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        graphics.list->ResourceBarrier(1, &barrier);
        arcade_ui::examples::Check(graphics.list->Close());
        ID3D12CommandList *lists[] = {graphics.list.Get()};
        graphics.queue->ExecuteCommandLists(1, lists);
        arcade_ui::examples::Check(graphics.swap->Present(1, 0));
        frames++;
        if (app.smoke && frames > 40 && !app.view().running) {
            if (app.view().tool_calls != 1 || !app.view().error.empty())
                throw std::runtime_error("Harness GUI self-test failed: " + app.view().error);
            if (!screenshot.empty())
                graphics.Screenshot(index, screenshot);
            running = false;
        }
        if (app.smoke && std::chrono::steady_clock::now() - start > std::chrono::seconds(40))
            throw std::runtime_error("Harness GUI smoke timeout");
    }
    graphics.Wait();
    for (auto &chat : app.chats)
        chat.session->cancel();
    for (auto &chat : app.chats)
        chat.session->wait();
    DestroyWindow(window);
    return 0;
} catch (const std::exception &e) {
    if (automated)
        fprintf(stderr, "%s\n", e.what());
    else
        MessageBoxA(nullptr, e.what(), "NextAgent Harness", MB_ICONERROR);
    return 1;
}
