#include <imgui.h>

#include "aether/editor/editor_app.hpp"
#include "generated/roboto_medium.inl"

namespace aether::editor {

void apply_editor_theme(float dpi_scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding = 2.0f;
    s.ChildRounding = 2.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 3.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = {8, 8};
    s.FramePadding = {6, 4};
    s.ItemSpacing = {6, 5};
    s.IndentSpacing = 14;
    s.ScrollbarSize = 12;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DockingSeparatorSize = 3.0f;

    // Neutral dark greys with a blue accent, in the spirit of modern
    // engine editors.
    ImVec4* c = s.Colors;
    const ImVec4 bg{0.082f, 0.082f, 0.086f, 1}, panel{0.125f, 0.125f, 0.13f, 1}, frame{0.059f, 0.059f, 0.063f, 1};
    const ImVec4 header{0.19f, 0.19f, 0.2f, 1}, hover{0.25f, 0.25f, 0.27f, 1}, accent{0.0f, 0.44f, 0.88f, 1};
    const ImVec4 accent_soft{0.0f, 0.44f, 0.88f, 0.55f}, text{0.86f, 0.86f, 0.88f, 1};
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = {0.5f, 0.5f, 0.53f, 1};
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = {0.1f, 0.1f, 0.105f, 0.98f};
    c[ImGuiCol_Border] = {0.03f, 0.03f, 0.035f, 1};
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = {0.1f, 0.1f, 0.11f, 1};
    c[ImGuiCol_FrameBgActive] = {0.12f, 0.12f, 0.13f, 1};
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = panel;
    c[ImGuiCol_ScrollbarGrab] = header;
    c[ImGuiCol_ScrollbarGrabHovered] = hover;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = header;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = header;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = accent_soft;
    c[ImGuiCol_Separator] = {0.03f, 0.03f, 0.035f, 1};
    c[ImGuiCol_SeparatorHovered] = accent_soft;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered] = accent_soft;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = {0.3f, 0.3f, 0.32f, 1};
    c[ImGuiCol_DockingPreview] = accent_soft;
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = header;
    c[ImGuiCol_TableBorderLight] = {0.16f, 0.16f, 0.17f, 1};
    c[ImGuiCol_TableBorderStrong] = {0.03f, 0.03f, 0.035f, 1};
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, 0.025f};
    c[ImGuiCol_TextSelectedBg] = accent_soft;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_PlotLines] = {0.4f, 0.8f, 0.4f, 1};
    c[ImGuiCol_PlotHistogram] = accent;
    s.ScaleAllSizes(dpi_scale);
}

void load_editor_font(float size_pixels) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // static data, never freed by ImGui
    cfg.OversampleH = 2;
    // ImGui takes a non-const pointer but does not modify unowned data.
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kRobotoMedium), static_cast<int>(kRobotoMedium_size),
                                   size_pixels, &cfg);
}

}  // namespace aether::editor
