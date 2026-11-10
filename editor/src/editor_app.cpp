#include "aether/editor/editor_app.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder, BeginViewportSideBar
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "aether/editor/demo_scene.hpp"
#include "aether/editor/editor_camera.hpp"
#include "aether/editor/picking.hpp"
#include "aether/editor/snapshot.hpp"
#include "aether/editor/undo.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"
#include "aether/world/schedule.hpp"
#include "aether/world/serialization.hpp"
#include "aether/gameplay/spin.hpp"
#include "aether/render/light.hpp"
#include "aether/render/mesh_registry.hpp"
#include "aether/render/model_library.hpp"
#include "aether/simulation/physics.hpp"

namespace aether::editor {

namespace h = world::hierarchy;
using math::Mat4;
using math::Quat;
using math::Vec3;
using world::Schedule;
using world::SystemAccess;
using world::SystemContext;

namespace {

constexpr const char* kViewport = "Viewport";
constexpr const char* kOutliner = "Outliner";
constexpr const char* kDetails = "Details";
constexpr const char* kPlace = "Place Actors";
constexpr const char* kLog = "Output Log";
constexpr const char* kStats = "Stats";
constexpr const char* kContent = "Content Browser";
constexpr const char* kWorldSettings = "World Settings";
constexpr const char* kAssetDrag = "AETHER_ASSET";
constexpr const char* kOutlinerDrag = "AETHER_ENTITY";
constexpr const char* kGenerator = "Aether Editor 0.7.0";

namespace fs = std::filesystem;

// UTF-8 conversions that also work on Windows, where path::string() uses
// the ANSI code page and can throw for characters outside it.
std::string to_utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
fs::path from_utf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// OpenGL-style projection for ImGuizmo (Y up in NDC), matching the
// renderer's Vulkan projection otherwise.
Mat4 perspective_gl(float fov_y, float aspect, float n, float f) {
    const float t = 1.0f / std::tan(fov_y * 0.5f);
    Mat4 m;
    for (float& v : m.m) v = 0;
    m.m[0] = t / aspect;
    m.m[5] = t;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1;
    m.m[14] = 2 * f * n / (n - f);
    return m;
}

ImVec4 level_color(LogLevel level) {
    switch (level) {
        case LogLevel::Error:
        case LogLevel::Fatal: return {1.0f, 0.42f, 0.42f, 1};
        case LogLevel::Warn: return {1.0f, 0.82f, 0.35f, 1};
        case LogLevel::Debug:
        case LogLevel::Trace: return {0.6f, 0.6f, 0.65f, 1};
        default: return {0.85f, 0.85f, 0.88f, 1};
    }
}

}  // namespace

struct EditorApp::Impl {
    render::Renderer& renderer;
    Logger& logger;
    std::shared_ptr<MemorySink> log;

    std::unique_ptr<world::World> world;
    Schedule schedule;
    std::unique_ptr<render::ModelLibrary> library;
    render::MeshId cube = 0;
    world::SerializerRegistry registry;

    // Level file state.
    fs::path level_path;               // empty: never saved
    std::uint64_t saved_revision = 0;  // undo revision at the last save/load
    std::vector<fs::path> recent;

    // Actions that may need the "unsaved changes" prompt first.
    enum class Pending { None, New, Demo, Open, Quit } pending = Pending::None;
    fs::path pending_path;
    bool unsaved_prompt = false;
    bool continue_after_save = false;

    // File browser.
    enum class DialogMode { Open, Save } dialog_mode = DialogMode::Open;
    bool dialog_requested = false;
    fs::path dialog_dir;
    char dialog_name[256] = {};
    std::string dialog_error;
    std::string error_title, error_text;
    bool error_requested = false;

    UndoStack undo;
    Uuid selected;  // nil = nothing selected
    std::optional<EntityState> edit_before;
    std::optional<EntityState> gizmo_before;

    EditorCamera cam;
    bool flying = false;

    enum class PlayState { Editing, Playing, Paused } play = PlayState::Editing;
    std::vector<EntityState> play_backup;
    // Physics exists only while playing; Stop restores the level.
    std::unique_ptr<simulation::PhysicsWorld> physics;
    float physics_accumulator = 0.0f;
    simulation::StepStats physics_stats;

    ImGuizmo::OPERATION gizmo_op = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmo_mode = ImGuizmo::WORLD;
    bool snap = false;
    float snap_translate = 0.5f, snap_rotate = 15.0f, snap_scale = 0.1f;
    bool show_grid = true;

    bool show_outliner = true, show_details = true, show_place = true, show_log = true, show_stats = true;
    bool show_content = true, show_world = true;
    // Used while the level has no Environment actor: the editor's own sky.
    render::Environment environment = [] {
        render::Environment env;
        env.sky_visible = true;
        return env;
    }();

    // Content browser.
    std::vector<std::string> content_models;
    bool content_scanned = false;
    std::string content_selected;
    int focus_content = 0;  // frames until the default tabs are brought to the front (after a layout reset)
    ImGuiTextFilter content_filter;
    bool show_demo = false, show_about = false, show_controls = false;
    bool reset_layout = false;
    bool quit = false;

    ImGuiTextFilter outliner_filter;
    bool log_info = true, log_warn = true, log_error = true, log_autoscroll = true;

    std::array<float, 240> frame_ms{};
    std::size_t frame_ms_pos = 0;
    render::FrameStats last_stats;

    // Viewport rect (screen space) and camera matrices of the last frame.
    ImVec2 vp_min{0, 0}, vp_size{1, 1};

    Impl(render::Renderer& r, Logger& l, std::shared_ptr<MemorySink> s, fs::path content_root)
        : renderer(r), logger(l), log(std::move(s)) {
        auto lib = render::ModelLibrary::create(renderer, std::move(content_root), &logger);
        if (!lib) throw std::runtime_error("cannot upload the builtin meshes: " + lib.error().message);
        library = std::move(lib).value();
        cube = library->cube();
        register_level_components(registry, library->names());
        AETHER_LOG(logger, LogLevel::Info, "editor", "content folder: {}", to_utf8(library->content_root()));
        std::error_code ec;
        dialog_dir = fs::current_path(ec);
    }

    // ---- Level --------------------------------------------------------

    void install_world(std::unique_ptr<world::World> w) {
        world = std::move(w);
        schedule = Schedule{};
        gameplay::add_systems(schedule, *world);
        // Register editor components up front so queries and snapshots see them.
        (void)world->component_id<world::Name>();
        (void)world->component_id<render::MeshInstance>();
        (void)world->component_id<world::UnknownComponents>();
        (void)world->component_id<render::Light>();
        (void)world->component_id<render::Environment>();
        (void)world->component_id<simulation::RigidBody>();
        (void)world->component_id<simulation::Collider>();
    }
    void reset_world() { install_world(std::make_unique<world::World>()); }

    void level_replaced(fs::path path) {
        undo.clear();
        saved_revision = undo.revision();
        selected = {};
        level_path = std::move(path);
        cam = EditorCamera{};
        cam.distance = 22;
    }

    void new_level() {
        stop();
        reset_world();
        level_replaced({});
        AETHER_LOG(logger, LogLevel::Info, "editor", "new level");
    }

    void load_demo() {
        stop();
        reset_world();
        build_demo_scene(*world, cube);
        level_replaced({});
        AETHER_LOG(logger, LogLevel::Info, "editor", "loaded demo level ({} entities)", world->entity_count());
    }

    // ---- Files ----------------------------------------------------------

    bool dirty() const { return undo.revision() != saved_revision; }
    std::string level_name() const { return level_path.empty() ? "Untitled" : to_utf8(level_path.filename()); }

    void show_error(std::string title, std::string text) {
        AETHER_LOG(logger, LogLevel::Error, "editor", "{}: {}", title, text);
        error_title = std::move(title);
        error_text = std::move(text);
        error_requested = true;
    }

    void remember(const fs::path& p) {
        recent.erase(std::remove(recent.begin(), recent.end(), p), recent.end());
        recent.insert(recent.begin(), p);
        if (recent.size() > 8) recent.pop_back();
    }

    bool save_to(fs::path path) {
        if (play != PlayState::Editing) {
            show_error("Cannot save", "Stop the simulation before saving; play-mode changes are not part of the level.");
            return false;
        }
        if (path.extension() != world::kLevelFileExtension) path += world::kLevelFileExtension;
        if (auto r = world::save_level_file(path, *world, registry, kGenerator); !r) {
            show_error("Save failed", r.error().message);
            return false;
        }
        level_path = path;
        saved_revision = undo.revision();
        remember(path);
        AETHER_LOG(logger, LogLevel::Info, "editor", "saved {} ({} entities)", to_utf8(path), world->entity_count());
        return true;
    }

    void open_from(const fs::path& path) {
        auto fresh = std::make_unique<world::World>();
        auto report = world::load_level_file(path, *fresh, registry);
        if (!report) {
            show_error("Could not open level", report.error().message);  // current level stays open
            return;
        }
        stop();
        install_world(std::move(fresh));
        level_replaced(path);
        remember(path);
        for (const auto& w : report.value().warnings) AETHER_LOG(logger, LogLevel::Warn, "editor", "{}", w);
        AETHER_LOG(logger, LogLevel::Info, "editor", "opened {} ({} entities, {} warnings)", to_utf8(path),
                   report.value().entities, report.value().warnings.size());
    }

    void save() {
        if (level_path.empty()) open_dialog(DialogMode::Save);
        else save_to(level_path);
    }

    void open_dialog(DialogMode mode) {
        dialog_mode = mode;
        dialog_requested = true;
        dialog_error.clear();
        const std::string name = mode == DialogMode::Save ? (level_path.empty() ? std::string("NewLevel")
                                                                                 : to_utf8(level_path.stem()))
                                                          : std::string();
        std::snprintf(dialog_name, sizeof dialog_name, "%s", name.c_str());
        if (!level_path.empty()) dialog_dir = level_path.parent_path();
    }

    // Runs `p` now, or after the unsaved-changes prompt.
    void request(Pending p, fs::path path = {}) {
        if (dirty()) {
            pending = p;
            pending_path = std::move(path);
            unsaved_prompt = true;
            return;
        }
        perform(p, path);
    }

