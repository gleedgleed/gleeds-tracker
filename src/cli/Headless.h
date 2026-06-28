#pragma once

#include <cstdint>
#include <string>

namespace tpt::cli {

enum class Mode {
    Gui,           // default: launch the windowed app
    Items,         // dump decoded inventory and exit
    Quest,         // dump quest state (vitals, twilight, portals, switch keys)
    Flags,         // dump set event flags + set get-item flags
    Region,        // dump region + game ID and exit
    SaveDump,      // dump raw bytes from a console address and exit
    SeedInfo,      // scan RAM for TPR seed header and dump decoded settings
    Settings,      // decode an explicit settings string (no Dolphin needed)
    LogicStats,    // load world data + verify parser; no Dolphin needed
    Next,          // compute reachable-and-not-completed checks
    Placements,    // dump per-check seed placements with progression+completion
    MemWrite,      // poke a byte at a console address (--mem-write=ADDR,VALUE)
    DuskProbe,     // dev tool: introspect a running Dusk process for AOB derivation
    P64Probe,      // dev tool: introspect a running Project64 process for RDRAM derivation
    OotDump,       // dev tool: attach to Project64, read SaveContext, decode PlayerData+Inventory
    OotChecks,     // dev tool: list OoT checks by area with completion status
    OotWorld,      // dev tool: load OoTR World/*.json, print region/edge stats
    OotParse,      // dev tool: parse every rule expression in the world graph + LogicHelpers
    OotReach,      // dev tool: run reachability BFS against live save, list reachable checks
    OotExtras,     // dev tool: dump OoTR extended_savectx + xflag bytes, watch for drift
    OotEvents,     // dev tool: dump eventChkInf / itemGetInf / infTable as set-bit lists
    OotWrite,      // dev tool: write a single byte to N64 RAM via Project64
    Help,          // print usage and exit
};

struct Options {
    Mode mode = Mode::Gui;
    int  hookTimeoutSec = 5;       // max seconds to wait for Dolphin
    std::uint32_t dumpAddr = 0;    // for --save-dump
    std::uint32_t dumpLen  = 0;    // for --save-dump
    std::uint32_t writeAddr = 0;   // for --mem-write
    std::uint8_t  writeValue = 0;  // for --mem-write
    std::string settingsString;    // for --settings=... and --next overlay
    bool   glitched = false;       // for --next
    bool   dontLoadPrefs = false;  // skip auto-loading the saved settings string
    std::string parseError;        // non-empty -> bad args, print + exit 1
};

// Parse argv. On bad args, returns Options with non-empty parseError.
Options parseArgs(int argc, char** argv);

void printUsage(const char* progName);

// Run the requested headless mode. Returns process exit code. Takes `opts` by
// value so it can resolve the settings string from prefs (see runHeadless).
int runHeadless(Options opts);

}  // namespace tpt::cli
