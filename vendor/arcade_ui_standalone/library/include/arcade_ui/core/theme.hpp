// arcade_ui/core/theme.hpp
//
// ShadCN design system palette and semantic theme tokens for Arcade UI.
#pragma once

#include "types.hpp"

namespace arcade_ui::theme {

// --- Zinc Palette (Dark Mode Base) -----------------------------------------
inline constexpr Color Zinc950 = Color::FromRgba8(9, 9, 11);
inline constexpr Color Zinc900 = Color::FromRgba8(24, 24, 27);
inline constexpr Color Zinc800 = Color::FromRgba8(39, 39, 42);
inline constexpr Color Zinc700 = Color::FromRgba8(63, 63, 70);
inline constexpr Color Zinc600 = Color::FromRgba8(82, 82, 91);
inline constexpr Color Zinc500 = Color::FromRgba8(113, 113, 122);
inline constexpr Color Zinc400 = Color::FromRgba8(161, 161, 170);
inline constexpr Color Zinc300 = Color::FromRgba8(212, 212, 216);
inline constexpr Color Zinc200 = Color::FromRgba8(228, 228, 231);
inline constexpr Color Zinc100 = Color::FromRgba8(244, 244, 245);
inline constexpr Color Zinc50  = Color::FromRgba8(250, 250, 250);

// --- Semantic Accents ------------------------------------------------------
inline constexpr Color Blue500    = Color::FromRgba8(59, 130, 246);
inline constexpr Color Blue600    = Color::FromRgba8(37, 99, 235);
inline constexpr Color Emerald500 = Color::FromRgba8(16, 185, 129);
inline constexpr Color Emerald600 = Color::FromRgba8(5, 150, 105);
inline constexpr Color Red500     = Color::FromRgba8(239, 68, 68);
inline constexpr Color Red600     = Color::FromRgba8(220, 38, 38);

// --- Semantic Theme Tokens -------------------------------------------------
struct Theme {
    // Backgrounds
    Color background = Zinc950;
    Color card = Color::FromRgba8(24, 24, 27, 235); // 92% translucent dark glass
    Color card_border = Zinc800;

    // Text & Foreground
    Color foreground = Zinc50;
    Color muted_foreground = Zinc400;

    // Primary Button / Accent
    Color primary = Zinc50;
    Color primary_foreground = Zinc900;

    // Secondary Button / Surface
    Color secondary = Zinc800;
    Color secondary_foreground = Zinc100;
    Color secondary_hover = Color::FromRgba8(50, 50, 56);

    // Accent Colors
    Color accent = Blue500;
    Color accent_foreground = Zinc50;
    Color success = Emerald500;
    Color destructive = Red500;

    // Borders & Separators
    Color border = Zinc800;
    Color separator = Zinc800;

    // Controls
    Color switch_track_off = Zinc800;
    Color switch_track_on = Blue500;
    Color switch_thumb = Zinc50;

    Color progress_track = Zinc800;
    Color progress_fill = Blue500;

    // Radii
    CornerRadii radius_card{12.0f};
    CornerRadii radius_btn{6.0f};
    CornerRadii radius_badge{9999.0f};
    CornerRadii radius_pill{9999.0f};
};

inline constexpr Theme DefaultDarkTheme{};

} // namespace arcade_ui::theme
