#include "core/SettingsString.h"

#include <array>
#include <cstdio>
#include <utility>
#include <vector>

namespace tpt::core {

namespace {

// Mirrors Generator/Util/SettingsEncoder.cs::charMap (6-bit alphabet).
constexpr std::string_view kCharMap =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";

int decodeChar(char c) {
    const auto pos = kCharMap.find(c);
    if (pos == std::string_view::npos) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Invalid settings-string char: %c (0x%02X)",
                      c, static_cast<std::uint8_t>(c));
        throw SettingsParseError(buf);
    }
    return static_cast<int>(pos);
}

// Decode N chars (N <= 5 fits in 30 bits, safely in unsigned).
std::uint32_t decodeToInt(std::string_view s) {
    std::uint32_t v = 0;
    for (char c : s) v = (v << 6) | static_cast<std::uint32_t>(decodeChar(c));
    return v;
}

// Decoded bit stream. Bits are stored MSB-first within each char's 6-bit value
// to match Python's `format(x, '06b')`.
class BitsProcessor {
  public:
    explicit BitsProcessor(std::vector<bool> bits) : bits_(std::move(bits)) {}

    bool nextBool() {
        if (idx_ >= bits_.size()) throwShort(1);
        return bits_[idx_++];
    }

    std::uint32_t nextInt(std::size_t n) {
        if (idx_ + n > bits_.size()) throwShort(n);
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < n; ++i) v = (v << 1) | (bits_[idx_++] ? 1u : 0u);
        return v;
    }

  private:
    [[noreturn]] void throwShort(std::size_t want) const {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "Not enough bits remaining (have %zu, want %zu)",
            bits_.size() - idx_, want);
        throw SettingsParseError(buf);
    }

    std::vector<bool> bits_;
    std::size_t idx_ = 0;
};

std::vector<bool> charsToBits(std::string_view s) {
    std::vector<bool> out;
    out.reserve(s.size() * 6);
    for (char c : s) {
        const std::uint32_t v = static_cast<std::uint32_t>(decodeChar(c));
        for (int i = 5; i >= 0; --i) out.push_back(((v >> i) & 1) != 0);
    }
    return out;
}

