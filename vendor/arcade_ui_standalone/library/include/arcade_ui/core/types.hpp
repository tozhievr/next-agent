// arcade_ui/core/types.hpp
//
// Core fundamental types, geometry, colors, identifiers, and unit conversions.
#pragma once

#include <cstdint>
#include <compare>
#include <cmath>
#include <string>
#include <string_view>
#include <algorithm>
#include <memory>

namespace arcade_ui {

// --- Handle with generation index (§6.2) -----------------------------------
struct Handle {
    uint32_t index = ~0u;
    uint32_t generation = 0;

    constexpr bool IsValid() const noexcept { return index != ~0u && generation != 0; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr auto operator<=>(const Handle&) const noexcept = default;
};

using InstanceId = Handle;
using LayerId = Handle;
using FocusScopeId = Handle;

// --- 2D Geometry (DIP) -----------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const noexcept { return {x / s, y / s}; }
    constexpr Vec2& operator+=(const Vec2& o) noexcept { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) noexcept { x -= o.x; y -= o.y; return *this; }
    constexpr auto operator<=>(const Vec2&) const noexcept = default;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr Rect() noexcept = default;
    constexpr Rect(float x_, float y_, float w_, float h_) noexcept
        : x(x_), y(y_), width(w_), height(h_) {}

    constexpr float Left() const noexcept { return x; }
    constexpr float Top() const noexcept { return y; }
    constexpr float Right() const noexcept { return x + width; }
    constexpr float Bottom() const noexcept { return y + height; }
    constexpr Vec2 TopLeft() const noexcept { return {x, y}; }
    constexpr Vec2 BottomRight() const noexcept { return {x + width, y + height}; }
    constexpr Vec2 Size() const noexcept { return {width, height}; }

    constexpr bool Contains(const Vec2& p) const noexcept {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }

    constexpr bool Intersects(const Rect& o) const noexcept {
        return x < o.Right() && Right() > o.x && y < o.Bottom() && Bottom() > o.y;
    }

    constexpr Rect Intersect(const Rect& o) const noexcept {
        const float nx = (std::max)(x, o.x);
        const float ny = (std::max)(y, o.y);
        const float nr = (std::min)(Right(), o.Right());
        const float nb = (std::min)(Bottom(), o.Bottom());
        if (nr < nx || nb < ny) return Rect{0, 0, 0, 0};
        return Rect{nx, ny, nr - nx, nb - ny};
    }

    constexpr auto operator<=>(const Rect&) const noexcept = default;
};

// --- Colors (Linear Premultiplied Alpha) (§13.3) ---------------------------
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() noexcept = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) noexcept
        : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color FromRgba8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) noexcept {
        const float af = static_cast<float>(a8) / 255.0f;
        // Premultiplied alpha: RGB is multiplied by alpha!
        return Color{
            (static_cast<float>(r8) / 255.0f) * af,
            (static_cast<float>(g8) / 255.0f) * af,
            (static_cast<float>(b8) / 255.0f) * af,
            af
        };
    }

    static constexpr Color White() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color Black() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color Transparent() noexcept { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    constexpr Color MultiplyAlpha(float alpha_scale) const noexcept {
        return Color{r * alpha_scale, g * alpha_scale, b * alpha_scale, a * alpha_scale};
    }

    constexpr Color Lighten(float amount) const noexcept {
        return Color{
            (std::clamp)(r + amount * a, 0.0f, a),
            (std::clamp)(g + amount * a, 0.0f, a),
            (std::clamp)(b + amount * a, 0.0f, a),
            a
        };
    }

    constexpr Color WithAlpha(float new_a) const noexcept {
        if (a <= 0.0f) return Color{0.0f, 0.0f, 0.0f, new_a};
        const float ratio = new_a / a;
        return Color{r * ratio, g * ratio, b * ratio, new_a};
    }
};

// --- Spacing & Radii -------------------------------------------------------
struct Padding {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Padding() noexcept = default;
    constexpr Padding(float all) noexcept : left(all), top(all), right(all), bottom(all) {}
    constexpr Padding(float h, float v) noexcept : left(h), top(v), right(h), bottom(v) {}
    constexpr Padding(float l, float t, float r, float b) noexcept
        : left(l), top(t), right(r), bottom(b) {}

    constexpr float Horizontal() const noexcept { return left + right; }
    constexpr float Vertical() const noexcept { return top + bottom; }
};

struct CornerRadii {
    float top_left = 0.0f;
    float top_right = 0.0f;
    float bottom_right = 0.0f;
    float bottom_left = 0.0f;

    constexpr CornerRadii() noexcept = default;
    constexpr CornerRadii(float all) noexcept
        : top_left(all), top_right(all), bottom_right(all), bottom_left(all) {}
    constexpr CornerRadii(float tl, float tr, float br, float bl) noexcept
        : top_left(tl), top_right(tr), bottom_right(br), bottom_left(bl) {}
};

// --- Layer Bands & Input Policy (§7.1, §7.3) -------------------------------
enum class LayerBand : uint8_t {
    Background = 0,
    Content = 1,
    Overlay = 2,
    Modal = 3,
    Tooltip = 4,
    Debug = 5
};

