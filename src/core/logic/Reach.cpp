#include "core/logic/Reach.h"

#include "core/logic/Evaluator.h"

namespace tpt::core::logic {

const std::unordered_set<std::string>&
reach(const RoomMap& rooms, Context& ctx,
      const std::vector<std::string>& startRooms,
      const std::vector<std::string>& warpRooms) {
    ctx.reachedRooms.clear();
    for (const auto& s : startRooms) ctx.reachedRooms.insert(s);
    for (const auto& w : warpRooms)  ctx.reachedRooms.insert(w);

    // Iterate to fixpoint — `Room.X` inside requirements depends on the
    // current reached set, so single-pass BFS isn't enough.
    while (true) {
        bool added = false;
        // Snapshot to avoid iterator invalidation while mutating reachedRooms.
        const std::vector<std::string> frontier(
            ctx.reachedRooms.begin(), ctx.reachedRooms.end());
        for (const auto& roomName : frontier) {
            const auto it = rooms.find(roomName);
            if (it == rooms.end()) continue;
            for (const auto& exit : it->second.exits) {
                if (ctx.reachedRooms.count(exit.target)) continue;
                const auto& expr = ctx.glitched ? exit.reqGlitched : exit.req;
                bool ok = false;
                try { ok = evalExpr(expr, ctx); }
                catch (...) { ok = false; }
                if (ok) {
                    ctx.reachedRooms.insert(exit.target);
                    added = true;
                }
            }
        }
        if (!added) break;
    }
    return ctx.reachedRooms;
}

std::vector<std::string>
pendingInReach(const RoomMap& rooms, const CheckMap& checks,
               const std::unordered_set<std::string>& reached,
               const std::unordered_set<std::string>& completed,
               const Context& ctx) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& roomName : reached) {
        const auto it = rooms.find(roomName);
        if (it == rooms.end()) continue;
        for (const auto& checkName : it->second.checks) {
            if (checkName.empty() || seen.count(checkName) || completed.count(checkName)) continue;
            seen.insert(checkName);
            const auto cIt = checks.find(checkName);
            if (cIt == checks.end()) {
                // Unknown check — include anyway (mirrors Python).
                out.push_back(checkName);
                continue;
            }
            const auto& expr = ctx.glitched ? cIt->second.reqGlitched : cIt->second.req;
            try {
                if (evalExpr(expr, ctx)) out.push_back(checkName);
            } catch (...) { /* skip on parse/eval failure */ }
        }
    }
    return out;
}

}  // namespace tpt::core::logic
