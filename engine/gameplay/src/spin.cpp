#include "aether/gameplay/spin.hpp"

#include <cmath>

#include "aether/world/hierarchy.hpp"

namespace aether::gameplay {

void register_components(world::SerializerRegistry& registry) {
    registry.add<Spin>(
        [](const Spin& s) {
            Json j = Json::object();
            j.set("axis", world::json_io::from_vec3(s.axis));
            j.set("speed", world::json_io::from_float(s.speed));
            return j;
        },
        [](const Json& j) -> Result<Spin> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "Spin must be an object"};
            auto axis = world::json_io::read_vec3(j, "axis", {0, 1, 0});
            if (!axis) return axis.error();
            auto speed = world::json_io::read_number(j, "speed", 1.0);
            if (!speed) return speed.error();
            if (std::fabs(speed.value()) > 1e6) return Error{ErrorCode::InvalidArgument, "Spin speed out of range"};
            return Spin{axis.value(), static_cast<float>(speed.value())};
        });
}

void add_systems(world::Schedule& schedule, world::World& world) {
    schedule.add_system("spin", world::SystemAccess::of<const Spin, world::Transform>(world),
                        [](world::SystemContext& ctx) {
                            const float dt = static_cast<float>(ctx.dt);
                            ctx.each<const Spin, world::Transform>([dt](const Spin& s, world::Transform& t) {
                                if (s.speed == 0 || math::length(s.axis) == 0) return;
                                t.rotation = math::normalize(math::Quat::from_axis_angle(s.axis, s.speed * dt) * t.rotation);
                            });
                        });
}

}  // namespace aether::gameplay
