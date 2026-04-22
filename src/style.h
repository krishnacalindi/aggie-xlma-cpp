#ifndef STYLE_H
#define STYLE_H

#include <imgui.h>
#include <array>

/*
themes imported from https://github.com/ocornut/imgui/issues/707#issuecomment-4107169777,
thanks to 'TheAncientOwl' for listing all the themes
*/

struct Styl
{
    // custom styles
    int vhf_size = 1;
    ImFont *font = nullptr;
    float padding, axis_size, tick_height;
    const std::array<float, 3> three_ticks = {0.2f, 0.5f, 0.8f};
    const std::array<float, 5> five_ticks = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};

    ImGuiStyle *imgui_style = nullptr;
    ImVec4 *imgui_colors = nullptr;
    int theme_index = 0;
    std::array<const char *, 13> themes = {"Dark", "Light", "Forest", "Amethyst", "Sapphire", "Amber", "Dracula", "CatppuccinMocha", "Gruvbox", "Crimson", "Rose", "Cyberpunk", "Paper"};

    void SetTheme();                   // colors
    void SetStyle(ImGuiStyle &_style); // spacing, sizing, borders, rounding, padding, etc.

private:
    void _set_dark();
    void _set_light();
    void _set_forest();
    void _set_amethyst();
    void _set_sapphire();
    void _set_amber();
    void _set_dracula();
    void _set_catppuccin_mocha();
    void _set_gruvbox();
    void _set_crimson();
    void _set_rose();
    void _set_cyberpunk();
    void _set_paper();
};

#endif
