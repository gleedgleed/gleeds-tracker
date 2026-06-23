#include "core/CheckSaveBindings.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/EventFlags.h"
#include "core/Items.h"
#include "core/QuestState.h"
#include "core/SaveOffsets.h"

namespace tpt::core {

namespace {

SaveBindingType typeFromString(std::string_view s) {
    if (s == "Region")  return SaveBindingType::Region;
    if (s == "Flag")    return SaveBindingType::Flag;
    if (s == "Event")   return SaveBindingType::Event;
    return SaveBindingType::Unknown;
}

void mergeBindingsJson(CheckSaveBindings& out, const nlohmann::json& j) {
    for (auto it = j.begin(); it != j.end(); ++it) {
        CheckSaveBinding e;
        e.name = it.key();
        e.type = typeFromString(it.value().value("type", "Unknown"));
        if (auto rIt = it.value().find("region"); rIt != it.value().end() && rIt->is_number_integer()) {
            e.region = static_cast<std::uint8_t>(rIt->get<int>());
        }
        if (auto oIt = it.value().find("offset"); oIt != it.value().end() && oIt->is_number_integer()) {
            e.offset = static_cast<std::uint16_t>(oIt->get<int>());
        }
        if (auto bIt = it.value().find("bit"); bIt != it.value().end() && bIt->is_number_integer()) {
            e.bit = static_cast<std::uint8_t>(bIt->get<int>());
        }
        out.insert_or_assign(it.key(), std::move(e));
    }
}

}  // namespace

CheckSaveBindings loadCheckSaveBindings(const std::filesystem::path& jsonPath) {
    CheckSaveBindings out;

    std::ifstream in(jsonPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "warning: save bindings file not found at %s\n",
                     jsonPath.string().c_str());
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ss.str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "warning: %s parse error: %s\n",
                     jsonPath.filename().string().c_str(), e.what());
        return out;
    }
    if (!j.is_object()) {
        std::fprintf(stderr, "warning: %s is not an object\n",
                     jsonPath.filename().string().c_str());
        return out;
    }

    out.reserve(j.size());
    mergeBindingsJson(out, j);
    return out;
}

std::optional<bool> isCheckComplete(const CheckSaveBinding& e,
                                    std::span<const std::uint8_t> save,
                                    std::uint8_t currentNode) {
    if (e.type == SaveBindingType::Event) return std::nullopt;
    if (!e.offset || !e.bit) return std::nullopt;

    std::uint32_t addr = 0;
    if (e.type == SaveBindingType::Flag || e.name == "Hyrule Castle Ganondorf") {
        // Flag types live at save_addr + offset directly. The Ganondorf
        // exception mirrors the historical apworld behavior — even though
        // it's listed as Region elsewhere, the actual completion bit is
        // read flag-style.
        addr = *e.offset;
    } else if (e.type == SaveBindingType::Region) {
        if (!e.region) return std::nullopt;
        const std::uint32_t regionId = *e.region;
        addr = (regionId == currentNode)
            ? kOffsetActiveNode + *e.offset
            : kOffsetNodesStart + regionId * 32 + *e.offset;
    } else {
        return std::nullopt;
    }

    if (addr >= save.size()) return std::nullopt;
    return (save[addr] & *e.bit) != 0;
}

std::unordered_set<std::string> completedCheckSet(
    const CheckSaveBindings& bindings,
    std::span<const std::uint8_t> save,
    std::uint8_t currentNode,
    const SeedPlacements& placements) {
    std::unordered_set<std::string> out;
    for (const auto& [name, e] : bindings) {
        const auto v = isCheckComplete(e, save, currentNode);
        if (v && *v) out.insert(name);
    }
    // Placement-based fallback for checks the bindings can't resolve
    // (rupees, freestandings, ...). Each placed item's "first-bit"
    // (SAVE+0x0CC) flips when the rando hands the item out — see
    // _02_*ItemGetCheck in Randomizer-master/.../02_modifyItemData.cpp.
    //
    // IMPORTANT: only progression items survive this fallback. The first-bit
    // is per-item-ID, not per-check, so non-unique items (rupees, hearts,
    // ammo) would otherwise give false positives — e.g. picking up any green
    // rupee in grass would mark every Green-Rupee placement as collected.
    // Progression items are unique in a seed, so their first-bit uniquely
    // identifies the specific check that delivered them.
    for (const auto& [name, itemId] : placements) {
        if (!isProgressionItemId(itemId)) continue;
        if (out.contains(name)) continue;
        if (readGetItemFlag(save, itemId)) out.insert(name);
    }
    // Portal checks. The Region binding above reads the per-region stage switch
    // flag — the same signal the map screen uses to draw the warp (see
    // kPortalTable in QuestState.h). That switch is set both when the portal is
    // opened in-world and when the rando hands out the portal item. The one
    // case it misses is a seed pre-giving the portal as a starting item
    // ("Unlock Map Regions" on a pre-cleared province): that sets only the
    // item's get-item first-bit, so we OR it in here as a fallback.
    for (const auto& p : kPortalTable) {
        std::string name = std::string(p.name) + " Portal";
        if (!bindings.count(name)) continue;  // "Ordon Spring" has no check
        if (out.contains(name)) continue;
        if (readGetItemFlag(save, p.itemId)) out.insert(std::move(name));
    }
    return out;
}

}  // namespace tpt::core
