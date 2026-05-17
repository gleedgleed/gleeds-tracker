#include "core/Stages.h"

#include <array>
#include <utility>

namespace tpt::core {

namespace {

// Stage code -> human-readable description. Indices match libtp_rel's
// `data/stage::allStages` / `stageDescs` (78 entries, 0..77). Hand-copied
// from `externals/libtp_rel/include/data/stages.h` in the bundled
// Randomizer-master reference repo (attribution lives in the project
// root); duplicated here so tptracker doesn't pull libtp_rel as a build
// dependency.
//
// Several codes share a description because libtp's table doesn't
// distinguish interior/exterior variants of the same area — F_SP103,
// F_SP104, F_SP123 all describe as "Faron Woods", and so on. That's
// fine for a "where am I?" UI label.
//
// Spelling note: libtp's source has "Bublin" for indices 55 and 58.
// Corrected to "Bulblin" to match the in-game name and the Web-Generator
// StageIDs enum.
constexpr std::array<std::pair<std::string_view, std::string_view>, 78> kStages{{
    {"D_MN01",   "Lakebed Temple"},
    {"D_MN01A",  "Morpheel"},
    {"D_MN01B",  "Deku Toad"},
    {"D_MN04",   "Goron Mines"},
    {"D_MN04A",  "Fyrus"},
    {"D_MN04B",  "Dangoro"},
    {"D_MN05",   "Forest Temple"},
    {"D_MN05A",  "Diababa"},
    {"D_MN05B",  "Ook"},
    {"D_MN06",   "Temple of Time"},
    {"D_MN06A",  "Armogohma"},
    {"D_MN06B",  "Darknut"},
    {"D_MN07",   "City in the Sky"},
    {"D_MN07A",  "Argorok"},
    {"D_MN07B",  "Aeralfos"},
    {"D_MN08",   "Palace of Twilight"},
    {"D_MN08A",  "Zant Main Room"},
    {"D_MN08B",  "Phantom Zant 1"},
    {"D_MN08C",  "Phantom Zant 2"},
    {"D_MN08D",  "Zant Fight"},
    {"D_MN09",   "Hyrule Castle"},
    {"D_MN09A",  "Ganondorf Castle"},
    {"D_MN09B",  "Ganondorf Field"},
    {"D_MN09C",  "Ganondorf Defeated"},
    {"D_MN10",   "Arbiters Grounds"},
    {"D_MN10A",  "Stallord"},
    {"D_MN10B",  "Death Sword"},
    {"D_MN11",   "Snowpeak Ruins"},
    {"D_MN11A",  "Blizzeta"},
    {"D_MN11B",  "Darkhammer"},
    {"D_SB00",   "Lanayru Ice Puzzle Cave"},
    {"D_SB01",   "Cave of Ordeals"},
    {"D_SB02",   "Eldin Long Cave"},
    {"D_SB03",   "Lake Hylia Long Cave"},
    {"D_SB04",   "Eldin Goron Stockcave"},
    {"D_SB05",   "Grotto 1"},
    {"D_SB06",   "Grotto 2"},
    {"D_SB07",   "Grotto 3"},
    {"D_SB08",   "Grotto 4"},
    {"D_SB09",   "Grotto 5"},
    {"D_SB10",   "Faron Woods Cave"},
    {"F_SP00",   "Ordon Ranch"},
    {"F_SP102",  "Title Screen"},
    {"F_SP103",  "Ordon Village"},
    {"F_SP104",  "Ordon Spring"},
    {"F_SP108",  "Faron Woods"},
    {"F_SP109",  "Kakariko Village"},
    {"F_SP110",  "Death Mountain"},
    {"F_SP111",  "Kakariko Graveyard"},
    {"F_SP112",  "Zoras River"},
    {"F_SP113",  "Zoras Domain"},
    {"F_SP114",  "Snowpeak"},
    {"F_SP115",  "Lake Hylia"},
    {"F_SP116",  "Castle Town"},
    {"F_SP117",  "Sacred Grove"},
    {"F_SP118",  "Bulblin Camp"},
    {"F_SP121",  "Hyrule Field"},
    {"F_SP122",  "Outside Castle Town"},
    {"F_SP123",  "Bulblin 2"},
    {"F_SP124",  "Gerudo Desert"},
    {"F_SP125",  "Mirror Chamber"},
    {"F_SP126",  "Upper Zoras River"},
    {"F_SP127",  "Fishing Pond"},
    {"F_SP128",  "Hidden Village"},
    {"F_SP200",  "Hidden Skill"},
    {"R_SP01",   "Ordon Village"},
    {"R_SP107",  "Hyrule Castle Sewers"},
    {"R_SP108",  "Faron Woods"},
    {"R_SP109",  "Kakariko Village"},
    {"R_SP110",  "Death Mountain"},
    {"R_SP116",  "Telmas Bar"},
    {"R_SP127",  "Fishing Pond"},
    {"R_SP128",  "Hidden Village"},
    {"R_SP160",  "Castle Town"},
    {"R_SP161",  "Star Game"},
    {"R_SP209",  "Kakariko Graveyard"},
    {"R_SP300",  "Light Arrows Cutscene"},
    {"R_SP301",  "Hyrule Castle Cutscenes"},
}};

}  // namespace

std::string_view friendlyStageName(std::string_view code) {
    for (const auto& [k, v] : kStages) {
        if (k == code) return v;
    }
    return code;
}

}  // namespace tpt::core