constexpr std::array<std::pair<int, std::string_view>, 3> kLogicRules{{
    {0, "Glitchless"}, {1, "Glitched"}, {2, "No_Logic"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kWalletSize{{
    {0, "Reduced"}, {1, "Vanilla"}, {2, "HD"}, {3, "Large"},
}};
constexpr std::array<std::pair<int, std::string_view>, 7> kCastleReq{{
    {0, "Open"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"},
    {3, "Dungeons"}, {4, "Vanilla"}, {5, "Poe_Souls"}, {6, "Hearts"},
}};
constexpr std::array<std::pair<int, std::string_view>, 6> kCastleBkReq{{
    {0, "None"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"},
    {3, "Dungeons"}, {4, "Poe_Souls"}, {5, "Hearts"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kPalaceReq{{
    {0, "Open"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"}, {3, "Vanilla"},
}};
constexpr std::array<std::pair<int, std::string_view>, 2> kFaronWoods{{
    {0, "Open"}, {1, "Closed"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kPoes{{
    {0, "Vanilla"}, {1, "Overworld"}, {2, "Dungeons"}, {3, "All"},
}};
constexpr std::array<std::pair<int, std::string_view>, 5> kKeys{{
    {0, "Vanilla"}, {1, "Own_Dungeon"}, {2, "Any_Dungeon"}, {3, "Anywhere"},
    {4, "Keysy"},
}};
constexpr std::array<std::pair<int, std::string_view>, 5> kMapCompass{{
    {0, "Vanilla"}, {1, "Own_Dungeon"}, {2, "Any_Dungeon"}, {3, "Anywhere"},
    {4, "Start_With"},
}};
constexpr std::array<std::pair<int, std::string_view>, 5> kTrapFreq{{
    {0, "None"}, {1, "Few"}, {2, "Many"}, {3, "Mayhem"}, {4, "Nightmare"},
}};
constexpr std::array<std::pair<int, std::string_view>, 5> kTotEntrance{{
    {0, "None"}, {1, "Wooden_Sword"}, {2, "Ordon_Sword"},
    {3, "Master_Sword"}, {4, "Light_Sword"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kGoronMines{{
    {0, "Closed"}, {1, "NoWrestling"}, {2, "Open"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kItemScarcity{{
    {0, "Vanilla"}, {1, "Minimal"}, {2, "Plentiful"},
}};
constexpr std::array<std::pair<int, std::string_view>, 6> kDamageMag{{
    {0, "Default"}, {1, "Vanilla"}, {2, "Double"}, {3, "Triple"},
    {4, "Quadruple"}, {5, "OHKO"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kStartingTod{{
    {0, "Morning"}, {1, "Noon"}, {2, "Evening"}, {3, "Night"},
}};
constexpr std::array<std::pair<int, std::string_view>, 7> kHintDistribution{{
    {0, "None"}, {1, "Season_1"}, {2, "Weak"}, {3, "Balanced"},
    {4, "Strong"}, {5, "Very_Strong"}, {6, "Season_2"},
}};
constexpr std::array<std::pair<int, std::string_view>, 5> kIliaQuest{{
    {0, "Vanilla"}, {1, "Letter"}, {2, "Invoice"}, {3, "Statue"}, {4, "Charm"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kMirrorChamber{{
    {0, "Open"}, {1, "Barrier"}, {2, "Closed"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kDungeonER{{
    {0, "Off"}, {1, "Dungeon"}, {2, "Dungeon_Hyrule"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kHintImportance{{
    {0, "Default"}, {1, "Calculate"}, {2, "Upgrade_Hints"},
}};

template <std::size_t N>
std::string mapEnum(const std::array<std::pair<int, std::string_view>, N>& table,
                    std::uint32_t v) {
    for (const auto& [k, name] : table) {
        if (k == static_cast<int>(v)) return std::string(name);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "?(%u)", v);
    return std::string(buf);
}

}  // namespace

ParsedSettings decodeSettingsString(std::string_view raw) {
    // Trim leading/trailing whitespace.
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' ||
                            raw.front() == '\n' || raw.front() == '\r')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' ||
                            raw.back() == '\n' || raw.back() == '\r')) raw.remove_suffix(1);

    // Hex version prefix, terminated by 's'.
    std::size_t verLen = 0;
    while (verLen < raw.size()) {
        const char c = raw[verLen];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) ++verLen;
        else break;
    }
    if (verLen == 0 || verLen >= raw.size() || raw[verLen] != 's') {
        throw SettingsParseError("Settings string missing version+'s' prefix");
    }
    std::uint32_t version = 0;
    for (std::size_t i = 0; i < verLen; ++i) {
        const char c = raw[i];
        const int d = (c >= '0' && c <= '9') ? (c - '0')
                    : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
                    :                          (c - 'A' + 10);
        version = (version << 4) | static_cast<std::uint32_t>(d);
    }

    std::size_t cursor = verLen + 1;
    if (cursor >= raw.size()) throw SettingsParseError("Settings string truncated at length char");

    const std::uint32_t lengthVal = decodeToInt(raw.substr(cursor, 1));
    const std::size_t   lenDefCharCount = lengthVal & 0b111;
    const std::size_t   numExtraBits    = (lengthVal >> 3) & 0b111;
    cursor += 1;

    if (cursor + lenDefCharCount > raw.size())
        throw SettingsParseError("Settings string truncated at length data");
    const std::uint32_t numChars = decodeToInt(raw.substr(cursor, lenDefCharCount));
    cursor += lenDefCharCount;

    if (cursor + numChars > raw.size())
        throw SettingsParseError("Settings string truncated at data section");
    auto bits = charsToBits(raw.substr(cursor, numChars));
    if (numExtraBits > 0) {
        const std::size_t pad = 6 - numExtraBits;
        if (bits.size() < pad) bits.clear();
        else bits.resize(bits.size() - pad);
    }

    BitsProcessor p(std::move(bits));
    ParsedSettings out;
    out.version = version;

    out.logicRules            = mapEnum(kLogicRules,    p.nextInt(2));
    out.castleRequirements    = mapEnum(kCastleReq,     p.nextInt(3));
    out.palaceRequirements    = mapEnum(kPalaceReq,     p.nextInt(2));
    out.faronWoodsLogic       = mapEnum(kFaronWoods,    p.nextInt(1));
    out.shuffleGoldenBugs     = p.nextBool();
    out.shuffleSkyCharacters  = p.nextBool();
    out.shuffleNpcItems       = p.nextBool();
    out.shufflePoes           = mapEnum(kPoes,          p.nextInt(2));
    out.shuffleShopItems      = p.nextBool();
    out.shuffleHiddenSkills   = p.nextBool();
    out.smallKeySettings      = mapEnum(kKeys,          p.nextInt(3));
    out.bigKeySettings        = mapEnum(kKeys,          p.nextInt(3));
    out.mapAndCompassSettings = mapEnum(kMapCompass,    p.nextInt(3));
    out.skipPrologue            = p.nextBool();
    out.faronTwilightCleared    = p.nextBool();
    out.eldinTwilightCleared    = p.nextBool();
    out.lanayruTwilightCleared  = p.nextBool();
    out.skipMdh                 = p.nextBool();
    out.skipMinorCutscenes      = p.nextBool();
    out.fastIronBoots           = p.nextBool();
    out.quickTransform          = p.nextBool();
    out.transformAnywhere       = p.nextBool();
    out.walletSize              = mapEnum(kWalletSize,  p.nextInt(2));
    out.modifyShopModels        = p.nextBool();
    out.trapFrequency           = mapEnum(kTrapFreq,    p.nextInt(3));
    out.barrenDungeons          = p.nextBool();
    out.goronMinesEntrance      = mapEnum(kGoronMines,  p.nextInt(2));
    out.skipLakebedEntrance     = p.nextBool();
    out.skipArbitersEntrance    = p.nextBool();
    out.skipSnowpeakEntrance    = p.nextBool();
    out.skipGroveEntrance       = p.nextBool();
    out.totEntrance             = mapEnum(kTotEntrance, p.nextInt(3));
    out.skipCityEntrance        = p.nextBool();
    out.instantText             = p.nextBool();
    out.openMap                 = p.nextBool();
    out.increaseSpinnerSpeed    = p.nextBool();
    out.openDot                 = p.nextBool();
    out.itemScarcity            = mapEnum(kItemScarcity, p.nextInt(2));
    out.damageMagnification     = mapEnum(kDamageMag,   p.nextInt(3));
    out.bonksDoDamage           = p.nextBool();
    out.shuffleRewards          = p.nextBool();
    out.skipMajorCutscenes      = p.nextBool();
    out.noSmallKeysOnBosses     = p.nextBool();
    out.startingTod             = mapEnum(kStartingTod,  p.nextInt(3));
    out.hintDistribution        = mapEnum(kHintDistribution, p.nextInt(5));
    out.randomizeStartingPoint  = p.nextBool();
    out.shuffleHiddenRupees     = p.nextBool();
    out.gmShortcut              = p.nextBool();
    out.hcShortcut              = p.nextBool();
    out.iliaQuest               = mapEnum(kIliaQuest,    p.nextInt(3));
    out.mirrorChamberEntrance   = mapEnum(kMirrorChamber, p.nextInt(2));
    out.shuffleDungeonEntrances = mapEnum(kDungeonER,    p.nextInt(2));
    out.unpairEntrances         = p.nextBool();
    out.decoupleEntrances       = p.nextBool();
    out.shuffleFreestandingRupees = p.nextBool();
    out.castleRequirementCount  = p.nextInt(6);
    out.castleBkRequirements    = mapEnum(kCastleBkReq,  p.nextInt(3));
    out.castleBkRequirementCount = p.nextInt(6);
    out.autoFillWallet          = p.nextBool();
    out.skipBridgeDonation      = p.nextBool();
    out.maloShopDonation        = p.nextInt(11);
    out.hintImportance          = mapEnum(kHintImportance, p.nextInt(2));
    out.noPlandoHints           = p.nextBool();
    out.adjustHintsForCompletionists = p.nextBool();
    out.hintDungeonEntrances    = p.nextBool();

    return out;
}

}  // namespace tpt::core