    void perform(Pending p, const fs::path& path) {
        switch (p) {
            case Pending::None: break;
            case Pending::New: new_level(); break;
            case Pending::Demo: load_demo(); break;
            case Pending::Open:
                if (path.empty()) open_dialog(DialogMode::Open);
                else open_from(path);
                break;
            case Pending::Quit: quit = true; break;
        }
    }

    void file_popups() {
        if (unsaved_prompt) {
            ImGui::OpenPopup("Unsaved Changes");
            unsaved_prompt = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save changes to %s before continuing?", level_name().c_str());
            ImGui::TextDisabled("Unsaved changes will be lost otherwise.");
            ImGui::Spacing();
            const Pending p = pending;
            const fs::path path = pending_path;
            if (ImGui::Button("Save", ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();
                if (level_path.empty()) {
                    continue_after_save = true;
                    open_dialog(DialogMode::Save);
                } else if (save_to(level_path)) {
                    perform(p, path);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();
                perform(p, path);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pending = Pending::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const char* dialog_title = dialog_mode == DialogMode::Open ? "Open Level" : "Save Level As";
        if (dialog_requested) {
            ImGui::OpenPopup(dialog_title);
            dialog_requested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(620, 440), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(dialog_title, nullptr)) {
            file_browser();
            ImGui::EndPopup();
        }

        if (error_requested) {
            ImGui::OpenPopup("Error");
            error_requested = false;
        }
        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(error_title.c_str());
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36);
            ImGui::TextUnformatted(error_text.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void file_browser() {
        std::error_code ec;
        ImGui::TextDisabled("Folder");
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && dialog_dir.has_parent_path() && dialog_dir.parent_path() != dialog_dir)
            dialog_dir = dialog_dir.parent_path();
        ImGui::SameLine();
        ImGui::TextUnformatted(to_utf8(dialog_dir).c_str());

        std::vector<fs::path> dirs, files;
        for (fs::directory_iterator it(dialog_dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            const std::string name = to_utf8(p.filename());
            if (name.empty() || name[0] == '.') continue;
            if (it->is_directory(ec)) dirs.push_back(p);
            else if (p.extension() == world::kLevelFileExtension) files.push_back(p);
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        const float footer = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y * 2;
        bool confirm = false;
        if (ImGui::BeginChild("entries", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            if (ec) ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Cannot read this folder.");
            for (const auto& d : dirs) {
                const std::string label = "[folder]  " + to_utf8(d.filename());
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    dialog_dir = d;
            }
            for (const auto& f : files) {
                const std::string name = to_utf8(f.stem());
                const bool selected_file = name == dialog_name;
                if (ImGui::Selectable(to_utf8(f.filename()).c_str(), selected_file, ImGuiSelectableFlags_AllowDoubleClick)) {
                    std::snprintf(dialog_name, sizeof dialog_name, "%s", name.c_str());
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) confirm = true;
                }
            }
            if (dirs.empty() && files.empty() && !ec) ImGui::TextDisabled("No levels or folders here.");
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();  // type a name, Enter to confirm
        if (ImGui::InputTextWithHint("##name", "Level name", dialog_name, sizeof dialog_name,
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            confirm = true;
        if (!dialog_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "%s", dialog_error.c_str());
        const char* action = dialog_mode == DialogMode::Open ? "Open" : "Save";
        if (ImGui::Button(action, ImVec2(110, 0))) confirm = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            continue_after_save = false;
            pending = Pending::None;
            ImGui::CloseCurrentPopup();
            return;
        }
        if (!confirm) return;

        const std::string name = dialog_name;
        if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos) {
            dialog_error = "Enter a file name without / \\ : * ? \" < > |";
            return;
        }
        fs::path target = dialog_dir / from_utf8(name);
        if (target.extension() != world::kLevelFileExtension) target += world::kLevelFileExtension;
        if (dialog_mode == DialogMode::Open) {
            if (!fs::exists(target, ec)) {
                dialog_error = "No such level: " + to_utf8(target.filename());
                return;
            }
            ImGui::CloseCurrentPopup();
            open_from(target);
        } else {
            ImGui::CloseCurrentPopup();
            if (save_to(target) && continue_after_save) perform(pending, pending_path);
            continue_after_save = false;
        }
    }

    // ---- Selection & edits -------------------------------------------

    world::Entity selection() const { return selected.is_nil() ? world::kNullEntity : world->find(selected); }
    void select(world::Entity e) { selected = e.is_null() ? Uuid{} : world->id_of(e); }
    bool can_record() const { return play == PlayState::Editing; }

    void record(Command c) {
        if (!can_record()) return;
        AETHER_LOG(logger, LogLevel::Debug, "editor", "{}", c.label);
        undo.push(std::move(c));
    }

    // Undo tracking for the widget just drawn.
    void track_edit(world::Entity e, const char* label) {
        if (ImGui::IsItemActivated()) edit_before = capture(*world, e);
        if (ImGui::IsItemDeactivatedAfterEdit() && edit_before) {
            record(make_edit_command(label, *edit_before, capture(*world, e)));
            edit_before.reset();
        }
    }

    Mat4 world_matrix(world::Entity e) const {
        if (const auto* g = world->get<world::GlobalTransform>(e)) return g->matrix;
        return Mat4::identity();
    }

    // Sets `e`'s local transform so its world matrix becomes `target`.
    void set_world_matrix(world::Entity e, const Mat4& target) {
        Mat4 local = target;
        const world::Entity parent = h::parent_of(*world, e);
        if (!parent.is_null() && world->alive(parent)) {
            Mat4 inv;
            if (math::inverse(world_matrix(parent), inv)) local = inv * target;
        }
        world::Transform t;
        math::decompose(local, t.position, t.rotation, t.scale);
        world->add<world::Transform>(e, t);
    }

    world::Entity spawn(const char* name, bool mesh, bool spin, Vec3 color = {0.75f, 0.75f, 0.78f},
                        std::optional<render::MeshId> which = std::nullopt) {
        const world::Entity e = world->create();
        world->add<world::Name>(e, world::Name{name});
        world->add<world::Transform>(e, world::Transform{cam.focus});
        if (mesh) world->add<render::MeshInstance>(e, render::MeshInstance{which.value_or(cube), color});
        if (spin) world->add<Spin>(e, Spin{{0, 1, 0}, 1.0f});
        world::propagate_transforms(*world);
        record(make_create_command(std::string("Create ") + name, capture_subtree(*world, e)));
        select(e);
        return e;
    }

    // A collider that encloses the entity's mesh (a unit box without one).
    simulation::Collider fitted_collider(world::Entity e, simulation::ShapeType shape) const {
        simulation::Collider c;
        c.shape = shape;
        const auto* mesh = world->get<render::MeshInstance>(e);
        if (!mesh || mesh->mesh >= library->bounds().size()) return c;
        const render::Aabb& b = library->bounds()[mesh->mesh];
        const Vec3 half = (b.max - b.min) * 0.5f;
        c.offset = (b.max + b.min) * 0.5f;
        c.half_extents = {std::max(half.x, 0.01f), std::max(half.y, 0.01f), std::max(half.z, 0.01f)};
        c.radius = std::max({half.x, half.y, half.z, 0.01f});
        if (shape == simulation::ShapeType::Capsule) {
            c.radius = std::max({half.x, half.z, 0.01f});
            c.half_height = std::max(half.y - c.radius, 0.01f);
        }
        return c;
    }

    // A mesh that falls and collides when the level is played.
    void spawn_physics(bool sphere) {
        if (!can_record()) return;
        const world::Entity e = world->create();
        world->add<world::Name>(e, world::Name{sphere ? "Physics Sphere" : "Physics Cube"});
        world->add<world::Transform>(e, world::Transform{cam.focus + Vec3{0, 3, 0}});
        render::MeshInstance mesh{sphere ? library->sphere() : cube, sphere ? Vec3{0.9f, 0.45f, 0.2f} : Vec3{0.3f, 0.6f, 0.9f}};
        mesh.roughness = 0.4f;
        world->add<render::MeshInstance>(e, mesh);
        world->add<simulation::Collider>(e, fitted_collider(e, sphere ? simulation::ShapeType::Sphere : simulation::ShapeType::Box));
        simulation::RigidBody body;
        if (sphere) body.restitution = 0.5f;
        world->add<simulation::RigidBody>(e, body);
        world::propagate_transforms(*world);
        record(make_create_command(std::string("Create ") + (sphere ? "Physics Sphere" : "Physics Cube"),
                                   capture_subtree(*world, e)));
        select(e);
    }

    const char* actor_type(world::Entity e) const {
        if (const auto* light = world->get<render::Light>(e))
            return light->type == render::LightType::Directional ? "Directional Light" : "Point Light";
        if (world->has<render::Environment>(e)) return "Environment";
        return world->has<render::MeshInstance>(e) ? "Static Mesh" : "Empty";
    }

    // The level's Environment actor (the first one, as the renderer uses), or null.
    world::Entity environment_entity() const {
        world::Entity found;
        world->each<const render::Environment>([&](world::Entity e, const render::Environment&) {
            if (found.is_null()) found = e;
        });
        return found;
    }

    void add_environment() {
        if (!can_record()) return;
        const world::Entity e = world->create();
        world->add<world::Name>(e, world::Name{"Environment"});
        world->add<render::Environment>(e, environment);
        record(make_create_command("Add Environment", capture_subtree(*world, e)));
    }

    static render::Light default_light(render::LightType type) {
        render::Light light;
        light.type = type;
        if (type == render::LightType::Directional) {
            light.color = {1.0f, 0.96f, 0.9f};
            light.intensity = 3.0f;
        }
        return light;
    }

    void spawn_light(render::LightType type) {
        if (!can_record()) return;
        const bool sun = type == render::LightType::Directional;
        const world::Entity e = world->create();
        world->add<world::Name>(e, world::Name{sun ? "Directional Light" : "Point Light"});
        world::Transform t{cam.focus + Vec3{0, sun ? 4.0f : 1.5f, 0}};
        if (sun) t.rotation = render::rotation_toward({-0.4f, -1.0f, -0.3f});
        world->add<world::Transform>(e, t);
        world->add<render::Light>(e, default_light(type));
        world::propagate_transforms(*world);
        record(make_create_command(std::string("Create ") + (sun ? "Directional Light" : "Point Light"),
                                   capture_subtree(*world, e)));
        select(e);
    }

    // Instantiates a content model with its node hierarchy as one undo step.
    void spawn_model(const std::string& path, Vec3 position) {
        if (!can_record()) {
            show_error("Cannot add model", "Stop the simulation before editing the level.");
            return;
        }
        auto root = library->spawn(*world, path, position);
        if (!root) {
            show_error("Cannot add model", root.error().message);
            return;
        }
        world::propagate_transforms(*world);
        record(make_create_command("Add " + display_name(*world, root.value()), capture_subtree(*world, root.value())));
        select(root.value());
    }

    // Where a ray from the viewport hits the ground plane (y = 0); the view
    // focus when it points at the sky.
    Vec3 ground_point(float nx, float ny) const {
        const render::Camera rc = cam.to_render_camera();
        const Ray ray = screen_ray(render::view_projection(rc, vp_size.x / vp_size.y), nx, ny);
        if (std::abs(ray.direction.y) > 1e-6f) {
            const float t = -ray.origin.y / ray.direction.y;
            if (t > 0 && t < 1e4f) return ray.origin + ray.direction * t;
        }
        return cam.focus;
    }

    void delete_selected() {
        const world::Entity e = selection();
        if (e.is_null()) return;
        auto states = capture_subtree(*world, e);
        const std::string label = "Delete " + display_name(*world, e);
        h::destroy_recursive(*world, e);
        record(make_delete_command(label, std::move(states)));
        selected = {};
    }

    void duplicate_selected() {
        const world::Entity e = selection();
        if (e.is_null()) return;
        auto states = capture_subtree(*world, e);
        // Fresh identities; keep the copy under the same parent, offset a little.
        std::mt19937_64 rng(static_cast<std::uint64_t>(world->entity_count()) * 7919u + 17u);
        std::vector<std::pair<Uuid, Uuid>> remap;
        for (auto& s : states) {
            const Uuid fresh = Uuid::generate(rng);
            remap.emplace_back(s.id, fresh);
            s.id = fresh;
        }
        for (auto& s : states)
            for (const auto& [from, to] : remap)
                if (s.parent == from) s.parent = to;
        states.front().child_index = static_cast<std::size_t>(-1);  // append
        if (states.front().transform) states.front().transform->position = states.front().transform->position + Vec3{1, 0, 0};
        if (states.front().name) states.front().name->value += " Copy";
        restore(*world, states);
        world::propagate_transforms(*world);
        record(make_create_command("Duplicate " + display_name(*world, e), states));
        selected = states.front().id;
    }

    void reparent(world::Entity child, world::Entity new_parent) {
        if (child.is_null() || child == new_parent) return;
        if (!new_parent.is_null() && h::is_ancestor(*world, child, new_parent)) return;
        const EntityState before = capture(*world, child);
        const Mat4 keep = world_matrix(child);
        if (new_parent.is_null()) h::clear_parent(*world, child);
        else if (!h::set_parent(*world, child, new_parent)) return;
        if (world->has<world::Transform>(child)) set_world_matrix(child, keep);  // stay in place visually
        world::propagate_transforms(*world);
        record(make_reparent_command("Attach " + display_name(*world, child), before, capture(*world, child)));
    }

    void focus_selected() {
        const world::Entity e = selection();
        if (e.is_null()) return;
        const Mat4 m = world_matrix(e);
        const float sx = math::length({m.m[0], m.m[1], m.m[2]});
        const float sy = math::length({m.m[4], m.m[5], m.m[6]});
        const float sz = math::length({m.m[8], m.m[9], m.m[10]});
        cam.frame(m.translation(), std::max(0.5f, 0.87f * std::max({sx, sy, sz})));
    }

    // ---- Play mode ----------------------------------------------------

    void play_or_pause() {
        if (play == PlayState::Editing) {
            play_backup = capture_world(*world);
            play = PlayState::Playing;
            if (auto created = simulation::PhysicsWorld::create({.logger = &logger})) {
                physics = std::move(created).value();
            } else {
                AETHER_LOG(logger, LogLevel::Error, "editor", "physics is unavailable: {}", created.error().message);
            }
            physics_accumulator = 0.0f;
            AETHER_LOG(logger, LogLevel::Info, "editor", "play");
        } else {
            play = play == PlayState::Playing ? PlayState::Paused : PlayState::Playing;
        }
    }

    void stop() {
        if (play == PlayState::Editing) return;
        play = PlayState::Editing;
        physics.reset();
        physics_stats = {};
        reset_world();
        restore(*world, play_backup);
        world::propagate_transforms(*world);
        play_backup.clear();
        AETHER_LOG(logger, LogLevel::Info, "editor", "stopped; level restored");
    }

    // ---- Frame ----------------------------------------------------------

    void update(float dt) {
        if (play == PlayState::Playing) {
            schedule.run(*world, nullptr, dt);
            if (physics) {
                // Fixed 60 Hz steps; a long frame simulates at most four of them.
                constexpr float kStep = 1.0f / 60.0f;
                physics_accumulator = std::min(physics_accumulator + dt, 4.0f * kStep);
                for (; physics_accumulator >= kStep; physics_accumulator -= kStep)
                    physics_stats = physics->step(*world, kStep);
            }
        }
        world::propagate_transforms(*world);
        frame_ms[frame_ms_pos++ % frame_ms.size()] = dt * 1000.0f;
    }

    void shortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        // Dialogs (save prompt, file browser, errors) own the keyboard.
        if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo.undo(*world);
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
            undo.redo(*world);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicate_selected();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) request(Pending::New);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) request(Pending::Open);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (io.KeyShift) open_dialog(DialogMode::Save);
            else save();
        }
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false)) play_or_pause();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) delete_selected();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (play != PlayState::Editing) stop();
            else selected = {};
        }
        if (!flying && !ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmo_op = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmo_op = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmo_op = ImGuizmo::SCALE;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) focus_selected();
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) show_grid = !show_grid;
        }
    }

    void draw(float) {
        shortcuts();
        ImGuiViewport* main = ImGui::GetMainViewport();
        main_menu();
        toolbar(main);
        status_bar(main);
        const ImGuiID dock = ImGui::DockSpaceOverViewport(ImGui::GetID("AetherDockSpace"), main,
                                                          ImGuiDockNodeFlags_PassthruCentralNode);
        if (reset_layout || ImGui::DockBuilderGetNode(dock) == nullptr ||
            ImGui::DockBuilderGetNode(dock)->IsLeafNode()) {
            build_default_layout(dock, main->WorkSize);
            reset_layout = false;
            focus_content = 3;
        }
        viewport_panel();
        if (show_outliner) outliner_panel();
        if (show_world) world_settings_panel();
        if (show_details) details_panel();
        if (show_place) place_panel();
        if (show_content) content_panel();
        if (show_log) log_panel();
        if (show_stats) stats_panel();
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);
        about_popups();
        file_popups();
    }

    void build_default_layout(ImGuiID dock, ImVec2 size) {
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, size);
        ImGuiID center = dock;
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID right_bottom = 0;
        const ImGuiID right_top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.42f, nullptr, &right_bottom);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow(kViewport, center);
        ImGui::DockBuilderDockWindow(kOutliner, right_top);
        ImGui::DockBuilderDockWindow(kWorldSettings, right_bottom);
        ImGui::DockBuilderDockWindow(kDetails, right_bottom);
        ImGui::DockBuilderDockWindow(kPlace, left);
        ImGui::DockBuilderDockWindow(kContent, bottom);
        ImGui::DockBuilderDockWindow(kLog, bottom);
        ImGui::DockBuilderDockWindow(kStats, bottom);
        ImGui::DockBuilderFinish(dock);
    }

    // ---- Chrome ---------------------------------------------------------

    void main_menu() {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Level", "Ctrl+N")) request(Pending::New);
            if (ImGui::MenuItem("Open Level...", "Ctrl+O")) request(Pending::Open);
            if (ImGui::BeginMenu("Open Recent", !recent.empty())) {
                for (const auto& r : recent)
                    if (ImGui::MenuItem(to_utf8(r.filename()).c_str())) request(Pending::Open, r);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Open Demo Level")) request(Pending::Demo);
            ImGui::Separator();
            const bool editing = play == PlayState::Editing;
            if (ImGui::MenuItem("Save", "Ctrl+S", false, editing)) save();
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, editing)) open_dialog(DialogMode::Save);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) request(Pending::Quit);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            const std::string u = undo.can_undo() ? "Undo " + undo.undo_label() : "Undo";
            const std::string r = undo.can_redo() ? "Redo " + undo.redo_label() : "Redo";
            if (ImGui::MenuItem(u.c_str(), "Ctrl+Z", false, undo.can_undo())) undo.undo(*world);
            if (ImGui::MenuItem(r.c_str(), "Ctrl+Y", false, undo.can_redo())) undo.redo(*world);
            ImGui::Separator();
            const bool has_sel = !selection().is_null();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_sel)) duplicate_selected();
            if (ImGui::MenuItem("Delete", "Del", false, has_sel)) delete_selected();
            if (ImGui::MenuItem("Select None", "Esc", false, has_sel)) selected = {};
            ImGui::Separator();
            if (ImGui::MenuItem("Focus Selection", "F", false, has_sel)) focus_selected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem(kOutliner, nullptr, &show_outliner);
            ImGui::MenuItem(kDetails, nullptr, &show_details);
            ImGui::MenuItem(kWorldSettings, nullptr, &show_world);
            ImGui::MenuItem(kPlace, nullptr, &show_place);
            ImGui::MenuItem(kContent, nullptr, &show_content);
            ImGui::MenuItem(kLog, nullptr, &show_log);
            ImGui::MenuItem(kStats, nullptr, &show_stats);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                show_outliner = show_details = show_place = show_log = show_stats = show_content = show_world = true;
                reset_layout = true;
            }
            ImGui::MenuItem("ImGui Demo (developer)", nullptr, &show_demo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Controls")) show_controls = true;
            if (ImGui::MenuItem("About Aether Editor")) show_about = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    static bool toggle_button(const char* label, bool active, const char* tooltip) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool clicked = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    void toolbar(ImGuiViewport* main) {
        const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::BeginViewportSideBar("##Toolbar", main, ImGuiDir_Up, height, flags)) {
            const bool editing = play == PlayState::Editing;
            if (editing) {
                if (toggle_button("  Play  ", false, "Simulate the level (Alt+P)")) play_or_pause();
            } else {
                if (toggle_button(play == PlayState::Paused ? " Resume " : " Pause ", play == PlayState::Paused,
                                  "Pause or resume simulation (Alt+P)"))
                    play_or_pause();
                ImGui::SameLine();
                if (toggle_button("  Stop  ", false, "Stop and restore the level (Esc)")) stop();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (toggle_button("Move", gizmo_op == ImGuizmo::TRANSLATE, "Translate (W)")) gizmo_op = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (toggle_button("Rotate", gizmo_op == ImGuizmo::ROTATE, "Rotate (E)")) gizmo_op = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (toggle_button("Scale", gizmo_op == ImGuizmo::SCALE, "Scale (R)")) gizmo_op = ImGuizmo::SCALE;
            ImGui::SameLine();
            if (toggle_button(gizmo_mode == ImGuizmo::WORLD ? "World" : "Local", false,
                              "Gizmo space: world or local axes"))
                gizmo_mode = gizmo_mode == ImGuizmo::WORLD ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &snap);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Snap gizmo moves (hold Ctrl to toggle)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            if (gizmo_op == ImGuizmo::TRANSLATE) ImGui::DragFloat("##snapv", &snap_translate, 0.05f, 0.01f, 100.0f, "%.2f m");
            else if (gizmo_op == ImGuizmo::ROTATE) ImGui::DragFloat("##snapv", &snap_rotate, 1.0f, 1.0f, 90.0f, "%.0f deg");
            else ImGui::DragFloat("##snapv", &snap_scale, 0.01f, 0.01f, 10.0f, "%.2f");
            ImGui::SameLine();
            ImGui::Checkbox("Grid", &show_grid);
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::SliderFloat("##speed", &cam.fly_speed, 0.5f, 100.0f, "Cam %.1f m/s", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Camera fly speed (RMB + WASD)");
        }
        ImGui::End();
    }

    void status_bar(ImGuiViewport* main) {
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
        if (ImGui::BeginViewportSideBar("##StatusBar", main, ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
            if (ImGui::BeginMenuBar()) {
                const char* state = play == PlayState::Playing ? "Playing" : play == PlayState::Paused ? "Paused" : "Editing";
                ImGui::Text("%s", state);
                ImGui::Separator();
                ImGui::Text("%s%s", level_name().c_str(), dirty() ? " (modified)" : "");
                ImGui::Separator();
                ImGui::Text("%zu entities", world->entity_count());
                ImGui::Separator();
                const world::Entity sel = selection();
                ImGui::Text("Selected: %s", sel.is_null() ? "none" : display_name(*world, sel).c_str());
                if (undo.can_undo()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Last: %s", undo.undo_label().c_str());
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();
    }

    void about_popups() {
        if (show_about) ImGui::OpenPopup("About Aether Editor");
        if (show_controls) ImGui::OpenPopup("Controls");
        show_about = show_controls = false;
        if (ImGui::BeginPopupModal("About Aether Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Aether Editor");
            ImGui::TextDisabled("Open source (Apache-2.0), AI-native 3D engine - early preview");
            ImGui::Separator();
            ImGui::Text("GPU: %s", renderer.gpu().name.c_str());
            ImGui::Text("API: %s%s", renderer.gpu().api.c_str(), renderer.gpu().validation ? " (validation on)" : "");
            ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static const char* rows[][2] = {
                {"LMB click", "Select"},
                {"RMB drag", "Look around"},
                {"RMB + W/A/S/D, Q/E", "Fly"},
                {"Alt + LMB drag", "Orbit around focus"},
                {"MMB drag", "Pan"},
                {"Mouse wheel", "Zoom (dolly)"},
                {"F", "Focus selection"},
                {"W / E / R", "Move / Rotate / Scale gizmo"},
                {"Ctrl (while dragging)", "Toggle snapping"},
                {"Ctrl+Z / Ctrl+Y", "Undo / Redo"},
                {"Ctrl+D / Del", "Duplicate / Delete"},
                {"Alt+P / Esc", "Play-Pause / Stop"},
                {"G", "Toggle grid"},
            };
            if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                for (const auto& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r[0]);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r[1]);
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---- Viewport -------------------------------------------------------

    void viewport_panel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = ImGui::Begin(kViewport, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (!open) {
            ImGui::End();
            return;
        }
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(16.0f, avail.x), std::max(16.0f, avail.y)};
        const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
        renderer.set_viewport_size(static_cast<std::uint32_t>(size.x * scale), static_cast<std::uint32_t>(size.y * scale));
        vp_min = ImGui::GetCursorScreenPos();
        vp_size = size;
        ImGui::Image(static_cast<ImTextureID>(renderer.viewport_texture()), size);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDrag)) {
                const std::string path(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize));
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                spawn_model(path, ground_point(2.0f * (mouse.x - vp_min.x) / size.x - 1.0f,
                                               2.0f * (mouse.y - vp_min.y) / size.y - 1.0f));
            }
            ImGui::EndDragDropTarget();
        }

        const float aspect = size.x / size.y;
        const render::Camera rc = cam.to_render_camera();
        const Mat4 view = render::look_at(rc.position, rc.target, rc.up);
        const Mat4 proj_gl = perspective_gl(rc.fov_y, aspect, rc.near_plane, rc.far_plane);

        bool gizmo_hot = false;
        const world::Entity sel = selection();
        if (!sel.is_null() && world->has<world::Transform>(sel)) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(vp_min.x, vp_min.y, size.x, size.y);
            Mat4 m = world_matrix(sel);
            const bool snapping = snap != ImGui::GetIO().KeyCtrl;
            const float s = gizmo_op == ImGuizmo::TRANSLATE ? snap_translate : gizmo_op == ImGuizmo::ROTATE ? snap_rotate : snap_scale;
            const float snap_values[3] = {s, s, s};
            const ImGuizmo::MODE mode = gizmo_op == ImGuizmo::SCALE ? ImGuizmo::LOCAL : gizmo_mode;
            const bool was_using = ImGuizmo::IsUsing();
            if (ImGuizmo::Manipulate(view.m, proj_gl.m, gizmo_op, mode, m.m, nullptr, snapping ? snap_values : nullptr)) {
                if (!gizmo_before) gizmo_before = capture(*world, sel);
                set_world_matrix(sel, m);
                world::propagate_transforms(*world);
            }
            if (was_using && !ImGuizmo::IsUsing() && gizmo_before) {
                const char* what = gizmo_op == ImGuizmo::TRANSLATE ? "Move " : gizmo_op == ImGuizmo::ROTATE ? "Rotate " : "Scale ";
                record(make_edit_command(what + display_name(*world, sel), *gizmo_before, capture(*world, sel)));
                gizmo_before.reset();
            }
            gizmo_hot = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        }

        camera_input(hovered, size);

        // Light icons: lights have no mesh, so they are drawn and picked in
        // screen space.
        struct Icon {
            world::Entity entity;
            ImVec2 pos;
            ImVec2 arrow_end;  // directional lights: where the light goes
            const render::Light* light;
        };
        std::vector<Icon> icons;
        const Mat4 view_proj = render::view_projection(rc, aspect);
        const auto to_screen = [&](Vec3 p, ImVec2& out) {
            const Vec3 ndc = render::project_to_ndc(view_proj, p);
            const Vec3 eye = view.transform_point(p);
            if (eye.z > -rc.near_plane) return false;  // behind the camera
            out = {vp_min.x + (ndc.x * 0.5f + 0.5f) * size.x, vp_min.y + (ndc.y * 0.5f + 0.5f) * size.y};
            return true;
        };
        world->each<const world::GlobalTransform, const render::Light>(
            [&](world::Entity e, const world::GlobalTransform& g, const render::Light& light) {
                const Vec3 pos = g.matrix.translation();
                Icon icon{e, {}, {}, &light};
                if (!to_screen(pos, icon.pos)) return;
                const Vec3 dir = math::normalize(Vec3{-g.matrix.m[8], -g.matrix.m[9], -g.matrix.m[10]});
                if (!to_screen(pos + dir * 1.5f, icon.arrow_end)) icon.arrow_end = icon.pos;
                icons.push_back(icon);
            });

        // Click to select (not when the click was a camera drag or on the gizmo).
        ImGuiIO& io = ImGui::GetIO();
        if (hovered && !gizmo_hot && !io.KeyAlt && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x +
                    ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y < 16.0f) {
            const float nx = 2.0f * (io.MousePos.x - vp_min.x) / size.x - 1.0f;
            const float ny = 2.0f * (io.MousePos.y - vp_min.y) / size.y - 1.0f;
            world::Entity hit;
            float best = 14.0f * 14.0f;  // icon radius in pixels, squared
            for (const Icon& icon : icons) {
                const float dx = io.MousePos.x - icon.pos.x, dy = io.MousePos.y - icon.pos.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    hit = icon.entity;
                }
            }
            if (hit.is_null()) hit = pick(*world, screen_ray(view_proj, nx, ny), library->bounds()).entity;
            select(hit);
        }

        // Overlay: view label, selection, frame rate.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(vp_min, {vp_min.x + size.x, vp_min.y + size.y}, true);
        for (const Icon& icon : icons) {
            const bool chosen = icon.entity == selection();
            const Vec3 c = icon.light->color;
            const ImU32 tint = IM_COL32(static_cast<int>(std::min(1.0f, c.x) * 255), static_cast<int>(std::min(1.0f, c.y) * 255),
                                        static_cast<int>(std::min(1.0f, c.z) * 255), 255);
            const ImU32 outline = chosen ? IM_COL32(255, 150, 40, 255) : IM_COL32(20, 20, 24, 220);
            if (icon.light->type == render::LightType::Directional) {
                if (chosen) dl->AddLine(icon.pos, icon.arrow_end, IM_COL32(255, 210, 90, 255), 2.0f);
                for (int k = 0; k < 8; ++k) {  // sun rays
                    const float a = static_cast<float>(k) * math::kPi / 4.0f;
                    const ImVec2 d{std::cos(a), std::sin(a)};
                    dl->AddLine({icon.pos.x + d.x * 8, icon.pos.y + d.y * 8}, {icon.pos.x + d.x * 12, icon.pos.y + d.y * 12},
                                tint, 2.0f);
                }
                dl->AddCircleFilled(icon.pos, 6.5f, tint);
                dl->AddCircle(icon.pos, 7.0f, outline, 0, chosen ? 2.0f : 1.0f);
            } else {
                dl->AddCircleFilled(icon.pos, 9.0f, IM_COL32(20, 20, 24, 170));
                dl->AddCircleFilled(icon.pos, 6.0f, tint);
                dl->AddCircle(icon.pos, 9.5f, outline, 0, chosen ? 2.0f : 1.0f);
            }
        }
        // Collider of the selected actor, as a wireframe.
        if (const world::Entity chosen = selection(); !chosen.is_null() && world->has<simulation::Collider>(chosen)) {
            const simulation::Collider& c = *world->get<simulation::Collider>(chosen);
            const Mat4 m = world_matrix(chosen);
            const ImU32 wire = IM_COL32(90, 230, 120, 230);
            const auto line = [&](Vec3 a, Vec3 b) {
                ImVec2 pa, pb;
                if (to_screen(m.transform_point(a + c.offset), pa) && to_screen(m.transform_point(b + c.offset), pb))
                    dl->AddLine(pa, pb, wire, 1.5f);
            };
            // Circle of `radius` around `centre` in the plane spanned by u and v.
            const auto circle = [&](Vec3 centre, Vec3 u, Vec3 v, float radius, float from = 0.0f, float to = 2.0f * math::kPi) {
                constexpr int kSegments = 32;
                for (int k = 0; k < kSegments; ++k) {
                    const float a0 = from + (to - from) * static_cast<float>(k) / kSegments;
                    const float a1 = from + (to - from) * static_cast<float>(k + 1) / kSegments;
                    line(centre + (u * std::cos(a0) + v * std::sin(a0)) * radius, centre + (u * std::cos(a1) + v * std::sin(a1)) * radius);
                }
            };
            const Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
            if (c.shape == simulation::ShapeType::Box) {
                const Vec3 h = c.half_extents;
                for (int k = 0; k < 4; ++k) {
                    const float a = (k & 1) ? 1.0f : -1.0f, b = (k & 2) ? 1.0f : -1.0f;
                    line({-h.x, a * h.y, b * h.z}, {h.x, a * h.y, b * h.z});
                    line({a * h.x, -h.y, b * h.z}, {a * h.x, h.y, b * h.z});
                    line({a * h.x, b * h.y, -h.z}, {a * h.x, b * h.y, h.z});
                }
            } else if (c.shape == simulation::ShapeType::Sphere) {
                circle({}, x, y, c.radius);
                circle({}, y, z, c.radius);
                circle({}, x, z, c.radius);
            } else {
                const Vec3 top{0, c.half_height, 0}, bottom{0, -c.half_height, 0};
                circle(top, x, z, c.radius);
                circle(bottom, x, z, c.radius);
                for (const Vec3 side : {x, z}) {
                    line(top + side * c.radius, bottom + side * c.radius);
                    line(top - side * c.radius, bottom - side * c.radius);
                    circle(top, side, y, c.radius, 0.0f, math::kPi);
                    circle(bottom, side, y, c.radius, math::kPi, 2.0f * math::kPi);
                }
            }
        }
        dl->PopClipRect();
        const ImVec2 p{vp_min.x + 10, vp_min.y + 8};
        dl->AddText(p, IM_COL32(220, 220, 225, 230), "Perspective   Lit");
        char fps[64];
        std::snprintf(fps, sizeof fps, "%.0f fps  %.2f ms", io.Framerate, 1000.0f / std::max(1.0f, io.Framerate));
        const ImVec2 fs = ImGui::CalcTextSize(fps);
        dl->AddText({vp_min.x + size.x - fs.x - 10, vp_min.y + 8}, IM_COL32(150, 230, 150, 230), fps);
        if (play != PlayState::Editing) {
            const char* label = play == PlayState::Playing ? "PLAYING  (Esc to stop)" : "PAUSED  (Esc to stop)";
            const ImVec2 ls = ImGui::CalcTextSize(label);
            dl->AddRectFilled({vp_min.x + size.x * 0.5f - ls.x * 0.5f - 8, vp_min.y + 6},
                              {vp_min.x + size.x * 0.5f + ls.x * 0.5f + 8, vp_min.y + 10 + ls.y},
                              IM_COL32(20, 90, 30, 200), 4.0f);
            dl->AddText({vp_min.x + size.x * 0.5f - ls.x * 0.5f, vp_min.y + 8}, IM_COL32(220, 255, 220, 255), label);
        }
        if (world->entity_count() == 0) {
            const char* hint = "Empty level - add actors from Place Actors, or File > Open Demo Level";
            const ImVec2 hs = ImGui::CalcTextSize(hint);
            dl->AddText({vp_min.x + (size.x - hs.x) * 0.5f, vp_min.y + size.y * 0.5f}, IM_COL32(180, 180, 185, 200), hint);
        }
        ImGui::End();
    }

    void camera_input(bool hovered, ImVec2 size) {
        ImGuiIO& io = ImGui::GetIO();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) flying = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) flying = false;
        if (flying) {
            cam.look(io.MouseDelta.x, io.MouseDelta.y);
            Vec3 move{};
            if (ImGui::IsKeyDown(ImGuiKey_W)) move.z += 1;
            if (ImGui::IsKeyDown(ImGuiKey_S)) move.z -= 1;
            if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1;
            if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1;
            if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1;
            const float boost = io.KeyShift ? 3.0f : 1.0f;
            cam.fly(move, io.DeltaTime * boost);
            if (io.MouseWheel != 0) cam.fly_speed = std::clamp(cam.fly_speed * std::pow(1.2f, io.MouseWheel), 0.5f, 100.0f);
            return;
        }
        if (!hovered) return;
        if (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) cam.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) cam.pan(io.MouseDelta.x, io.MouseDelta.y, size.y);
        if (io.MouseWheel != 0) cam.dolly(io.MouseWheel);
    }

    // ---- Outliner -------------------------------------------------------

    void outliner_row(world::Entity e) {
        const auto kids = h::children_of(*world, e);
        const std::string name = display_name(*world, e);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
        if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (world->id_of(e) == selected) flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushID(static_cast<int>(e.index));
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) select(e);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            select(e);
            focus_selected();
        }
        if (ImGui::BeginDragDropSource()) {
            const Uuid id = world->id_of(e);
            ImGui::SetDragDropPayload(kOutlinerDrag, &id, sizeof id);
            ImGui::Text("Attach %s", name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kOutlinerDrag)) {
                Uuid id;
                std::memcpy(&id, p->Data, sizeof id);
                reparent(world->find(id), e);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            select(e);
            entity_context_menu(e);
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", actor_type(e));
        if (open && !kids.empty()) {
            const std::vector<world::Entity> copy(kids.begin(), kids.end());  // edits may reallocate
            for (const world::Entity c : copy)
                if (world->alive(c)) outliner_row(c);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void entity_context_menu(world::Entity e) {
        if (ImGui::MenuItem("Focus", "F")) focus_selected();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_selected();
        if (ImGui::MenuItem("Detach from parent", nullptr, false, !h::parent_of(*world, e).is_null()))
            reparent(e, world::kNullEntity);
        if (ImGui::MenuItem("Add child cube")) {
            const world::Entity child = world->create();
            world->add<world::Name>(child, world::Name{"Cube"});
            world->add<world::Transform>(child, world::Transform{{1.5f, 0, 0}, Quat::identity(), {0.5f, 0.5f, 0.5f}});
            world->add<render::MeshInstance>(child, render::MeshInstance{cube, {0.75f, 0.75f, 0.78f}});
            (void)h::set_parent(*world, child, e);
            world::propagate_transforms(*world);
            record(make_create_command("Add child cube", capture_subtree(*world, child)));
            select(child);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) delete_selected();
    }

    void outliner_panel() {
        if (!ImGui::Begin(kOutliner, &show_outliner)) {
            ImGui::End();
            return;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##search", "Search...", outliner_filter.InputBuf, IM_ARRAYSIZE(outliner_filter.InputBuf)))
            outliner_filter.Build();
        ImGui::TextDisabled("%zu entities", world->entity_count());

        std::vector<world::Entity> roots;
        std::vector<world::Entity> all;
        const auto collect = [&](world::Entity e) {
            if (std::find(all.begin(), all.end(), e) != all.end()) return;
            all.push_back(e);
            const world::Entity p = h::parent_of(*world, e);
            if (p.is_null() || !world->alive(p)) roots.push_back(e);
        };
        world->each<const world::Name>([&](world::Entity e, const world::Name&) { collect(e); });
        world->each<const world::Transform>([&](world::Entity e, const world::Transform&) { collect(e); });
        std::sort(roots.begin(), roots.end(), [&](world::Entity a, world::Entity b) {
            return display_name(*world, a) < display_name(*world, b);
        });

        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("outliner", 2, tflags, ImVec2(0, 0))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            if (outliner_filter.IsActive()) {
                for (const world::Entity e : all) {
                    const std::string name = display_name(*world, e);
                    if (!outliner_filter.PassFilter(name.c_str())) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(e.index));
                    if (ImGui::Selectable(name.c_str(), world->id_of(e) == selected, ImGuiSelectableFlags_SpanAllColumns))
                        select(e);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", actor_type(e));
                }
            } else {
                for (const world::Entity r : roots)
                    if (world->alive(r)) outliner_row(r);
            }
            ImGui::EndTable();
        }
        // Dropping on empty space detaches; right-click empty space to add actors.
        if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("outliner_bg"))) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kOutlinerDrag)) {
                Uuid id;
                std::memcpy(&id, p->Data, sizeof id);
                reparent(world->find(id), world::kNullEntity);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextWindow("outliner_ctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Cube")) spawn("Cube", true, false);
            if (ImGui::MenuItem("Add Empty")) spawn("Empty", false, false);
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    // ---- Details --------------------------------------------------------

    bool component_header(const char* label, bool removable, bool& remove) {
        ImGui::PushID(label);
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (removable) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() + ImGui::GetCursorPosX() -
                            ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::SmallButton("x")) remove = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        }
        ImGui::PopID();
        return open;
    }

    bool vec3_row(const char* label, Vec3& v, float speed, const char* fmt, Vec3 reset) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 3 * 4) / 3.0f;
        const ImU32 colors[3] = {IM_COL32(200, 60, 60, 255), IM_COL32(80, 170, 60, 255), IM_COL32(60, 110, 210, 255)};
        float* comps[3] = {&v.x, &v.y, &v.z};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, 4);
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            ImGui::SetNextItemWidth(w);
            ImGui::PushID(i);
            changed |= ImGui::DragFloat("##c", comps[i], speed, 0, 0, fmt);
            pending_track_ |= ImGui::IsItemActivated();
            if (ImGui::IsItemDeactivatedAfterEdit()) pending_commit_ = true;
            ImGui::PopID();
            ImGui::GetWindowDrawList()->AddRectFilled(cp, {cp.x + 3, cp.y + ImGui::GetFrameHeight()}, colors[i], 2.0f);
        }
        ImGui::SameLine(0, 4);
        if (ImGui::Button("R", ImVec2(ImGui::GetFrameHeight(), 0))) {
            v = reset;
            changed = true;
            pending_reset_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Reset");
        ImGui::PopID();
        return changed;
    }

    // Undo plumbing for multi-widget rows.
    bool pending_track_ = false, pending_commit_ = false, pending_reset_ = false;

    void commit_row(world::Entity e, const std::string& label, const EntityState& before_frame) {
        if (pending_track_) edit_before = before_frame;
        if (pending_reset_) record(make_edit_command("Reset " + label, before_frame, capture(*world, e)));
        if (pending_commit_ && edit_before) {
            record(make_edit_command(label, *edit_before, capture(*world, e)));
            edit_before.reset();
        }
        pending_track_ = pending_commit_ = pending_reset_ = false;
    }

    void details_panel() {
        if (!ImGui::Begin(kDetails, &show_details)) {
            ImGui::End();
            return;
        }
        const world::Entity e = selection();
        if (e.is_null()) {
            ImGui::TextDisabled("Select an actor to view its details.");
            ImGui::End();
            return;
        }

        // Name
        char buf[256];
        const std::string name = world->has<world::Name>(e) ? world->get<world::Name>(e)->value : "";
        std::snprintf(buf, sizeof buf, "%s", name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##name", "Name", buf, sizeof buf)) world->add<world::Name>(e, world::Name{buf});
        track_edit(e, "Rename");
        ImGui::TextDisabled("%s", world->id_of(e).to_string().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy ID")) ImGui::SetClipboardText(world->id_of(e).to_string().c_str());
        ImGui::Spacing();

        const EntityState frame_before = capture(*world, e);
        const ImGuiTableFlags tflags = ImGuiTableFlags_SizingStretchProp;

        // Transform
        if (auto* t = world->get<world::Transform>(e)) {
            bool remove = false;
            if (component_header("Transform", false, remove) && ImGui::BeginTable("transform", 2, tflags)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                world::Transform edit = *t;
                Vec3 euler = math::euler_degrees_from_quat(edit.rotation);
                bool changed = vec3_row("Location", edit.position, 0.05f, "%.2f", {0, 0, 0});
                commit_row(e, "Move " + display_name(*world, e), frame_before);
                if (vec3_row("Rotation", euler, 0.5f, "%.1f", {0, 0, 0})) {
                    edit.rotation = math::quat_from_euler_degrees(euler);
                    changed = true;
                }
                commit_row(e, "Rotate " + display_name(*world, e), frame_before);
                changed |= vec3_row("Scale", edit.scale, 0.01f, "%.2f", {1, 1, 1});
                commit_row(e, "Scale " + display_name(*world, e), frame_before);
                if (changed) world->add<world::Transform>(e, edit);
                ImGui::EndTable();
            }
        }

        // Static mesh
        if (world->has<render::MeshInstance>(e)) {
            bool remove = false;
            if (component_header("Static Mesh", true, remove) && !remove) {
                render::MeshInstance m = *world->get<render::MeshInstance>(e);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Mesh");
                ImGui::SameLine(80);
                ImGui::TextDisabled("%s", asset_label(library->names().name_of(m.mesh)).c_str());
                if (m.texture != render::kWhiteTexture) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Texture");
                    ImGui::SameLine(80);
                    ImGui::TextDisabled("%s", asset_label(library->names().texture_name_of(m.texture)).c_str());
                }
                const auto texture_row = [&](const char* label, render::TextureId t) {
                    if (t == render::kWhiteTexture) return;
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", label);
                    ImGui::SameLine(80);
                    ImGui::TextDisabled("%s", asset_label(library->names().texture_name_of(t)).c_str());
                };
                texture_row("Normal", m.normal_texture);
                texture_row("Metal/Rgh", m.metallic_roughness_texture);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Color");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit3("##color", &m.color.x)) world->add<render::MeshInstance>(e, m);
                track_edit(e, "Change color");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Metallic");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##metallic", &m.metallic, 0.0f, 1.0f, "%.2f")) world->add<render::MeshInstance>(e, m);
                track_edit(e, "Change metallic");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Roughness");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##roughness", &m.roughness, 0.0f, 1.0f, "%.2f")) world->add<render::MeshInstance>(e, m);
                track_edit(e, "Change roughness");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Emissive");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit3("##emissive", &m.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                    world->add<render::MeshInstance>(e, m);
                track_edit(e, "Change emissive");
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<render::MeshInstance>(e);
                record(make_edit_command("Remove Static Mesh", before, capture(*world, e)));
            }
        }

        // Physics
        const auto prop = [](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", label);
            ImGui::SameLine(96);
            ImGui::SetNextItemWidth(-1);
        };
        if (world->has<simulation::Collider>(e)) {
            bool remove = false;
            if (component_header("Collider", true, remove) && !remove) {
                simulation::Collider c = *world->get<simulation::Collider>(e);
                const auto dragged = [&](bool changed, const char* label) {
                    if (changed) world->add<simulation::Collider>(e, c);
                    track_edit(e, label);
                };
                const auto at_once = [&](const char* label) {
                    const EntityState before = capture(*world, e);
                    world->add<simulation::Collider>(e, c);
                    record(make_edit_command(label, before, capture(*world, e)));
                };
                prop("Shape");
                int shape = static_cast<int>(c.shape);
                if (ImGui::Combo("##shape", &shape, "Box\0Sphere\0Capsule\0")) {
                    c = fitted_collider(e, static_cast<simulation::ShapeType>(shape));
                    at_once("Change collider shape");
                }
                if (c.shape == simulation::ShapeType::Box) {
                    prop("Half size");
                    dragged(ImGui::DragFloat3("##half", &c.half_extents.x, 0.01f, 0.01f, 1000.0f, "%.2f"), "Resize collider");
                } else {
                    prop("Radius");
                    dragged(ImGui::DragFloat("##radius", &c.radius, 0.01f, 0.01f, 1000.0f, "%.2f m"), "Resize collider");
                }
                if (c.shape == simulation::ShapeType::Capsule) {
                    prop("Half height");
                    dragged(ImGui::DragFloat("##halfheight", &c.half_height, 0.01f, 0.01f, 1000.0f, "%.2f m"), "Resize collider");
                }
                prop("Offset");
                dragged(ImGui::DragFloat3("##offset", &c.offset.x, 0.01f, -1000.0f, 1000.0f, "%.2f"), "Move collider");
                if (ImGui::Button("Fit to mesh")) {
                    c = fitted_collider(e, c.shape);
                    at_once("Fit collider to mesh");
                }
                if (!world->has<simulation::RigidBody>(e)) ImGui::TextDisabled("No Rigid Body: solid and never moves.");
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<simulation::Collider>(e);
                record(make_edit_command("Remove Collider", before, capture(*world, e)));
            }
        }
        if (world->has<simulation::RigidBody>(e)) {
            bool remove = false;
            if (component_header("Rigid Body", true, remove) && !remove) {
                simulation::RigidBody rb = *world->get<simulation::RigidBody>(e);
                const auto dragged = [&](bool changed, const char* label) {
                    if (changed) world->add<simulation::RigidBody>(e, rb);
                    track_edit(e, label);
                };
                prop("Type");
                int type = static_cast<int>(rb.type);
                if (ImGui::Combo("##bodytype", &type, "Static\0Dynamic\0Kinematic\0")) {
                    const EntityState before = capture(*world, e);
                    rb.type = static_cast<simulation::BodyType>(type);
                    world->add<simulation::RigidBody>(e, rb);
                    record(make_edit_command("Change body type", before, capture(*world, e)));
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Dynamic: moved by gravity and collisions.\nKinematic: follows its transform and pushes others.\n"
                                      "Static: never moves.");
                if (rb.type == simulation::BodyType::Dynamic) {
                    prop("Mass");
                    dragged(ImGui::DragFloat("##mass", &rb.mass, 0.05f, 0.01f, 100000.0f, "%.2f kg"), "Change mass");
                }
                prop("Friction");
                dragged(ImGui::SliderFloat("##friction", &rb.friction, 0.0f, 1.5f, "%.2f"), "Change friction");
                prop("Bounciness");
                dragged(ImGui::SliderFloat("##restitution", &rb.restitution, 0.0f, 1.0f, "%.2f"), "Change bounciness");
                if (rb.type == simulation::BodyType::Dynamic) {
                    prop("Gravity");
                    dragged(ImGui::DragFloat("##gravityscale", &rb.gravity_scale, 0.01f, -10.0f, 10.0f, "x %.2f"), "Change gravity scale");
                    prop("Damping");
                    dragged(ImGui::DragFloat("##lineardamping", &rb.linear_damping, 0.005f, 0.0f, 10.0f, "%.3f"), "Change damping");
                    prop("Spin damping");
                    dragged(ImGui::DragFloat("##angulardamping", &rb.angular_damping, 0.005f, 0.0f, 10.0f, "%.3f"), "Change spin damping");
                    prop("Velocity");
                    dragged(ImGui::DragFloat3("##velocity", &rb.linear_velocity.x, 0.05f, -1000.0f, 1000.0f, "%.2f"), "Change velocity");
                }
                if (!world->has<simulation::Collider>(e)) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Needs a Collider to take part in physics.");
                if (play == PlayState::Editing) ImGui::TextDisabled("Simulates while playing (Alt+P).");
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<simulation::RigidBody>(e);
                record(make_edit_command("Remove Rigid Body", before, capture(*world, e)));
            }
        }

        // Light
        if (world->has<render::Light>(e)) {
            bool remove = false;
            if (component_header("Light", true, remove) && !remove) {
                render::Light l = *world->get<render::Light>(e);
                const auto row = [](const char* label) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", label);
                    ImGui::SameLine(80);
                    ImGui::SetNextItemWidth(-1);
                };
                row("Type");
                int type = l.type == render::LightType::Directional ? 0 : 1;
                if (ImGui::Combo("##type", &type, "Directional\0Point\0")) {
                    const EntityState before = capture(*world, e);
                    const render::Light fresh = default_light(type == 0 ? render::LightType::Directional : render::LightType::Point);
                    l.type = fresh.type;
                    l.intensity = fresh.intensity;
                    world->add<render::Light>(e, l);
                    record(make_edit_command("Change light type", before, capture(*world, e)));
                }
                row("Color");
                if (ImGui::ColorEdit3("##lcolor", &l.color.x)) world->add<render::Light>(e, l);
                track_edit(e, "Change light color");
                row("Intensity");
                if (ImGui::DragFloat("##intensity", &l.intensity, 0.05f, 0.0f, 1000.0f, "%.2f")) world->add<render::Light>(e, l);
                track_edit(e, "Change light intensity");
                if (l.type == render::LightType::Point) {
                    row("Range");
                    if (ImGui::DragFloat("##range", &l.range, 0.1f, 0.1f, 1000.0f, "%.1f m")) world->add<render::Light>(e, l);
                    track_edit(e, "Change light range");
                } else {
                    row("Shadows");
                    if (ImGui::Checkbox("##shadows", &l.cast_shadows)) {
                        const EntityState before = capture(*world, e);
                        world->add<render::Light>(e, l);
                        record(make_edit_command("Toggle shadows", before, capture(*world, e)));
                    }
                    ImGui::TextDisabled("Rotate to aim. The first directional light casts shadows.");
                }
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<render::Light>(e);
                record(make_edit_command("Remove Light", before, capture(*world, e)));
            }
        }

        if (world->has<render::Environment>(e)) {
            bool remove = false;
            if (component_header("Environment", true, remove) && !remove) {
                ImGui::TextWrapped("Sky, sky light, exposure and shadows for the whole level.");
                if (ImGui::Button("Edit in World Settings")) {
                    show_world = true;
                    ImGui::SetWindowFocus(kWorldSettings);
                }
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<render::Environment>(e);
                record(make_edit_command("Remove Environment", before, capture(*world, e)));
            }
        }

        // Spin
        if (world->has<Spin>(e)) {
            bool remove = false;
            if (component_header("Spin", true, remove) && !remove) {
                Spin s = *world->get<Spin>(e);
                bool changed = false;
                if (ImGui::BeginTable("spin", 2, tflags)) {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                    changed |= vec3_row("Axis", s.axis, 0.01f, "%.2f", {0, 1, 0});
                    if (changed) world->add<Spin>(e, s);
                    commit_row(e, "Edit Spin axis", frame_before);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Speed");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    float deg = s.speed * 180.0f / math::kPi;
                    if (ImGui::DragFloat("##speed", &deg, 1.0f, -2000, 2000, "%.0f deg/s")) {
                        s.speed = deg * math::kPi / 180.0f;
                        world->add<Spin>(e, s);
                    }
                    track_edit(e, "Edit Spin speed");
                    ImGui::EndTable();
                }
                ImGui::TextDisabled("Rotates while playing (Alt+P).");
            }
            if (remove) {
                const EntityState before = capture(*world, e);
                world->remove<Spin>(e);
                record(make_edit_command("Remove Spin", before, capture(*world, e)));
            }
        }

        // Other components, read-only
        std::vector<std::string> others;
        for (const world::ComponentId id : world->components_of(e)) {
            const auto& info = world->component_info(id);
            if (info.name == "Name" || info.name == "Transform" || info.name == "MeshInstance" || info.name == "Spin" ||
                info.name == "Light" || info.name == "Environment" || info.name == "RigidBody" || info.name == "Collider" ||
                info.name == "GlobalTransform")
                continue;
            others.push_back(info.name);
        }
        if (!others.empty() && ImGui::CollapsingHeader("Other Components")) {
            for (const auto& n : others) ImGui::BulletText("%s", n.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("+ Add Component", ImVec2(w, 0))) ImGui::OpenPopup("add_component");
        if (ImGui::BeginPopup("add_component")) {
            const EntityState before = capture(*world, e);
            if (ImGui::MenuItem("Static Mesh (Cube)", nullptr, false, !world->has<render::MeshInstance>(e))) {
                world->add<render::MeshInstance>(e, render::MeshInstance{cube, {0.75f, 0.75f, 0.78f}});
                record(make_edit_command("Add Static Mesh", before, capture(*world, e)));
            }
            if (ImGui::MenuItem("Collider (fit to mesh)", nullptr, false, !world->has<simulation::Collider>(e))) {
                world->add<simulation::Collider>(e, fitted_collider(e, simulation::ShapeType::Box));
                record(make_edit_command("Add Collider", before, capture(*world, e)));
            }
            if (ImGui::MenuItem("Rigid Body", nullptr, false, !world->has<simulation::RigidBody>(e))) {
                if (!world->has<simulation::Collider>(e))
                    world->add<simulation::Collider>(e, fitted_collider(e, simulation::ShapeType::Box));
                world->add<simulation::RigidBody>(e);
                record(make_edit_command("Add Rigid Body", before, capture(*world, e)));
            }
            for (const auto type : {render::LightType::Point, render::LightType::Directional}) {
                const char* label = type == render::LightType::Point ? "Point Light" : "Directional Light";
                if (ImGui::MenuItem(label, nullptr, false, !world->has<render::Light>(e))) {
                    world->add<render::Light>(e, default_light(type));
                    record(make_edit_command(std::string("Add ") + label, before, capture(*world, e)));
                }
            }
            if (ImGui::MenuItem("Spin", nullptr, false, !world->has<Spin>(e))) {
                world->add<Spin>(e, Spin{});
                record(make_edit_command("Add Spin", before, capture(*world, e)));
            }
            if (ImGui::MenuItem("Transform", nullptr, false, !world->has<world::Transform>(e))) {
                world->add<world::Transform>(e);
                record(make_edit_command("Add Transform", before, capture(*world, e)));
            }
            ImGui::EndPopup();
        }
        ImGui::End();
    }

    void world_settings_panel() {
        if (!ImGui::Begin(kWorldSettings, &show_world)) {
            ImGui::End();
            return;
        }
        const world::Entity e = environment_entity();
        if (e.is_null()) {
            ImGui::TextWrapped("This level has no Environment actor, so the editor's default sky is shown and nothing "
                               "here is saved with the level.");
            if (ImGui::Button("Add Sky & Environment to the level")) add_environment();
        } else {
            render::Environment env = *world->get<render::Environment>(e);
            const auto row = [](const char* label) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", label);
                ImGui::SameLine(110);
                ImGui::SetNextItemWidth(-1);
            };
            // Sliders and colour pickers: one undo step per drag. Checkboxes and buttons: at once.
            const auto dragged = [&](bool changed, const char* label) {
                if (changed) world->add<render::Environment>(e, env);
                track_edit(e, label);
            };
            const auto toggled = [&](bool changed, const char* label) {
                if (!changed) return;
                const EntityState before = capture(*world, e);
                world->add<render::Environment>(e, env);
                record(make_edit_command(label, before, capture(*world, e)));
            };
            ImGui::SeparatorText("Sky");
            row("Zenith");
            dragged(ImGui::ColorEdit3("##sky", &env.sky_color.x), "Change sky colour");
            row("Horizon");
            dragged(ImGui::ColorEdit3("##horizon", &env.horizon_color.x), "Change horizon colour");
            row("Ground");
            dragged(ImGui::ColorEdit3("##ground", &env.ground_color.x), "Change ground colour");
            row("Brightness");
            dragged(ImGui::SliderFloat("##ambient", &env.ambient_intensity, 0.0f, 4.0f, "%.2f"), "Change sky brightness");
            row("Show sky");
            toggled(ImGui::Checkbox("##skyvisible", &env.sky_visible), "Toggle sky");
            ImGui::SeparatorText("Camera");
            row("Exposure");
            dragged(ImGui::SliderFloat("##exposure", &env.exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic),
                    "Change exposure");
            ImGui::SeparatorText("Shadows");
            row("Sun shadows");
            toggled(ImGui::Checkbox("##shadows", &env.shadows), "Toggle sun shadows");
            ImGui::Spacing();
            if (ImGui::Button("Reset to defaults")) {
                env = environment;
                toggled(true, "Reset environment");
            }
            ImGui::TextDisabled("Saved with the level (the \"%s\" actor).", display_name(*world, e).c_str());
        }
        if (world->count<render::Light>() == 0)
            ImGui::TextWrapped("This level has no lights, so a default sun is used. Add one from Place Actors > Lights.");
        ImGui::End();
    }

    // ---- Place Actors, Log, Stats ---------------------------------------

    void place_panel() {
        if (!ImGui::Begin(kPlace, &show_place)) {
            ImGui::End();
            return;
        }
        ImGui::TextDisabled("Basic");
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Cube", ImVec2(w, 0))) spawn("Cube", true, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Static mesh cube at the view focus");
        if (ImGui::Button("Spinning Cube", ImVec2(w, 0))) spawn("Spinning Cube", true, true, {0.35f, 0.65f, 1.0f});
        if (ImGui::Button("Sphere", ImVec2(w, 0))) spawn("Sphere", true, false, {0.75f, 0.75f, 0.78f}, library->sphere());
        if (ImGui::Button("Empty Actor", ImVec2(w, 0))) spawn("Empty", false, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Transform only; use as a group or pivot");
        ImGui::Spacing();
        ImGui::TextDisabled("Physics");
        if (ImGui::Button("Physics Cube", ImVec2(w, 0))) spawn_physics(false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("A cube with a collider and a rigid body: press Play and it falls. Placed 3 m above the view focus");
        if (ImGui::Button("Physics Sphere", ImVec2(w, 0))) spawn_physics(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("A bouncy ball. Add a Collider to the ground so it has something to land on");
        ImGui::Spacing();
        ImGui::TextDisabled("Lights");
        if (ImGui::Button("Directional Light", ImVec2(w, 0))) spawn_light(render::LightType::Directional);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Sunlight: parallel rays along the light's forward axis; casts shadows");
        if (ImGui::Button("Point Light", ImVec2(w, 0))) spawn_light(render::LightType::Point);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Light bulb: shines in all directions up to its range");
        if (ImGui::Button("Sky & Environment", ImVec2(w, 0))) {
            if (environment_entity().is_null()) add_environment();
            select(environment_entity());
            show_world = true;
            ImGui::SetWindowFocus(kWorldSettings);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Sky colours, sky light, exposure and shadows; saved with the level. Edit in World Settings");
        ImGui::Spacing();
        ImGui::TextDisabled("Levels");
        if (ImGui::Button("Open Demo Level", ImVec2(w, 0))) request(Pending::Demo);
        if (ImGui::Button("Open Level...", ImVec2(w, 0))) request(Pending::Open);
        ImGui::End();
    }

    // "builtin:cube" -> "Cube"; "asset:models/crate.glb#mesh0.0" -> "crate.glb #mesh0.0".
    static std::string asset_label(const std::optional<std::string>& name) {
        if (!name) return "(unknown)";
        if (*name == render::kBuiltinCube) return "Cube";
        if (*name == render::kBuiltinSphere) return "Sphere";
        constexpr std::string_view prefix = "asset:";
        if (name->rfind(prefix, 0) != 0) return *name;
        std::string rest = name->substr(prefix.size());
        const std::size_t hash = rest.find('#');
        const std::string file = to_utf8(fs::path(rest.substr(0, hash)).filename());
        return hash == std::string::npos ? file : file + " " + rest.substr(hash);
    }

    void content_panel() {
        // Layouts saved by older versions lack this panel: tab it with the log.
        if (const ImGuiWindow* log_window = ImGui::FindWindowByName(kLog); log_window && log_window->DockId)
            ImGui::SetNextWindowDockID(log_window->DockId, ImGuiCond_FirstUseEver);
        // Select the tab once the reset layout's windows exist.
        // Only the focused window's tab is brought forward, so one per frame:
        // Details (shares a dock with World Settings), then this panel.
        if (focus_content == 2) ImGui::SetWindowFocus(kDetails);
        if (focus_content > 0 && --focus_content == 0) ImGui::SetWindowFocus(kContent);
        if (!ImGui::Begin(kContent, &show_content)) {
            ImGui::End();
            return;
        }
        if (!content_scanned) {
            content_models = library->list_models();
            content_scanned = true;
        }
        if (ImGui::Button("Refresh")) {
            library->refresh();
            content_models = library->list_models();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Rescan the content folder");
        ImGui::SameLine();
        content_filter.Draw("##content_filter", 180);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", to_utf8(library->content_root()).c_str());
        ImGui::Separator();

        if (content_models.empty()) {
            ImGui::TextWrapped("No models found. Put .gltf or .glb files in the content folder (start the editor with "
                               "--content <folder>) and press Refresh.");
            ImGui::End();
            return;
        }

        const float tile = 96.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (tile + spacing)));
        int column = 0;
        for (const auto& path : content_models) {
            if (!content_filter.PassFilter(path.c_str())) continue;
            const fs::path p(path);
            const std::string stem = to_utf8(p.stem());
            ImGui::PushID(path.c_str());
            if (column > 0) ImGui::SameLine();
            ImGui::BeginGroup();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool chosen = content_selected == path;
            if (ImGui::Selectable("##tile", chosen, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tile, tile)))
                content_selected = path;
            const bool activate = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(kAssetDrag, path.data(), path.size());
                ImGui::Text("Place %s", stem.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                ImGui::SetTooltip("%s\nDouble-click: add at the view focus\nDrag into the viewport: place on the ground",
                                  path.c_str());
            // Tile art: a model badge with the file type.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a{pos.x + 14, pos.y + 8}, b{pos.x + tile - 14, pos.y + tile - 30};
            dl->AddRectFilled(a, b, IM_COL32(52, 70, 96, 255), 6.0f);
            dl->AddRect(a, b, IM_COL32(96, 132, 180, 255), 6.0f, 0, 1.5f);
            const std::string ext = to_utf8(p.extension());
            const ImVec2 es = ImGui::CalcTextSize(ext.c_str());
            dl->AddText({(a.x + b.x - es.x) * 0.5f, (a.y + b.y - es.y) * 0.5f}, IM_COL32(220, 230, 245, 255), ext.c_str());
            std::string label = stem;
            while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > tile - 6) label.resize(label.size() - 1);
            if (label.size() < stem.size()) label += "..";
            const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
            dl->AddText({pos.x + (tile - ls.x) * 0.5f, pos.y + tile - 24}, IM_COL32(225, 225, 230, 255), label.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            if (activate) spawn_model(path, cam.focus);
            column = (column + 1) % columns;
        }
        ImGui::End();
    }

    void log_panel() {
        if (!ImGui::Begin(kLog, &show_log)) {
            ImGui::End();
            return;
        }
        ImGui::Checkbox("Info", &log_info);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &log_warn);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &log_error);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &log_autoscroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) log->clear();
        ImGui::Separator();
        if (ImGui::BeginChild("log_lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : log->entries()) {
                const bool err = entry.level >= LogLevel::Error, warn = entry.level == LogLevel::Warn;
                if ((err && !log_error) || (warn && !log_warn) || (!err && !warn && !log_info)) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, level_color(entry.level));
                ImGui::Text("[%9.3f] %-6s %-8s %s", static_cast<double>(entry.timestamp_ns) * 1e-9,
                            std::string(to_string(entry.level)).c_str(), entry.module.c_str(), entry.message.c_str());
                ImGui::PopStyleColor();
            }
            if (log_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void stats_panel() {
        if (!ImGui::Begin(kStats, &show_stats)) {
            ImGui::End();
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Frame: %.2f ms (%.0f fps)", 1000.0f / std::max(1.0f, io.Framerate), io.Framerate);
        float max_ms = 1.0f;
        for (const float v : frame_ms) max_ms = std::max(max_ms, v);
        ImGui::PlotLines("##frametimes", frame_ms.data(), static_cast<int>(frame_ms.size()),
                         static_cast<int>(frame_ms_pos % frame_ms.size()), "frame time (ms)", 0.0f, max_ms * 1.1f,
                         ImVec2(-1, 60));
        if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            const auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            };
            row("GPU", renderer.gpu().name + (renderer.gpu().software ? " (software)" : ""));
            row("API", renderer.gpu().api + (renderer.gpu().validation ? ", validation" : ""));
            row("Viewport", std::to_string(renderer.viewport_width()) + " x " + std::to_string(renderer.viewport_height()));
            row("Draw calls", std::to_string(last_stats.draws) + " (+" + std::to_string(last_stats.shadow_draws) + " shadow)");
            row("Point lights", std::to_string(last_stats.point_lights));
            row("Sun shadows", last_stats.sun_shadows ? "on" : "off");
            row("Physics bodies", physics ? std::to_string(physics_stats.bodies) + " (" + std::to_string(physics_stats.active_bodies) + " awake)"
                                           : std::string("not playing"));
            row("Entities", std::to_string(world->entity_count()));
            row("Archetypes", std::to_string(world->archetype_count()));
            row("Component types", std::to_string(world->component_type_count()));
            row("Undo steps", std::to_string(undo.size()));
            ImGui::EndTable();
        }
        ImGui::End();
    }
};

