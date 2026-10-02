#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

// One sampled snapshot of player 1.
struct GhostFrame {
    double time;      // m_gameState.m_levelTime at sample
    float x, y;       // position in the player's parent node
    float rotation;
    float scale;
    uint8_t icon;     // IconType
    uint8_t flags;    // bit 0: upside down
};

struct GhostRun {
    bool completed = false;
    float maxX = 0.f;
    double duration = 0.0;
    std::vector<GhostFrame> frames;

    // A completed run beats any death; faster completions beat slower ones;
    // otherwise whoever got further wins.
    bool isBetterThan(GhostRun const& other) const;

    bool save(std::filesystem::path const& path) const;
    static std::optional<GhostRun> load(std::filesystem::path const& path);
};

std::filesystem::path ghostPathFor(GJGameLevel* level);
