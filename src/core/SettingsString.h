#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tpt::core {

// Decoded form of the web-gen SharedSettings struct. Field naming matches the
// C# `SharedSettings` for easy diffing. Trailing list types
// (startingItems / excludedChecks / plandoChecks) are deliberately not
// decoded — they don't affect "what's reachable given my items".
struct ParsedSettings {
    std::uint32_t version = 0;

    std::string logicRules;
    std::string castleRequirements;
    std::string palaceRequirements;
    std::string faronWoodsLogic;
    bool   shuffleGoldenBugs    = false;
    bool   shuffleSkyCharacters = false;
    bool   shuffleNpcItems      = false;
    std::string shufflePoes;
    bool   shuffleShopItems     = false;
    bool   shuffleHiddenSkills  = false;
    std::string smallKeySettings;
    std::string bigKeySettings;
    std::string mapAndCompassSettings;
    bool   skipPrologue            = false;
    bool   faronTwilightCleared    = false;
    bool   eldinTwilightCleared    = false;
    bool   lanayruTwilightCleared  = false;
    bool   skipMdh                 = false;
    bool   skipMinorCutscenes      = false;
    bool   fastIronBoots           = false;
    bool   quickTransform          = false;
    bool   transformAnywhere       = false;
    std::string walletSize;
    bool   modifyShopModels        = false;
    std::string trapFrequency;
    bool   barrenDungeons          = false;
    std::string goronMinesEntrance;
    bool   skipLakebedEntrance     = false;
    bool   skipArbitersEntrance    = false;
    bool   skipSnowpeakEntrance    = false;
    bool   skipGroveEntrance       = false;
    std::string totEntrance;
    bool   skipCityEntrance        = false;
    bool   instantText             = false;
    bool   openMap                 = false;
    bool   increaseSpinnerSpeed    = false;
    bool   openDot                 = false;
    std::string itemScarcity;
    std::string damageMagnification;
    bool   bonksDoDamage           = false;
    bool   shuffleRewards          = false;
    bool   skipMajorCutscenes      = false;
    bool   noSmallKeysOnBosses     = false;
    std::string startingTod;
    std::string hintDistribution;
    bool   randomizeStartingPoint  = false;
    bool   shuffleHiddenRupees     = false;
    bool   gmShortcut              = false;
    bool   hcShortcut              = false;
    std::string iliaQuest;
    std::string mirrorChamberEntrance;
    std::string shuffleDungeonEntrances;
    bool   unpairEntrances         = false;
    bool   decoupleEntrances       = false;
    bool   shuffleFreestandingRupees = false;
    std::uint32_t castleRequirementCount   = 0;
    std::string castleBkRequirements;
    std::uint32_t castleBkRequirementCount = 0;
    bool   autoFillWallet          = false;
    bool   skipBridgeDonation      = false;
    std::uint32_t maloShopDonation = 0;
    std::string hintImportance;
    bool   noPlandoHints                = false;
    bool   adjustHintsForCompletionists = false;
    bool   hintDungeonEntrances    = false;
};

class SettingsParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Parse a TPR settings string. Throws SettingsParseError on bad input.
ParsedSettings decodeSettingsString(std::string_view s);

// The web-gen's default settings: every enum at its index-0 value, every bool
// false, every count 0 — i.e. what decoding an all-zero settings string would
// yield. Used as a baseline so logic `Setting.X equals Y` comparisons resolve
// to the generator's actual default when the live seed / settings string don't
// specify X, instead of falling back to the blanket-permissive answer.
ParsedSettings defaultParsedSettings();

}  // namespace tpt::core
