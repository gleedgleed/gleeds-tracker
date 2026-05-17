#include "ui/Render.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/Stages.h"
#include "game/GameModule.h"

namespace tpt::ui::render {

namespace {

ImVec4 colorForStatus(CheckStatus s) {
    switch (s) {
        case CheckStatus::Done:    return {0.55f, 0.95f, 0.55f, 1.0f};  // green
        case CheckStatus::Pending: return {0.85f, 0.85f, 0.85f, 1.0f};  // light gray
        case CheckStatus::Unknown: return {0.50f, 0.50f, 0.50f, 1.0f};  // dim gray
    }
    return {1, 1, 1, 1};
}

// Cache of explanation text by check name. Returns "" if no file exists.
const std::string& explanationFor(const std::string& checkName) {
    static std::unordered_map<std::string, std::string> kCache;
    static const std::string kEmpty;
    if (auto it = kCache.find(checkName); it != kCache.end()) return it->second;

    namespace fs = std::filesystem;
    fs::path explDir;
#ifdef _WIN32
    char buf[260] = {};
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) > 0) {
        explDir = fs::path(buf).parent_path() / "explanations";
    }
#else
    explDir = fs::current_path() / "explanations";
#endif

    // Sanitise filename (no path traversal).
    std::string safe = checkName;
    for (char& c : safe) if (c == '/' || c == '\\') c = '_';
    const fs::path path = explDir / (safe + ".md");

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        kCache.emplace(checkName, kEmpty);
        return kCache[checkName];
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto [it, _] = kCache.emplace(checkName, ss.str());
    return it->second;
}

// Pick a header color for a stage based on how many entries are Done.
//   all done -> green
//   some done -> amber (in-progress convention)
//   none done -> default text
ImVec4 colorForStageHeader(int doneCount, int total) {
    if (total > 0 && doneCount == total) return {0.55f, 0.95f, 0.55f, 1.0f};   // green
    if (doneCount > 0)                   return {1.00f, 0.75f, 0.40f, 1.0f};   // amber
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);                            // default
}

