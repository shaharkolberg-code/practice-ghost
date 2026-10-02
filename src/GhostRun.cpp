#include "GhostRun.hpp"

#include <array>
#include <fstream>
#include <functional>
#include <string>
#include <type_traits>

using namespace geode::prelude;

namespace {
    constexpr std::array<char, 4> kMagic = {'P', 'G', 'H', '1'};

    static_assert(std::is_trivially_copyable_v<GhostFrame>);

    template <class T>
    void writeRaw(std::ofstream& out, T const& value) {
        out.write(reinterpret_cast<char const*>(&value), sizeof(T));
    }

    template <class T>
    bool readRaw(std::ifstream& in, T& value) {
        return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
    }
}

bool GhostRun::isBetterThan(GhostRun const& other) const {
    if (completed != other.completed) return completed;
    if (completed) return duration < other.duration;
    return maxX > other.maxX;
}

bool GhostRun::save(std::filesystem::path const& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    writeRaw(out, kMagic);
    writeRaw(out, static_cast<uint8_t>(completed));
    writeRaw(out, maxX);
    writeRaw(out, duration);
    writeRaw(out, static_cast<uint32_t>(frames.size()));
    out.write(reinterpret_cast<char const*>(frames.data()), frames.size() * sizeof(GhostFrame));
    return static_cast<bool>(out);
}

std::optional<GhostRun> GhostRun::load(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    std::array<char, 4> magic{};
    uint8_t completed = 0;
    uint32_t count = 0;
    GhostRun run;
    if (!readRaw(in, magic) || magic != kMagic) return std::nullopt;
    if (!readRaw(in, completed) || !readRaw(in, run.maxX) || !readRaw(in, run.duration) || !readRaw(in, count)) {
        return std::nullopt;
    }
    // ~1 hour at 120 Hz; anything bigger is a corrupt file
    if (count == 0 || count > 500'000) return std::nullopt;

    run.completed = completed != 0;
    run.frames.resize(count);
    if (!in.read(reinterpret_cast<char*>(run.frames.data()), count * sizeof(GhostFrame))) {
        return std::nullopt;
    }
    return run;
}

std::filesystem::path ghostPathFor(GJGameLevel* level) {
    // Level type is part of the key so main levels, online levels and
    // local levels with colliding IDs don't share a ghost.
    int type = static_cast<int>(level->m_levelType);
    int id = level->m_levelID.value();
    std::string key = id > 0
        ? fmt::format("t{}-{}", type, id)
        : fmt::format("t{}-local-{}", type, std::hash<std::string>{}(std::string(level->m_levelName.c_str())));
    return Mod::get()->getSaveDir() / "ghosts" / (key + ".bin");
}
