#include "game/oot/logic/Reach.h"

#include <unordered_set>

#include "game/oot/logic/Evaluator.h"
#include "game/oot/logic/RuleParser.h"

namespace tpt::game::oot::logic {

namespace {

// Generator-only shortcut regions. OoTR's world graph includes direct
// `Root` exits to these so item placement can put vanilla-fixed items
// at locations the BFS wouldn't otherwise reach without specific
// settings (skip_child_zelda, free Light Medallion, no hideout shuffle,
// etc.). For player-facing reach, the actual path is via normal
// gameplay — walking to HC, ToT, GF — so we filter them out here.
//
// When OoTR settings-string parsing lands, we can re-enable these
// conditionally (e.g. show HC Garden Skippable when skip_child_zelda
// is actually on for the loaded seed). For now: unconditional skip.
const std::unordered_set<std::string>& generatorOnlyRegions() {
    static const std::unordered_set<std::string> kShortcuts{
        "HC Garden Skippable Locations",
        "Beyond Door of Time Skippable Locations",
        "GF Above Jail Child Locations",
    };
    return kShortcuts;
}

std::vector<CompiledEdge> compileEdges(const std::vector<Edge>& src) {
    std::vector<CompiledEdge> out;
    out.reserve(src.size());
    for (const auto& e : src) {
        CompiledEdge ce;
        ce.target = e.target;
        if (!e.rule.empty()) {
            ce.rule = parseRule(e.rule);  // bubbles RuleParseError up
        }
        out.push_back(std::move(ce));
    }
    return out;
}

// An edge with no parsed rule (empty rule string) is treated as
// unconditionally true. Same semantics as OoTR — a missing rule means
// "always accessible".
bool edgePasses(const Evaluator& ev, const CompiledEdge& e) {
    if (!e.rule) return true;
    return ev.evaluate(*e.rule);
}

}  // namespace

CompiledRegionMap compileWorld(const RegionMap& src) {
    CompiledRegionMap out;
    out.reserve(src.size());
    for (const auto& [name, r] : src) {
        CompiledRegion cr;
        cr.name        = r.name;
        cr.dungeon     = r.dungeon;
        cr.timePasses  = r.timePasses;
        cr.exits       = compileEdges(r.exits);
        cr.locations   = compileEdges(r.locations);
        cr.events      = compileEdges(r.events);
        out.emplace(name, std::move(cr));
    }
    return out;
}

std::unordered_set<std::string>
reach(const CompiledRegionMap& world,
      const AliasTable& aliases,
      Context& ctx,
      const std::string& startRegion) {

    ctx.reachedRegions.insert(startRegion);

    // BFS-to-fixpoint: every iteration tries every exit and every event
    // of every reached region. When something new flips (new region or
    // new event), we loop again because that new state may unlock more
    // edges elsewhere.
    const std::size_t kMaxRounds = world.size() + 256;
    for (std::size_t round = 0; round < kMaxRounds; ++round) {
        const Evaluator ev(ctx, aliases);
        bool changed = false;

        for (const auto& [name, region] : world) {
            if (!ctx.reachedRegions.count(name)) continue;
            for (const auto& exit : region.exits) {
                if (ctx.reachedRegions.count(exit.target)) continue;
                if (generatorOnlyRegions().count(exit.target)) continue;
                if (edgePasses(ev, exit)) {
                    ctx.reachedRegions.insert(exit.target);
                    changed = true;
                }
            }
            for (const auto& evt : region.events) {
                if (ctx.events.count(evt.target)) continue;
                if (edgePasses(ev, evt)) {
                    ctx.events.insert(evt.target);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }

    // Final pass: collect locations.
    std::unordered_set<std::string> result;
    const Evaluator ev(ctx, aliases);
    for (const auto& [name, region] : world) {
        if (!ctx.reachedRegions.count(name)) continue;
        for (const auto& loc : region.locations) {
            if (edgePasses(ev, loc)) {
                result.insert(loc.target);
            }
        }
    }
    return result;
}

}  // namespace tpt::game::oot::logic
