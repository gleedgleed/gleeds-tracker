#pragma once

#include "game/GameModule.h"

namespace tpt::game::tp {

// TP implementation of GameModule. Owns the options-row + right-pane
// rendering. Lifecycle (loadWorldData, poll) still delegates to the
// free functions in ui/UIState.cpp for now; those will migrate in a
// later pass once the State has been split into game-private and
// UI-only halves.
class TwilightPrincessGame final : public GameModule {
public:
    explicit TwilightPrincessGame(tpt::ui::State& state) : state_(state) {}

    std::string id()          const override { return "tp"; }
    std::string displayName() const override { return "Twilight Princess"; }

    bool loadWorldData(std::ostream& errlog) override;
    void poll(tpt::memory::MemorySource& mem) override;

    const TrackerSnapshot& snapshot() const override { return snapshot_; }
    const std::vector<tpt::ui::FilterSpec>& filterSpecs() const override;

    void renderOptionsRow()  override;
    void renderRightPane()   override;
    void renderSettingsPane() override;

    void           loadPrefs(const nlohmann::json& sub) override;
    nlohmann::json savePrefs() const                    override;

    std::unique_ptr<tpt::memory::MemorySource> defaultSource() const override;

private:
    // Detailed rando-settings pane (the big collapsing section under the
    // inventory summary). Kept private because it reads several State
    // fields directly; long-term it'll move under renderSettingsPane()
    // when the UI shell starts splitting the right column into separate
    // panes again.
    void renderRandomizerSettingsPane();

    tpt::ui::State& state_;
    TrackerSnapshot snapshot_;
};

}  // namespace tpt::game::tp
