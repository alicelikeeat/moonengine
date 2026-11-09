#include "aether/world/schedule.hpp"

#include <algorithm>

namespace aether::world {

void SystemAccess::insert(std::vector<ComponentId>& set, ComponentId id) {
    const auto it = std::lower_bound(set.begin(), set.end(), id);
    if (it == set.end() || *it != id) set.insert(it, id);
}

bool SystemAccess::reads(ComponentId id) const noexcept {
    return std::binary_search(reads_.begin(), reads_.end(), id);
}

bool SystemAccess::writes(ComponentId id) const noexcept {
    return std::binary_search(writes_.begin(), writes_.end(), id);
}

namespace {
bool intersects(const std::vector<ComponentId>& a, const std::vector<ComponentId>& b) {
    auto i = a.begin();
    auto j = b.begin();
    while (i != a.end() && j != b.end()) {
        if (*i == *j) return true;
        if (*i < *j) ++i; else ++j;
    }
    return false;
}
}  // namespace

bool SystemAccess::conflicts_with(const SystemAccess& other) const noexcept {
    if (exclusive_ || other.exclusive_) return true;
    return intersects(writes_, other.writes_) || intersects(writes_, other.reads_) ||
           intersects(other.writes_, reads_);
}

void Schedule::add_system(std::string name, SystemAccess access, SystemFn fn) {
    AETHER_ASSERT(static_cast<bool>(fn), "system function must not be empty");
    systems_.push_back(System{std::move(name), std::move(access), std::move(fn)});
    dirty_ = true;
}

void Schedule::rebuild_waves() const {
    // A system goes one wave after the latest earlier system it conflicts
    // with, which preserves registration order between conflicting systems.
    std::vector<std::size_t> level(systems_.size(), 0);
    std::size_t max_level = 0;
    for (std::size_t j = 0; j < systems_.size(); ++j) {
        for (std::size_t i = 0; i < j; ++i)
            if (systems_[i].access.conflicts_with(systems_[j].access))
                level[j] = std::max(level[j], level[i] + 1);
        max_level = std::max(max_level, level[j]);
    }
    waves_.assign(systems_.empty() ? 0 : max_level + 1, {});
    for (std::size_t j = 0; j < systems_.size(); ++j) waves_[level[j]].push_back(j);
    dirty_ = false;
}

std::vector<std::vector<std::string>> Schedule::plan() const {
    if (dirty_) rebuild_waves();
    std::vector<std::vector<std::string>> out;
    for (const auto& wave : waves_) {
        auto& names = out.emplace_back();
        for (const std::size_t i : wave) names.push_back(systems_[i].name);
    }
    return out;
}

void Schedule::run(World& world, JobSystem* jobs, double dt) {
    if (dirty_) rebuild_waves();
    std::vector<CommandBuffer> buffers(systems_.size());

    for (const auto& wave : waves_) {
        if (jobs && wave.size() > 1) {
            WaitGroup group;
            for (const std::size_t i : wave) {
                jobs->submit([this, &world, &buffers, i, dt] {
                    SystemContext ctx(world, systems_[i].access, buffers[i], dt);
                    systems_[i].fn(ctx);
                }, &group);
            }
            jobs->wait(group);
        } else {
            for (const std::size_t i : wave) {
                SystemContext ctx(world, systems_[i].access, buffers[i], dt);
                systems_[i].fn(ctx);
            }
        }
    }

    for (auto& buffer : buffers) buffer.apply(world);
}

}  // namespace aether::world
