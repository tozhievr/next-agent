#pragma once
#include "host.hpp"
#include <functional>
namespace arcade_ui {
enum class CanvasKind : uint32_t {
    Panel, Text, Button, Input, Textarea, Checkbox, Switch, Toggle, Slider, Progress,
    Badge, Alert, Avatar, Separator, Combobox, RadioGroup, ToggleGroup, Accordion,
    DatePicker, Calendar, Pagination, Breadcrumb
};
struct CanvasElement {
    uint64_t id=0;
    CanvasKind kind=CanvasKind::Button;
    std::string name="element",text="Button";
    Rect bounds{32,32,240,40};
    uint32_t background=0x27272aff,foreground=0xf4f4f5ff,border=0x3f3f46ff;
    float radius=6,value=50;
    bool checked=false;
    std::vector<std::string> items;
    bool operator==(const CanvasElement&) const = default;
};
struct CanvasDocument {
    float width=800,height=640;
    uint32_t background=0x09090bff;
    std::vector<CanvasElement> elements; // back to front; coordinates in DIP
    bool operator==(const CanvasDocument&) const = default;
};
Color CanvasColor(uint32_t rgba);
// Shared runtime used by the WYSIWYG editor and generated C++ modules.
// Fixed canvas coordinates; Update before rendering or dispatching pointer events.
class UiCanvas {
public:
    explicit UiCanvas(CanvasDocument document);
    ~UiCanvas();
    UiCanvas(UiCanvas&&) noexcept;
    UiCanvas& operator=(UiCanvas&&) noexcept;
    UiCanvas(const UiCanvas&) = delete;
    UiCanvas& operator=(const UiCanvas&) = delete;
    void Reset(CanvasDocument document);
    UiHost& Host();
    void Update(Vec2 viewport_dip,float dpi,Vec2 origin={});
    FramePacket BuildFramePacket(Vec2 viewport_px,float dpi,bool background=true);
    std::optional<InstanceId> Instance(uint64_t element_id) const;
    // Returns false for unknown IDs or nonfinite/nonpositive sizes. Call Update afterwards.
    bool SetBounds(uint64_t element_id,Rect bounds);
    // Button/control activation and compound selection changes report stable document IDs.
    void SetOnAction(std::function<void(uint64_t)> callback);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
