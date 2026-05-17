#pragma once

#include <string_view>

namespace tpt::core {

// Look up the friendly description for a Twilight Princess internal stage
// code (the 8-byte string the game writes into the save block as
// `currentStage`, e.g. "F_SP108", "R_SP01"). Returns the input `code`
// unchanged on miss so call sites can render the raw code as a fallback.
//
// The backing table is a verbatim duplicate of libtp_rel's
// `data/stage::allStages` / `data/stage::stageDescs` parallel arrays,
// hand-copied so tptracker has no source dependency on libtp_rel.
std::string_view friendlyStageName(std::string_view code);

}  // namespace tpt::core
