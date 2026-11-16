#include <arcade_ui/core/canvas.hpp>
#include <arcade_ui/core/dsl.hpp>
#include <arcade_ui/core/components.hpp>
#include <stdexcept>
#include <cmath>
namespace arcade_ui {
Color CanvasColor(uint32_t c) { return Color::FromRgba8(static_cast<uint8_t>(c>>24),static_cast<uint8_t>(c>>16),static_cast<uint8_t>(c>>8),static_cast<uint8_t>(c)); }
namespace {
struct Primitive {
    struct Props { CanvasElement element; std::function<void(uint64_t)> action; };
    struct State {
        bool initialized=false,checked=false; float value=50;
        template<class Component> void Activate(EventContext<Component>& ctx) {
            const float ratio=ctx.PointerRatio();
            ctx.update([ratio](State& s,const Props& p) {
                s.checked=!s.checked;
                if (p.element.kind==CanvasKind::Slider) s.value=ratio*100;
                if (p.action) p.action(p.element.id);
            });
        }
    };
    static void Bind(UiStorage& storage,InstanceId id,State& state,const Props& props) {
        if (!state.initialized) { state.checked=props.element.checked; state.value=props.element.value; state.initialized=true; }
        const auto kind=props.element.kind;
        auto& style=storage.GetStyle(id,SlotRef{1});
        if (kind==CanvasKind::Toggle || kind==CanvasKind::Switch) style.fill_color=state.checked ? theme::Blue600 : CanvasColor(props.element.background);
        if (kind==CanvasKind::Switch) {
            const auto track=storage.GetRect(id,SlotRef{1}); auto& thumb=storage.GetRect(id,SlotRef{2});
            thumb.x=track.x+(state.checked ? track.width-22 : 4);
        }
        if (kind==CanvasKind::Checkbox) {
            auto& indicator=storage.GetStyle(id,SlotRef{3}); indicator.fill_color=state.checked ? theme::Blue500 : Color::Transparent();
            indicator.border_color=state.checked ? theme::Blue500 : theme::Zinc500;
            storage.GetStyle(id,SlotRef{4}).fill_color=state.checked ? theme::Zinc50 : Color::Transparent();
        }
        if (kind==CanvasKind::Slider) {
            const auto track=storage.GetRect(id,SlotRef{1}); auto& fill=storage.GetRect(id,SlotRef{2});
            fill.width=track.width*std::clamp(state.value,0.f,100.f)/100.f;
        }
    }
};
template<CanvasKind K> struct Node : Primitive {
    static auto Describe() {
        if constexpr(K==CanvasKind::Text) return column(text("Text"));
        else if constexpr(K==CanvasKind::Button) return column(button("Button",on_click<&State::template Activate<Node>>()));
        else if constexpr(K==CanvasKind::Input) return column(input(""));
        else if constexpr(K==CanvasKind::Textarea) return column(textarea(""));
        else if constexpr(K==CanvasKind::Checkbox) return column(checkbox("Checkbox",false,on_click<&State::template Activate<Node>>()));
        else if constexpr(K==CanvasKind::Switch) return column(switch_toggle(false,on_click<&State::template Activate<Node>>()));
        else if constexpr(K==CanvasKind::Toggle) return column(toggle("Toggle",false,on_click<&State::template Activate<Node>>()));
        else if constexpr(K==CanvasKind::Slider) return column(slider(50,on_click<&State::template Activate<Node>>()));
        else if constexpr(K==CanvasKind::Progress) return column(progress_bar(50));
        else if constexpr(K==CanvasKind::Badge) return column(badge("Badge"));
        else if constexpr(K==CanvasKind::Alert) return column(alert("Alert"));
        else if constexpr(K==CanvasKind::Avatar) return column(avatar("UI"));
        else if constexpr(K==CanvasKind::Separator) return column(separator());
        else return column(surface("",240,80));
    }
};
template<CanvasKind K> ComponentProgram Program() { return CompileComponent<Node<K>>(); }
ComponentProgram MakePrimitive(CanvasKind kind) {
    switch(kind) {
#define ARCADE_CANVAS_CASE(K) case CanvasKind::K: return Program<CanvasKind::K>();
    ARCADE_CANVAS_CASE(Panel) ARCADE_CANVAS_CASE(Text) ARCADE_CANVAS_CASE(Button)
    ARCADE_CANVAS_CASE(Input) ARCADE_CANVAS_CASE(Textarea) ARCADE_CANVAS_CASE(Checkbox)
    ARCADE_CANVAS_CASE(Switch) ARCADE_CANVAS_CASE(Toggle) ARCADE_CANVAS_CASE(Slider)
    ARCADE_CANVAS_CASE(Progress) ARCADE_CANVAS_CASE(Badge) ARCADE_CANVAS_CASE(Alert)
    ARCADE_CANVAS_CASE(Avatar) ARCADE_CANVAS_CASE(Separator)
#undef ARCADE_CANVAS_CASE
    default: throw std::invalid_argument("Invalid primitive canvas kind");
    }
}
void Customize(ComponentProgram& program,const CanvasElement& e) {
    if (!program.initial_texts.empty()) program.initial_texts[0]=e.text;
    for (auto& paint:program.paint_descriptors) {
        auto& style=program.initial_styles[paint.style_slot.local];
        if (paint.geometry_slot==SlotRef{1} && paint.kind!=PrimitiveKind::GlyphRun) {
            style.fill_color=CanvasColor(e.background); style.border_color=CanvasColor(e.border);
            style.border_width=(e.border&255) ? 1.f : 0.f; style.radii=CornerRadii{e.radius};
        }
        if (paint.kind==PrimitiveKind::GlyphRun && paint.text_index==0) style.fill_color=CanvasColor(e.foreground);
        if (e.kind==CanvasKind::Text) { paint.text_left_aligned=true; paint.text_inset=0; }
    }
    for (auto& decoration:program.decorations) if (decoration.kind==ComponentProgram::DecorationKind::ProgressFill) decoration.value=e.value;
    // Set only the primary element's leaf size; root keeps intrinsic layout.
    for (auto& op:program.layout_ops) if (op.opcode==LayoutOpcode::MeasureFixed && op.outputs.first==1) {
        auto& desc=program.layout_descriptors[op.descriptor_index]; desc.width_spec=SizeSpec::Fixed(e.bounds.width); desc.height_spec=SizeSpec::Fixed(e.bounds.height);
    }
}
}
struct UiCanvas::Impl {
    CanvasDocument document;
    std::unique_ptr<UiHost> host=std::make_unique<UiHost>();
    struct Entry { CanvasElement element; InstanceId instance; Vec2 natural{}; };
    std::vector<Entry> entries;
    std::function<void(uint64_t)> action;
    Vec2 origin{};
    uint64_t elevated=0;
};
UiCanvas::UiCanvas(CanvasDocument document) : impl_(std::make_unique<Impl>()) { Reset(std::move(document)); }
UiCanvas::~UiCanvas() = default;
UiCanvas::UiCanvas(UiCanvas&&) noexcept = default;
UiCanvas& UiCanvas::operator=(UiCanvas&&) noexcept = default;
void UiCanvas::Reset(CanvasDocument document) {
    if(!std::isfinite(document.width) || !std::isfinite(document.height) || document.width<=0 || document.height<=0) throw std::invalid_argument("Invalid canvas dimensions");
    if (document.elements.size()>256) throw std::invalid_argument("Canvas capacity is 256 elements");
    auto replacement=std::make_unique<Impl>(); replacement->action=impl_->action; replacement->document=std::move(document);
    auto& host=*replacement->host;
    for (const auto& e:replacement->document.elements) {
        if (!e.id || !std::isfinite(e.bounds.x) || !std::isfinite(e.bounds.y) || !std::isfinite(e.bounds.width) || !std::isfinite(e.bounds.height) || e.bounds.width<=0 || e.bounds.height<=0)
            throw std::invalid_argument("Invalid canvas element bounds/identity");
        for (const auto& entry:replacement->entries) if (entry.element.id==e.id) throw std::invalid_argument("Duplicate canvas element identity");
        InstanceId instance;
        auto action=[impl=replacement.get(),id=e.id](auto){if(impl->action)impl->action(id);};
        const auto mount=[&]<class T>(typename T::Props props) {
            auto program=CompileComponent<T>();
            for (const auto& paint:program.paint_descriptors) {
                auto& style=program.initial_styles[paint.style_slot.local];
                if (paint.geometry_slot==SlotRef{0} && paint.kind!=PrimitiveKind::GlyphRun) {
                    style.fill_color=CanvasColor(e.background); style.border_color=CanvasColor(e.border);
                    style.border_width=(e.border&255) ? 1.f : 0.f; style.radii=CornerRadii{e.radius};
                }
                if (paint.kind==PrimitiveKind::GlyphRun) style.fill_color=CanvasColor(e.foreground);
            }
            return host.Mount<T>(host.RegisterProgram(std::move(program)),std::move(props));
        };
        using namespace components;
        switch(e.kind) {
        case CanvasKind::Combobox: instance=mount.operator()<Combobox>({e.items,e.text,true,action}); break;
        case CanvasKind::RadioGroup: {
            RadioGroup::Props p; for(const auto& item:e.items) p.items.push_back({item}); p.initial_selection=static_cast<int>(e.value);p.on_change=action; instance=mount.operator()<RadioGroup>(p); break;
        }
        case CanvasKind::ToggleGroup: instance=mount.operator()<ToggleGroup>({e.items,true,static_cast<uint32_t>(e.value),action}); break;
        case CanvasKind::Accordion: {
            Accordion::Props p; for(const auto& item:e.items) p.items.push_back({item,e.text}); instance=mount.operator()<Accordion>(p); break;
        }
        case CanvasKind::DatePicker: {Calendar::Props p;p.on_change=action;instance=mount.operator()<DatePicker>({p,e.text});break;}
        case CanvasKind::Calendar: {Calendar::Props p;p.on_change=action;instance=mount.operator()<Calendar>(p);break;}
        case CanvasKind::Pagination: instance=mount.operator()<Pagination>({(std::max)(1,static_cast<int>(e.value)),action}); break;
        case CanvasKind::Breadcrumb: instance=mount.operator()<Breadcrumb>({e.items,action}); break;
        default: {
            auto program=MakePrimitive(e.kind); Customize(program,e);
            auto callback=[impl=replacement.get()](uint64_t id){ if(impl->action) impl->action(id); };
            instance=host.Mount<Primitive>(host.RegisterProgram(std::move(program)),{e,callback}); break;
        }
        }
        replacement->entries.push_back({e,instance});
    }
    impl_=std::move(replacement);
}
UiHost& UiCanvas::Host() { return *impl_->host; }
void UiCanvas::Update(Vec2 viewport,float dpi,Vec2 origin) {
    impl_->origin=origin;
    auto& host=*impl_->host; host.Update(viewport,dpi);
    uint64_t popup=0;
    for (auto& entry:impl_->entries) {
        const auto* record=host.Storage().GetInstance(entry.instance);
        const auto root=host.Storage().GetRect(entry.instance,SlotRef{0});
        const auto& target=entry.element.bounds;
        if(entry.natural.x<=0)entry.natural={(std::max)(1.f,root.width),(std::max)(1.f,root.height)};
        // Expanded popups keep the scale of their collapsed component.
        const float sx=target.width/entry.natural.x, sy=target.height/entry.natural.y;
        for (uint32_t i=0;i<record->geometry_count;++i) {
            auto& rect=host.Storage().GetRect(entry.instance,SlotRef{i});
            rect={origin.x+target.x+(rect.x-root.x)*sx,origin.y+target.y+(rect.y-root.y)*sy,rect.width*sx,rect.height*sy};
        }
        if(entry.element.kind==CanvasKind::Combobox && static_cast<components::Combobox::State*>(record->state.get())->expanded)popup=entry.element.id;
        if(entry.element.kind==CanvasKind::DatePicker && static_cast<components::DatePicker::State*>(record->state.get())->expanded)popup=entry.element.id;
    }
    if(popup!=impl_->elevated) {
        for(const auto& entry:impl_->entries)host.Layers().RaiseLayer(host.Storage().GetInstance(entry.instance)->layer);
        if(popup)for(const auto& entry:impl_->entries)if(entry.element.id==popup)host.Layers().RaiseLayer(host.Storage().GetInstance(entry.instance)->layer);
        impl_->elevated=popup;
    }
}
FramePacket UiCanvas::BuildFramePacket(Vec2 pixels,float dpi,bool background) {
    auto packet=impl_->host->BuildFramePacket(pixels,dpi);
    if(background)packet.commands.insert(packet.commands.begin(),DrawCommand{.kind=PrimitiveKind::Rect,.bounds={impl_->origin.x,impl_->origin.y,impl_->document.width,impl_->document.height},.fill_color=CanvasColor(impl_->document.background)});
    return packet;
}
std::optional<InstanceId> UiCanvas::Instance(uint64_t id) const { for(const auto& entry:impl_->entries) if(entry.element.id==id) return entry.instance; return {}; }
bool UiCanvas::SetBounds(uint64_t id,Rect bounds) { if(!std::isfinite(bounds.x)||!std::isfinite(bounds.y)||!std::isfinite(bounds.width)||!std::isfinite(bounds.height)||bounds.width<=0||bounds.height<=0)return false;for(auto& entry:impl_->entries) if(entry.element.id==id) { entry.element.bounds=bounds; return true; } return false; }
void UiCanvas::SetOnAction(std::function<void(uint64_t)> callback) { impl_->action=std::move(callback); }
}
