#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>

#include "GhostRun.hpp"

using namespace geode::prelude;

namespace {
    constexpr double kSampleInterval = 1.0 / 120.0;

    IconType iconTypeFor(PlayerObject* p) {
        if (p->m_isShip) return p->m_isPlatformer ? IconType::Jetpack : IconType::Ship;
        if (p->m_isBird) return IconType::Ufo;
        if (p->m_isBall) return IconType::Ball;
        if (p->m_isDart) return IconType::Wave;
        if (p->m_isRobot) return IconType::Robot;
        if (p->m_isSpider) return IconType::Spider;
        if (p->m_isSwing) return IconType::Swing;
        return IconType::Cube;
    }

    // Interpolate along the shortest arc so 359 -> 1 doesn't spin backwards.
    float lerpAngle(float a, float b, float t) {
        float diff = std::fmod(b - a + 540.f, 360.f) - 180.f;
        return a + diff * t;
    }
}

class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        std::filesystem::path m_path;
        std::optional<GhostRun> m_best;
        GhostRun m_current;
        bool m_recording = false;
        bool m_finished = false;
        double m_lastSample = -1.0;
        size_t m_playhead = 0;
        SimplePlayer* m_ghost = nullptr; // owned by the node tree
        IconType m_ghostIcon = IconType::Cube;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->m_path = ghostPathFor(level);
        m_fields->m_best = GhostRun::load(m_fields->m_path);
        this->createGhost();
        this->beginAttempt();
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        this->beginAttempt();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        double time = m_gameState.m_levelTime;
        this->recordFrame(time);
        this->updateGhost(time);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        // Noclip and the anticheat spike call this without actually killing.
        if (player && player->m_isDead) this->finishRun(false);
    }

    void levelComplete() {
        this->finishRun(true);
        PlayLayer::levelComplete();
    }

    void createGhost() {
        auto fields = m_fields.self();
        if (fields->m_ghost || !m_player1) return;

        auto ghost = SimplePlayer::create(GameManager::get()->activeIconForType(IconType::Cube));
        ghost->setID("ghost"_spr);
        ghost->setVisible(false);

        // Live next to the player so we share its coordinate space. A batch
        // node can't hold a SimplePlayer, so fall back to the object layer.
        CCNode* parent = m_player1->getParent();
        if (!parent || typeinfo_cast<CCSpriteBatchNode*>(parent)) parent = m_objectLayer;
        parent->addChild(ghost, m_player1->getZOrder() - 1);

        fields->m_ghost = ghost;
        fields->m_ghostIcon = IconType::Cube;
    }

    void styleGhost() {
        auto ghost = m_fields->m_ghost;
        if (!ghost) return;
        auto color = Mod::get()->getSettingValue<ccColor3B>("ghost-color");
        auto opacity = Mod::get()->getSettingValue<int64_t>("opacity");
        ghost->setColors(color, color);
        ghost->setOpacity(static_cast<GLubyte>(std::clamp<int64_t>(opacity, 0, 255)));
    }

    void beginAttempt() {
        auto fields = m_fields.self();
        // Only full normal-mode runs are comparable: practice checkpoints and
        // start positions put the player out of sync with the level timer.
        fields->m_recording = !m_isPracticeMode && !m_isTestMode && !m_startPosObject;
        fields->m_finished = false;
        fields->m_current = GhostRun{};
        fields->m_lastSample = -1.0;
        fields->m_playhead = 0;

        if (!fields->m_ghost) return;
        bool show = fields->m_recording
            && fields->m_best
            && Mod::get()->getSettingValue<bool>("enabled");
        fields->m_ghost->setVisible(show);
        if (show) this->styleGhost();
    }

    GhostFrame sampleFrame(double time) {
        uint8_t flags = m_player1->m_isUpsideDown ? 1 : 0;
        return GhostFrame{
            .time = time,
            .x = m_player1->getPositionX(),
            .y = m_player1->getPositionY(),
            .rotation = m_player1->getRotation(),
            .scale = m_player1->m_vehicleSize,
            .icon = static_cast<uint8_t>(iconTypeFor(m_player1)),
            .flags = flags,
        };
    }

    void recordFrame(double time) {
        auto fields = m_fields.self();
        if (!fields->m_recording || fields->m_finished || !m_player1) return;
        if (fields->m_lastSample >= 0.0 && time - fields->m_lastSample < kSampleInterval) return;

        fields->m_lastSample = time;
        fields->m_current.frames.push_back(this->sampleFrame(time));
        fields->m_current.maxX = std::max(fields->m_current.maxX, m_player1->getPositionX());
    }

    void updateGhost(double time) {
        auto fields = m_fields.self();
        auto ghost = fields->m_ghost;
        if (!ghost || !ghost->isVisible() || !fields->m_best) return;

        auto const& run = *fields->m_best;
        auto const& frames = run.frames;
        if (time > run.duration) {
            // The best run ended here (death or finish line).
            ghost->setVisible(false);
            return;
        }

        auto& i = fields->m_playhead;
        while (i + 1 < frames.size() && frames[i + 1].time <= time) ++i;

        auto const& a = frames[i];
        auto const& b = i + 1 < frames.size() ? frames[i + 1] : a;
        float k = b.time > a.time
            ? std::clamp(static_cast<float>((time - a.time) / (b.time - a.time)), 0.f, 1.f)
            : 0.f;

        auto icon = static_cast<IconType>(a.icon);
        if (icon != fields->m_ghostIcon) {
            ghost->updatePlayerFrame(GameManager::get()->activeIconForType(icon), icon);
            fields->m_ghostIcon = icon;
            this->styleGhost();
        }

        ghost->setPosition({std::lerp(a.x, b.x, k), std::lerp(a.y, b.y, k)});
        ghost->setRotation(lerpAngle(a.rotation, b.rotation, k));
        ghost->setScaleX(a.scale);
        ghost->setScaleY((a.flags & 1) ? -a.scale : a.scale);
    }

    void finishRun(bool completed) {
        auto fields = m_fields.self();
        if (!fields->m_recording || fields->m_finished || !m_player1) return;
        fields->m_finished = true;

        double time = m_gameState.m_levelTime;
        auto& run = fields->m_current;
        run.frames.push_back(this->sampleFrame(time));
        run.maxX = std::max(run.maxX, m_player1->getPositionX());
        run.completed = completed;
        run.duration = time;

        if (fields->m_best && !run.isBetterThan(*fields->m_best)) return;

        if (!run.save(fields->m_path)) {
            log::warn("Failed to save ghost to {}", fields->m_path.string());
        }
        fields->m_best = std::move(run);
        fields->m_current = GhostRun{};
        // The playhead indexes the old run; park the ghost until the next attempt.
        if (fields->m_ghost) fields->m_ghost->setVisible(false);
    }

    void clearGhost() {
        auto fields = m_fields.self();
        std::error_code ec;
        std::filesystem::remove(fields->m_path, ec);
        fields->m_best.reset();
        if (fields->m_ghost) fields->m_ghost->setVisible(false);
    }
};

class $modify(GhostPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) return;

        auto sprite = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
        sprite->setScale(0.6f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(GhostPauseLayer::onClearGhost)
        );
        button->setID("clear-ghost-button"_spr);
        menu->addChild(button);
        menu->updateLayout();
    }

    void onClearGhost(CCObject*) {
        createQuickPopup(
            "Practice Ghost",
            "Delete the saved ghost for this level?",
            "Cancel", "Delete",
            [](FLAlertLayer*, bool confirmed) {
                if (!confirmed) return;
                if (auto playLayer = PlayLayer::get()) {
                    static_cast<GhostPlayLayer*>(playLayer)->clearGhost();
                }
            }
        );
    }
};