void renderCheckTree(
    State& s,
    const std::map<std::string, std::vector<CheckEntry>>& byStage,
    const char* idPrefix) {

    for (const auto& [stage, entries] : byStage) {
        int doneCount = 0;
        for (const auto& e : entries) if (e.status == CheckStatus::Done) ++doneCount;
        const int total = static_cast<int>(entries.size());

        // Show "(X/Y)" once we have any done entries, otherwise just "(Y)".
        // The label is *display only* — counts change as checks complete or
        // filters toggle. We pass `stage` as the ImGui str_id so the node's
        // open/closed state survives those label changes (otherwise ImGui's
        // "label is the ID" default collapses the tree on every recount).
        std::string label = stage + " (";
        if (doneCount > 0) label += std::to_string(doneCount) + "/";
        label += std::to_string(total) + ")";

        ImGui::PushID(idPrefix);
        ImGui::PushStyleColor(ImGuiCol_Text, colorForStageHeader(doneCount, total));
        const bool open = ImGui::TreeNodeEx(
            stage.c_str(), ImGuiTreeNodeFlags_None, "%s", label.c_str());
        ImGui::PopStyleColor();
        if (open) {
            for (const auto& e : entries) {
                ImGui::PushStyleColor(ImGuiCol_Text, colorForStatus(e.status));
                const std::string row = std::string(statusMarker(e.status)) + " " + e.name;
                const bool selected = (s.selectedCheck == e.name);
                if (ImGui::Selectable(row.c_str(), selected)) {
                    s.selectedCheck = e.name;
                }
                ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// (renderInventorySummary + renderRandomizerSettingsPane moved to
//  src/game/tp/TwilightPrincessGame.cpp.)

// (Randomizer settings pane moved to src/game/tp/TwilightPrincessGame.cpp.)

// ---------------------------------------------------------------------------
// Tiny markdown renderer for the notes pane. Handles the subset our
// explanation files actually use:
//   # / ## / ###   headings (sage-tinted, larger font)
//   **bold**       inline emphasis (brighter color, wraps within a paragraph)
//   - item, * item bullet lists
//   1. item        numbered lists (any number of digits)
//   blank line     paragraph break
// Anything else renders as a wrapping paragraph.

namespace md {

constexpr ImVec4 kHeadingColor{0.55f, 0.85f, 0.75f, 1.0f};   // sage accent
constexpr ImVec4 kBoldColor   {0.78f, 0.96f, 0.88f, 1.0f};   // lighter sage; pops against gray body text

void renderHeading(std::string_view text, float scale) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, kHeadingColor);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
}

// Render a single line, splitting on **bold** markers and [text](url) links,
// with manual word-wrap.
//
// Why manual wrap: ImGui's PushTextWrapPos doesn't compose with SameLine(0,0).
// When TextWrapped wraps mid-segment, the wrap origin sticks to where the
// SameLine put the cursor (near the right edge), producing the "characters
// falling straight down the right side" failure mode. So we measure each word
// ourselves and snap X back to startX when the next word wouldn't fit.
void renderInlineWithBold(std::string_view text) {
    if (text.empty()) return;

    struct Word {
        std::string text;
        bool bold = false;
        std::string url;     // empty = not a hyperlink
    };
    std::vector<Word> words;

    // Split a plain (non-bold-toggle) span by spaces and push each word.
    auto pushPlain = [&words](std::string_view t, bool bold, const std::string& url) {
        std::size_t k = 0;
        while (k < t.size()) {
            while (k < t.size() && t[k] == ' ') ++k;
            if (k >= t.size()) break;
            const std::size_t start = k;
            while (k < t.size() && t[k] != ' ') ++k;
            words.push_back({std::string(t.substr(start, k - start)), bold, url});
        }
    };

    // Within a bold-uniform span, peel off [label](url) chunks. Anything that
    // doesn't look like a valid link is treated as plain text.
    auto pushSpan = [&](std::string_view span, bool bold) {
        std::size_t k = 0;
        while (k < span.size()) {
            const auto bracket = span.find('[', k);
            if (bracket == std::string_view::npos) {
                pushPlain(span.substr(k), bold, {});
                break;
            }
            const auto closeBracket = span.find(']', bracket + 1);
            const bool hasParen =
                closeBracket != std::string_view::npos
                && closeBracket + 1 < span.size()
                && span[closeBracket + 1] == '(';
            const auto closeParen = hasParen
                ? span.find(')', closeBracket + 2)
                : std::string_view::npos;
            if (closeParen == std::string_view::npos) {
                // Stray '[' — emit through the bracket as plain and continue.
                pushPlain(span.substr(k, bracket - k + 1), bold, {});
                k = bracket + 1;
                continue;
            }
            if (bracket > k) pushPlain(span.substr(k, bracket - k), bold, {});
            const auto label = span.substr(bracket + 1, closeBracket - bracket - 1);
            const auto url   = std::string(span.substr(
                closeBracket + 2, closeParen - closeBracket - 2));
            pushPlain(label, bold, url);
            k = closeParen + 1;
        }
    };

    // Outer pass: split the line by **bold** boundaries.
    std::size_t i = 0;
    while (i < text.size()) {
        const auto open = text.find("**", i);
        if (open == std::string_view::npos) { pushSpan(text.substr(i), false); break; }
        if (open > i) pushSpan(text.substr(i, open - i), false);
        const auto close = text.find("**", open + 2);
        if (close == std::string_view::npos) {
            pushSpan(text.substr(open), false);   // unterminated — render verbatim
            break;
        }
        pushSpan(text.substr(open + 2, close - open - 2), true);
        i = close + 2;
    }
    if (words.empty()) return;

    const float startX = ImGui::GetCursorPosX();
    const float maxX   = startX + ImGui::GetContentRegionAvail().x;
    const float spaceW = ImGui::CalcTextSize(" ").x;

    float curX = startX;
    bool  firstOnLine = true;

    for (const auto& w : words) {
        const float wordW = ImGui::CalcTextSize(w.text.c_str()).x;
        const float needed = (firstOnLine ? 0.0f : spaceW) + wordW;

        if (!firstOnLine && curX + needed > maxX) {
            ImGui::SetCursorPosX(startX);
            curX = startX;
            firstOnLine = true;
        }

        std::string display;
        if (!firstOnLine) display = " ";
        display.append(w.text);

        if (!firstOnLine) ImGui::SameLine(0, 0);

        if (!w.url.empty()) {
            // ImGui's TextLinkOpenURL handles styling, hover cursor, click
            // detection, and the platform-IO open-url callback for us.
            ImGui::TextLinkOpenURL(display.c_str(), w.url.c_str());
        } else if (w.bold) {
            ImGui::PushStyleColor(ImGuiCol_Text, kBoldColor);
            ImGui::TextUnformatted(display.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(display.c_str());
        }

        curX += (firstOnLine ? wordW : spaceW + wordW);
        firstOnLine = false;
    }
}

// Returns the prefix length (digits + ". ") if `line` starts with a numbered
// list marker (e.g. "1. ", "12. "); 0 otherwise.
std::size_t numberedListPrefixLen(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i > 0 && i + 1 < line.size() && line[i] == '.' && line[i + 1] == ' ')
        return i + 2;
    return 0;
}

void render(const std::string& text) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto eol = text.find('\n', pos);
        std::string_view line = (eol == std::string::npos)
            ? std::string_view(text).substr(pos)
            : std::string_view(text).substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (line.empty()) {
            ImGui::Spacing();
        } else if (line == "---" || line == "***" || line == "___") {
            // Horizontal rule.
            ImGui::Separator();
        } else if (line.size() >= 4 && line.substr(0, 4) == "### ") {
            renderHeading(line.substr(4), 1.10f);
        } else if (line.size() >= 3 && line.substr(0, 3) == "## ") {
            renderHeading(line.substr(3), 1.20f);
        } else if (line.size() >= 2 && line.substr(0, 2) == "# ") {
            renderHeading(line.substr(2), 1.40f);
        } else if (line.size() >= 2 &&
                   (line[0] == '-' || line[0] == '*' || line[0] == '+') &&
                   line[1] == ' ') {
            ImGui::Bullet();
            ImGui::SameLine();
            renderInlineWithBold(line.substr(2));
        } else if (const auto n = numberedListPrefixLen(line); n > 0) {
            ImGui::Text("%.*s", static_cast<int>(n - 1), line.data());
            ImGui::SameLine();
            renderInlineWithBold(line.substr(n));
        } else {
            renderInlineWithBold(line);
        }

        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
}

}  // namespace md

void renderNotesPane(const State& s) {
    if (s.selectedCheck.empty()) {
        ImGui::TextDisabled("(select a check on the left or middle column)");
        return;
    }
    ImGui::BeginChild("##NotesScroll");
    const auto& text = explanationFor(s.selectedCheck);
    if (text.empty()) {
        ImGui::TextUnformatted(s.selectedCheck.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("(no explanation file at explanations/%s.md)",
                            s.selectedCheck.c_str());
    } else {
        md::render(text);
    }
    ImGui::EndChild();
}

}  // namespace

// Font-size slider popup, opened from a button on the status bar.
void renderFontSizePopup(style::Settings& styleSettings, bool& fontReloadNeeded) {
    if (ImGui::BeginPopup("##FontSizePopup")) {
        style::renderControls(styleSettings, fontReloadNeeded);
        ImGui::EndPopup();
    }
}

void statusBar(const State& s) {
    const char* src = s.sourceName.empty() ? "" : s.sourceName.c_str();
    if (!s.emulatorHooked) {
        ImGui::TextColored({1.0f, 0.6f, 0.6f, 1.0f},
            src[0] ? "[%s: disconnected]" : "[disconnected]", src);
    } else if (!s.saveLoaded) {
        ImGui::TextColored({1.0f, 0.85f, 0.5f, 1.0f},
            src[0] ? "[%s: hooked, no save]" : "[hooked, no save]", src);
    } else {
        ImGui::TextColored({0.6f, 1.0f, 0.6f, 1.0f},
            src[0] ? "[%s: hooked]" : "[hooked]", src);
    }
    ImGui::SameLine();
    if (s.region) {
        ImGui::Text("| %s %.*s",
            s.gameId.empty() ? "?" : s.gameId.c_str(),
            static_cast<int>(s.region->name.size()), s.region->name.data());
        ImGui::SameLine();
    }
    if (s.seed && !s.seed->seedName.empty()) {
        ImGui::Text("| seed: %s", s.seed->seedName.c_str());
        ImGui::SameLine();
    }
    if (s.totalResolvable > 0) {
        ImGui::Text("| %d/%d done | %d reachable pending",
                    s.totalCompleted, s.totalResolvable, s.totalReachablePending);
        ImGui::SameLine();
    }
    ImGui::Text("| %s", s.glitched ? "glitched" : "glitchless");
    if (!s.error.empty()) {
        ImGui::SameLine();
        ImGui::TextColored({1, 0.55f, 0.55f, 1}, "| %s", s.error.c_str());
    }
}

namespace {

// Sum of leaf counts across stages.
std::size_t totalEntries(const std::map<std::string, std::vector<CheckEntry>>& m) {
    std::size_t n = 0;
    for (const auto& [_, v] : m) n += v.size();
    return n;
}

// Render one tab: a scrollable region containing the per-stage tree.
void renderTabBody(State& s,
                   const std::map<std::string, std::vector<CheckEntry>>& byStage,
                   const char* idPrefix, const char* emptyMessage) {
    if (byStage.empty()) {
        ImGui::TextDisabled("%s", emptyMessage);
        return;
    }
    ImGui::BeginChild(("##scroll_" + std::string(idPrefix)).c_str());
    renderCheckTree(s, byStage, idPrefix);
    ImGui::EndChild();
}

}  // namespace

void allChecksColumn(State& s, tpt::game::GameModule& game) {
    // FittingPolicyScroll keeps each tab at its natural label width — no
    // "All chec..." truncation. Tabs that don't fit get scroll buttons.
    //
    // Tab labels include a count, but the count changes when checks complete
    // or when the rupee-filter toggles. The "###StableId" suffix tells ImGui
    // to treat the part after ### as the persistent ID, so the active tab
    // doesn't lose its identity (and therefore selection) on every recount.
    if (ImGui::BeginTabBar("##AllChecksTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // Default tab: master list.
        const std::size_t totalAll = totalEntries(s.allByStage);
        const std::string allLabel =
            "All Checks (" + std::to_string(totalAll) + ")###AllChecks";
        if (ImGui::BeginTabItem(allLabel.c_str())) {
            renderTabBody(s, s.allByStage, "all_master", "(no checks loaded)");
            ImGui::EndTabItem();
        }
        // Per-flag tabs — show every check of that flag (any status).
        for (const auto& spec : game.filterSpecs()) {
            const auto it = s.flagAllByStage.find(spec.label);
            const std::size_t total = it == s.flagAllByStage.end() ? 0
                                                                   : totalEntries(it->second);
            const std::string tabLabel =
                spec.label + " (" + std::to_string(total) + ")###" + spec.label;
            if (ImGui::BeginTabItem(tabLabel.c_str())) {
                if (it == s.flagAllByStage.end()) {
                    ImGui::TextDisabled("(no checks of this type)");
                } else {
                    renderTabBody(s, it->second,
                        ("all_" + spec.label).c_str(), "(no checks of this type)");
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

void filtersColumn(State& s, tpt::game::GameModule& game) {
    // Same ###StableId trick as allChecksColumn — keeps tab selection sticky
    // across count changes from poll updates and filter toggles.
    if (ImGui::BeginTabBar("##ReachableTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // Default tab: every reachable+in-logic+pending check.
        const std::size_t totalReach = totalEntries(s.reachableByStage);
        const std::string reachLabel =
            "Reachable (" + std::to_string(totalReach) + ")###Reachable";
        if (ImGui::BeginTabItem(reachLabel.c_str())) {
            renderTabBody(s, s.reachableByStage, "reach_all",
                "(nothing in-logic right now)");
            ImGui::EndTabItem();
        }
        // Per-flag tabs — only reachable+pending entries of that flag.
        for (const auto& spec : game.filterSpecs()) {
            const auto it = s.flagReachableByStage.find(spec.label);
            const std::size_t total = it == s.flagReachableByStage.end() ? 0
                                                                         : totalEntries(it->second);
            const std::string tabLabel =
                spec.label + " (" + std::to_string(total) + ")###" + spec.label;
            if (ImGui::BeginTabItem(tabLabel.c_str())) {
                if (it == s.flagReachableByStage.end()) {
                    ImGui::TextDisabled("(none reachable)");
                } else {
                    renderTabBody(s, it->second,
                        ("reach_" + spec.label).c_str(), "(none reachable)");
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

// (controlsRow moved to src/game/tp/TwilightPrincessGame.cpp.)

void rightColumn(const State& s, tpt::game::GameModule& game) {
    const float avail = ImGui::GetContentRegionAvail().y;
    const float upperH = avail * 0.55f;

    ImGui::BeginChild("##RUpper", ImVec2(0, upperH), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Live State");
    ImGui::Separator();
    game.renderRightPane();
    ImGui::EndChild();

    ImGui::BeginChild("##RLower", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Notes");
    ImGui::Separator();
    renderNotesPane(s);
    ImGui::EndChild();
}

void mainLayout(State& s, tpt::game::GameModule& game,
                style::Settings& styleSettings, bool& fontReloadNeeded) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##TPTrackerRoot", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    statusBar(s);
    // Right-aligned "Font size" button with a small visual margin from the
    // window's right edge.
    constexpr float kButtonWidth = 80.0f;
    constexpr float kRightMargin = 14.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX()
                    - kButtonWidth - kRightMargin);
    if (ImGui::SmallButton("Font size")) ImGui::OpenPopup("##FontSizePopup");
    renderFontSizePopup(styleSettings, fontReloadNeeded);
    ImGui::Separator();

    game.renderOptionsRow();
    ImGui::Separator();

    if (ImGui::BeginTable("##Cols", 3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_NoSavedSettings,
            ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("All",    ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Filter", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Right",  ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        allChecksColumn(s, game);

        ImGui::TableNextColumn();
        filtersColumn(s, game);

        ImGui::TableNextColumn();
        rightColumn(s, game);

        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace tpt::ui::render
