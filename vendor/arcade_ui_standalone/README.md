# Arcade UI Standalone — C++23

Самостоятельная нативная UI-библиотека для Windows с публичным API через именованные
C++23-модули. Папку `arcade_ui_standalone` можно перенести в другой репозиторий:
engine, shader_dsl, scene_manager, resources и assets для сборки не требуются.

## Модули и targets

| Import | CMake target | API |
| --- | --- | --- |
| `import arcade_ui;` | `arcade_ui::arcade_ui` | UiHost, storage, DSL, layout/layers, text editor, themes, compound components |
| `import arcade_ui.dx12;` | `arcade_ui::dx12` | UiRenderer, GpuUiInstance, Error/Result; реэкспорт core |
| `import arcade_ui.win32;` | `arcade_ui::win32` | Win32MessageAdapter, DirectWriteTextEngine и text metrics; реэкспорт core |

Namespace остаётся `arcade_ui`, компоненты — `arcade_ui::components`, тема —
`arcade_ui::theme`. Core не требует DX12 device; текст и clipboard используют Windows
и DirectWrite. DX12 backend дополнительно использует Direct2D/WIC/D3DCompiler.
Это Windows-библиотека, пока без Linux/macOS backend.

Интерфейсы модулей явно экспортируют существующие типы, функции и шаблоны.
Headers сохранены для совместимости и реализации: module/header API используют
одни и те же сущности и один ABI. Это модульный публичный фасад, не header units
и не `import std`. Стандартные headers, которые нужны вашему коду, подключайте сами;
для WinAPI/DX12 типов и macros подключайте соответствующие SDK headers.

## Сборка и интерактивное демо

Windows 10/11, Windows SDK, MSVC с C++23 и модулями, CMake 3.28+.
Проверено: MSVC 19.51, Visual Studio 18 2026 generator, CMake 4.3.2.

Из корня Arcade:

```powershell
cmake -S arcade_ui_standalone -B build/ui-standalone
cmake --build build/ui-standalone --config Release -j 4
ctest --test-dir build/ui-standalone -C Release --output-on-failure
.\build\ui-standalone\Release\arcade_ui_standalone_demo.exe
```

Если перенесли папку отдельно, используйте `cmake -S . -B build`.
Демо содержит editable input/textarea, кнопки, switch, checkbox, toggle, progress,
badge, alert, RadioGroup, Combobox, Accordion и DatePicker. Можно редактировать
Unicode-текст, пользоваться Ctrl+A/C/X/V/Z/Y, Tab и клавиатурой compound controls.
Закрытие окна завершает приложение; отдельная консоль не создаётся.
Флаги: `--warp` — программный DX12; `--smoke` — клик, ввод, resize, 30 кадров
и завершение; `--screenshot <file.bmp>` сохраняет кадр в smoke mode.

Отключение приложений: `-DARCADE_UI_BUILD_EXAMPLES=OFF -DARCADE_UI_BUILD_EDITOR=OFF -DARCADE_UI_BUILD_TESTS=OFF`.

## Визуальный редактор WYSIWYG

```powershell
.\build\ui-standalone\editor\Release\arcade_ui_editor.exe
```

Палитра из 22 компонентов, drag/resize, Inspector, слои, undo/redo, режим Test,
сохранение `.aui` и экспорт C++23 `.cppm`. Сгенерированный класс использует публичный
`UiCanvas` из `import arcade_ui;` и работает в независимом приложении через SDK.
Инструкции, пример подключения и текущие ограничения: [editor/README](editor/README.md).

## Подключение исходников

В чужом `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)
add_subdirectory(vendor/arcade_ui_standalone/library)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE arcade_ui::arcade_ui)
# Для окна и DX12: arcade_ui::win32 arcade_ui::dx12
```

Не нужно включать весь проект с его тестами/демо. `library/` использует
`../cmake/` для packaging: копируйте целиком `arcade_ui_standalone`.

