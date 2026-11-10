#include "aether/simulation/physics.hpp"

namespace aether::simulation {

namespace {

using world::json_io::from_float;
using world::json_io::from_vec3;
using world::json_io::read_number;
using world::json_io::read_string;
using world::json_io::read_vec3;

Result<float> read_range(const Json& j, std::string_view key, float fallback, double lo, double hi) {
    auto v = read_number(j, key, fallback);
    if (!v) return v.error();
    if (!(v.value() >= lo && v.value() <= hi))
        return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" is out of range"};
    return static_cast<float>(v.value());
}

}  // namespace

void register_components(world::SerializerRegistry& registry) {
    registry.add<RigidBody>(
        [](const RigidBody& rb) {
            Json j = Json::object();
            j.set("type", rb.type == BodyType::Static ? "static" : rb.type == BodyType::Kinematic ? "kinematic" : "dynamic");
            j.set("mass", from_float(rb.mass));
            j.set("friction", from_float(rb.friction));
            j.set("restitution", from_float(rb.restitution));
            j.set("linear_damping", from_float(rb.linear_damping));
            j.set("angular_damping", from_float(rb.angular_damping));
            j.set("gravity_scale", from_float(rb.gravity_scale));
            if (!(rb.linear_velocity == math::Vec3{})) j.set("linear_velocity", from_vec3(rb.linear_velocity));
            if (!(rb.angular_velocity == math::Vec3{})) j.set("angular_velocity", from_vec3(rb.angular_velocity));
            return j;
        },
        [](const Json& j) -> Result<RigidBody> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "RigidBody must be an object"};
            RigidBody rb;
            auto type = read_string(j, "type", "dynamic");
            if (!type) return type.error();
            if (type.value() == "static") rb.type = BodyType::Static;
            else if (type.value() == "dynamic") rb.type = BodyType::Dynamic;
            else if (type.value() == "kinematic") rb.type = BodyType::Kinematic;
            else return Error{ErrorCode::InvalidArgument, "unknown body type \"" + type.value() + "\""};
            const struct {
                const char* key;
                float* field;
                double lo, hi;
            } numbers[] = {{"mass", &rb.mass, 1e-3, 1e9},
                           {"friction", &rb.friction, 0, 10},
                           {"restitution", &rb.restitution, 0, 1},
                           {"linear_damping", &rb.linear_damping, 0, 1000},
                           {"angular_damping", &rb.angular_damping, 0, 1000},
                           {"gravity_scale", &rb.gravity_scale, -100, 100}};
            for (const auto& n : numbers) {
                auto v = read_range(j, n.key, *n.field, n.lo, n.hi);
                if (!v) return v.error();
                *n.field = v.value();
            }
            auto lv = read_vec3(j, "linear_velocity", rb.linear_velocity);
            if (!lv) return lv.error();
            rb.linear_velocity = lv.value();
            auto av = read_vec3(j, "angular_velocity", rb.angular_velocity);
            if (!av) return av.error();
            rb.angular_velocity = av.value();
            return rb;
        });

    registry.add<Collider>(
        [](const Collider& c) {
            Json j = Json::object();
            switch (c.shape) {
                case ShapeType::Sphere:
                    j.set("shape", "sphere");
                    j.set("radius", from_float(c.radius));
                    break;
                case ShapeType::Capsule:
                    j.set("shape", "capsule");
                    j.set("radius", from_float(c.radius));
                    j.set("half_height", from_float(c.half_height));
                    break;
                default:
                    j.set("shape", "box");
                    j.set("half_extents", from_vec3(c.half_extents));
                    break;
            }
            if (!(c.offset == math::Vec3{})) j.set("offset", from_vec3(c.offset));
            return j;
        },
        [](const Json& j) -> Result<Collider> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "Collider must be an object"};
            Collider c;
            auto shape = read_string(j, "shape", "box");
            if (!shape) return shape.error();
            if (shape.value() == "box") c.shape = ShapeType::Box;
            else if (shape.value() == "sphere") c.shape = ShapeType::Sphere;
            else if (shape.value() == "capsule") c.shape = ShapeType::Capsule;
            else return Error{ErrorCode::InvalidArgument, "unknown collider shape \"" + shape.value() + "\""};
            auto half = read_vec3(j, "half_extents", c.half_extents);
            if (!half) return half.error();
            if (!(half.value().x > 0 && half.value().y > 0 && half.value().z > 0))
                return Error{ErrorCode::InvalidArgument, "\"half_extents\" must be positive"};
            c.half_extents = half.value();
            auto radius = read_range(j, "radius", c.radius, 1e-4, 1e6);
            if (!radius) return radius.error();
            c.radius = radius.value();
            auto half_height = read_range(j, "half_height", c.half_height, 1e-4, 1e6);
            if (!half_height) return half_height.error();
            c.half_height = half_height.value();
            auto offset = read_vec3(j, "offset", c.offset);
            if (!offset) return offset.error();
            c.offset = offset.value();
            return c;
        });
}

}  // namespace aether::simulation
