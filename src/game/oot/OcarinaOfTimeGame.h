#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "game/GameModule.h"
#include "game/oot/Checks.h"
#include "game/oot/logic/AliasTable.h"
#include "game/oot/logic/Reach.h"
#include "game/oot/save/Inventory.h"
#include "game/oot/save/PlayerData.h"
#include "game/oot/save/SaveFlags.h"
#include "game/oot/save/Xflags.h"

namespace tpt::game::oot {

// Stub GameModule for Ocarina of Time. Every method is currently a no-op
// returning empty / default-constructed values. The class exists so the
// project can prove that the GameModule interface accepts a second concrete
// implementation alongside TwilightPrincessGame, and so the rest of the
// scaffolding (file layout, namespace, build inclusion, prefs key) is in
// place for the real implementation to drop into.
//
// Not yet wired into src/main.cpp — instantiation comes when game selection
// lands. See doc/game-module-handoff.md §7 for the full plan; this file is
// step 1.
//
// The module takes a `tpt::ui::State&` only to write the status-bar fields
// the UI shell still reads directly from State (sourceName, emulatorHooked,
// saveLoaded) — same transitional shared-bag arrangement TwilightPrincessGame
// uses (see handoff §3). Everything else lives inside the module and is
// exposed via TrackerSnapshot. When the State split lands those status
// fields move onto TrackerSnapshot too and this reference goes away.
class OcarinaOfTimeGame final : public GameModule {
public:
    explicit OcarinaOfTimeGame(tpt::ui::State& state) : state_(state) {}

    std::string id()          const override { return "oot"; }
    std::string displayName() const override { return "Ocarina of Time"; }

    bool loadWorldData(std::ostream& errlog) override;
    void poll(tpt::memory::MemorySource& mem) override;

    const TrackerSnapshot& snapshot() const override { return snapshot_; }
    const std::vector<tpt::ui::FilterSpec>& filterSpecs() const override;

    void renderOptionsRow()  override;
    void renderRightPane()   override;
    void renderSettingsPane() override;

    // ---- Per-shuffle predicates ---------------------------------------------
    //
    // Each predicate returns whether a specific OoTR-extended shuffle is
    // active in the loaded seed. Today they're stubs returning `false` —
    // hides the corresponding check types until the user toggles
    // `overrideShowOotrShuffles_`. When OoTR settings-string parsing
    // lands, only these bodies need to change: e.g.
    //   `return ctx.settings["shuffle_pots"] != "off"`.
    //
    // Public so the future settings layer (or CLI debug commands) can
    // call them directly.
    bool isPotShuffleEnabled()         const;
    bool isCrateShuffleEnabled()       const;
    bool isFlyingPotShuffleEnabled()   const;
    bool isBeehiveShuffleEnabled()     const;
    bool isWonderitemShuffleEnabled()  const;
    bool isSilverRupeeShuffleEnabled() const;
    bool isFreestandingShuffleEnabled() const;
    bool isRupeeTowerShuffleEnabled()  const;
    bool isDropShuffleEnabled()        const;
    bool isGrottoScrubShuffleEnabled() const;
    bool isHintShuffleEnabled()        const;

    void           loadPrefs(const nlohmann::json& sub) override;
    nlohmann::json savePrefs() const                    override;

    std::unique_ptr<tpt::memory::MemorySource> defaultSource() const override;

private:
    tpt::ui::State& state_;
    TrackerSnapshot snapshot_;
    nlohmann::json  prefs_ = nlohmann::json::object();

    // Most-recently-fetched SaveContext header bytes (N64-native big-endian
    // after Project64Source's per-word byte-swap). Re-read every poll;
    // decoded into `playerData_` / `inventory_` / `saveFlags_` below.
    std::vector<std::uint8_t> saveBuf_;
    save::PlayerData          playerData_;
    save::Inventory           inventory_;
    save::SaveFlags           saveFlags_;
    save::XflagState          xflagState_;

    // Static check list loaded once from data/oot/locations.json (output of
    // tools/extract_oot_locations.py). Not mutated after load.
    std::vector<Check>        checks_;

    // Pre-parsed world graph + alias table. Built once during
    // loadWorldData; reused on every poll for reachability BFS.
    logic::CompiledRegionMap  world_;
    logic::AliasTable         aliases_;

    // Map OoTR location name → its containing region's name. Used to
    // bridge the "location reachable" output of the BFS back into our
    // check list (which is keyed by location name).
    std::unordered_map<std::string, std::string> locationToRegion_;

    // Master user override: force-show every OoTR-extended shuffle type
    // regardless of the per-shuffle predicates. Saved to prefs so the
    // user's preference persists across sessions. Goes away once the
    // per-shuffle predicates read real settings.
    bool overrideShowOotrShuffles_ = false;

    // True iff the check's type would be hidden under the active shuffle
    // configuration. Centralizes the type → predicate dispatch.
    bool shouldHideCheck(const Check& c) const;
};

}  // namespace tpt::game::oot