## Установка SDK и find_package

```powershell
cmake --install build/ui-standalone --config Release --prefix D:/Projects/arcade/build/ui-sdk-standalone
cmake -S arcade_ui_standalone/examples/external_consumer -B build/ui-consumer -DCMAKE_PREFIX_PATH=D:/Projects/arcade/build/ui-sdk-standalone
cmake --build build/ui-consumer --config Release -j 4
ctest --test-dir build/ui-consumer -C Release --output-on-failure
```

Внешний проект:

```cmake
find_package(arcade_ui 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE arcade_ui::arcade_ui arcade_ui::dx12 arcade_ui::win32)
```

SDK содержит `.lib`, headers, `.cppm` и config/version файлы. Module interfaces
собираются в проекте-потребителе; готовые compiler-specific IFC не распространяются.
Это позволяет использовать Visual Studio generator, у которого CMake пока не
поддерживает экспорт module metadata и импортированные BMI targets.
См. [ограничения CMake modules](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html#limitations).

Сами `.lib` требуют совместимых MSVC toolset, архитектуры, CRT и конфигурации.
Для Debug соберите/установите Debug в тот же prefix; не смешивайте Release /MD
и Debug /MDd. При смене toolchain используйте подключение исходников.
Package создаёт локальные facade targets; встраивание этих targets в собственный
install/export downstream package пока не предусмотрено.

## Минимальный C++ API

```cpp
import arcade_ui;

struct Form {
    struct Props {};
    struct State {};
    static auto Describe() {
        return arcade_ui::column(
            arcade_ui::heading("My application"),
            arcade_ui::input("Editable text"),
            arcade_ui::textarea("Notes"));
    }
};

void CreateUi(arcade_ui::UiHost& host) {
    auto program = host.RegisterProgram(arcade_ui::CompileComponent<Form>());
    auto instance = host.Mount<Form>(program, {});
    host.Update({800, 600}, 96);
    auto frame = host.BuildFramePacket({800, 600}, 96);
    // Передать frame вашему renderer; Unmount(instance) удаляет форму.
}
```

Обработчики DSL — методы `State`, принимающие `EventContext<Component>&`.
Изменяйте state через `ctx.update`; `Bind` синхронизирует динамические styles/texts.
Полный пример: `examples/standalone_demo.cpp`, проверка DSL: `tests/module_test.cpp`.
Compound API: `library/include/arcade_ui/core/components.hpp`.

## Контракт DX12 и ввода

`UiRenderer::Create(device, rtv_format, frames_in_flight)` владеет device/root/PSO
и upload slots. Backend не создаёт окно, queue, swap chain или frame coordinator.
Ваша программа выставляет RTV/viewport/scissor, transitions и вызывает
`Render(command_list, frame_index, packet)`. Перед повторным использованием slot
нужно дождаться fence соответствующего кадра; перед уничтожением renderer — всех
его GPU commands. `FramePacket.viewport_size` передаётся в physical pixels, а
`UiHost::Update` — в DIP; packet содержит DPI. Смотрите рабочий frame loop в демо.
Renderer меняет graphics state; следующий pass должен установить своё состояние.

Win32MessageAdapter принимает сообщения WndProc, переводит pointer/key/UTF-16/IME
в UiHost и ставит candidate/caret position после layout через UpdateImePosition.
Используйте UiHost и backend на одном UI/render thread. Native IME candidate UI
нуждается в ручной проверке с установленным IME.

## Состояние

Разделение библиотеки сохраняет текущие ограничения UI: nested DSL/scrolling,
dynamic atlas capacity, accessibility и часть shadcn компонентов ещё требуют
развития. Лимиты renderer не сняты этой упаковкой. Галерея Arcade с Resources/GLB
сохранена в `../arcade_ui`; её engine adapters подключаются target `arcade_ui::arcade`.
Отчёт: [standalone implementation](docs/implementation_report.md).