EditorApp::EditorApp(render::Renderer& renderer, Logger& logger, std::shared_ptr<MemorySink> log,
                     std::filesystem::path content_root)
    : impl_(std::make_unique<Impl>(renderer, logger, std::move(log), std::move(content_root))) {
    impl_->load_demo();
}

EditorApp::~EditorApp() = default;

void EditorApp::update(float dt) { impl_->update(dt); }
void EditorApp::draw(float dt) { impl_->draw(dt); }
world::World& EditorApp::world() { return *impl_->world; }
render::Camera EditorApp::camera() const { return impl_->cam.to_render_camera(); }
void EditorApp::set_frame_stats(const render::FrameStats& stats) { impl_->last_stats = stats; }
bool EditorApp::quit_requested() const { return impl_->quit; }
void EditorApp::request_quit() { impl_->request(Impl::Pending::Quit); }
void EditorApp::open_level(const std::filesystem::path& path) { impl_->request(Impl::Pending::Open, path); }
std::string EditorApp::window_title() const {
    return "Aether Editor - " + impl_->level_name() + (impl_->dirty() ? " *" : "");
}
void EditorApp::new_level() { impl_->new_level(); }
void EditorApp::load_demo_level() { impl_->load_demo(); }

render::RenderOptions EditorApp::render_options() const {
    render::RenderOptions o;
    o.highlight = impl_->selection();
    o.grid = impl_->show_grid;
    o.environment = impl_->environment;
    return o;
}

}  // namespace aether::editor