enum class InputPolicy : uint8_t {
    PassThrough = 0,
    Interactive = 1,
    Blocking = 2,
    Modal = 3
};

// --- Dirty Mask Flags (§9.1) -----------------------------------------------
enum class DirtyMask : uint32_t {
    None        = 0,
    Bindings    = 1 << 0,
    Text        = 1 << 1,
    Measure     = 1 << 2,
    Arrange     = 1 << 3,
    Paint       = 1 << 4,
    HitTest     = 1 << 5,
    Order       = 1 << 6,
    Semantics   = 1 << 7,
    All         = 0xFFFFFFFFu
};

constexpr DirtyMask operator|(DirtyMask a, DirtyMask b) noexcept {
    return static_cast<DirtyMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr DirtyMask operator&(DirtyMask a, DirtyMask b) noexcept {
    return static_cast<DirtyMask>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr DirtyMask& operator|=(DirtyMask& a, DirtyMask b) noexcept {
    a = a | b;
    return a;
}
constexpr bool HasFlag(DirtyMask mask, DirtyMask flag) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0;
}

// --- Layout Alignment & Sizing Modes ---------------------------------------
enum class Alignment : uint8_t {
    Start = 0,
    Center = 1,
    End = 2,
    Stretch = 3
};

enum class SizeKind : uint8_t {
    Fixed = 0,
    Intrinsic = 1,
    Fill = 2,
    Percent = 3
};

struct SizeSpec {
    SizeKind kind = SizeKind::Intrinsic;
    float value = 0.0f; // px if Fixed, fraction/weight if Fill (1.0 = default), ratio if Percent (0.0..1.0)

    static constexpr SizeSpec Fixed(float px) noexcept { return {SizeKind::Fixed, px}; }
    static constexpr SizeSpec Intrinsic() noexcept { return {SizeKind::Intrinsic, 0.0f}; }
    static constexpr SizeSpec Fill(float weight = 1.0f) noexcept { return {SizeKind::Fill, weight}; }
    static constexpr SizeSpec Percent(float p) noexcept { return {SizeKind::Percent, p}; }
};

struct BoxConstraints {
    float min_width = 0.0f;
    float max_width = 1e9f;
    float min_height = 0.0f;
    float max_height = 1e9f;

    constexpr BoxConstraints() noexcept = default;
    constexpr BoxConstraints(float min_w, float max_w, float min_h, float max_h) noexcept
        : min_width(min_w), max_width(max_w), min_height(min_h), max_height(max_h) {}

    constexpr BoxConstraints Tight(Vec2 size) noexcept {
        return {size.x, size.x, size.y, size.y};
    }
    constexpr BoxConstraints Loose(Vec2 max_size) noexcept {
        return {0.0f, max_size.x, 0.0f, max_size.y};
    }

    constexpr Vec2 Constrain(Vec2 s) const noexcept {
        return {
            (std::clamp)(s.x, min_width, max_width),
            (std::clamp)(s.y, min_height, max_height)
        };
    }
};

// --- Unit conversions (DIP <-> Physical Pixels) (§8.1) ---------------------
constexpr float dip(float d) noexcept { return d; }

constexpr float DipToPx(float dip_val, float dpi) noexcept {
    return dip_val * (dpi / 96.0f);
}

constexpr float PxToDip(float px_val, float dpi) noexcept {
    return px_val * (96.0f / dpi);
}

constexpr Vec2 DipToPx(Vec2 dip_vec, float dpi) noexcept {
    return {DipToPx(dip_vec.x, dpi), DipToPx(dip_vec.y, dpi)};
}

constexpr Vec2 PxToDip(Vec2 px_vec, float dpi) noexcept {
    return {PxToDip(px_vec.x, dpi), PxToDip(px_vec.y, dpi)};
}

constexpr Rect DipToPx(const Rect& r, float dpi) noexcept {
    return {DipToPx(r.x, dpi), DipToPx(r.y, dpi), DipToPx(r.width, dpi), DipToPx(r.height, dpi)};
}

constexpr Rect PxToDip(const Rect& r, float dpi) noexcept {
    return {PxToDip(r.x, dpi), PxToDip(r.y, dpi), PxToDip(r.width, dpi), PxToDip(r.height, dpi)};
}

// --- Text Handle (§4.2) ---------------------------------------------------
class Text {
public:
    Text() = default;
    explicit Text(std::string str) : data_(std::make_shared<std::string>(std::move(str))) {}
    explicit Text(std::string_view sv) : data_(std::make_shared<std::string>(sv)) {}
    explicit Text(const char* cstr) : data_(std::make_shared<std::string>(cstr ? cstr : "")) {}

    static Text copy(std::string_view sv) { return Text(sv); }

    [[nodiscard]] std::string_view view() const noexcept {
        return data_ ? std::string_view(*data_) : std::string_view{};
    }
    [[nodiscard]] bool empty() const noexcept { return !data_ || data_->empty(); }
    [[nodiscard]] size_t size() const noexcept { return data_ ? data_->size() : 0; }

    bool operator==(const Text& o) const noexcept {
        if (data_ == o.data_) return true;
        return view() == o.view();
    }

private:
    std::shared_ptr<const std::string> data_;
};

} // namespace arcade_ui
