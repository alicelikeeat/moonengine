// Aether Editor: level editing UI built on Dear ImGui.
//
// The application owns the level (a World), the selection, undo history and
// the viewport camera, and draws every panel. The host program owns the
// window, the ImGui context and the renderer, and calls, once per frame:
//
//   app.update(dt);          // simulation (Play mode) and transforms
//   app.draw(dt);            // all panels; between ImGui::NewFrame/Render
//   renderer.render(app.world(), app.camera(), app.render_options());
//
// This header has no ImGui types so tools and tests can include it freely.
#pragma once

#include <memory>
#include <string>

#include "aether/foundation/log.hpp"
#include "aether/render/renderer.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

class EditorApp {
public:
    EditorApp(render::Renderer& renderer, Logger& logger, std::shared_ptr<MemorySink> log);
    ~EditorApp();

    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    void update(float dt);
    void draw(float dt);

    [[nodiscard]] world::World& world();
    [[nodiscard]] render::Camera camera() const;
    [[nodiscard]] render::RenderOptions render_options() const;
    void set_frame_stats(const render::FrameStats& stats);

    [[nodiscard]] bool quit_requested() const;

    // Level management (also reachable from the File menu).
    void new_level();
    void load_demo_level();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Applies the editor's dark theme to the current ImGui style.
void apply_editor_theme(float dpi_scale);

// Loads the embedded UI font into the current ImGui context.
void load_editor_font(float size_pixels);

}  // namespace aether::editor
