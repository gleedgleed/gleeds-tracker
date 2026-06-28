#pragma once

namespace tpt::util {

// Installs a process-wide unhandled-exception filter that, on a hard crash
// (access violation, etc.), writes a symbolized stack trace to
// `tptracker_crash.log` (in the working directory) and stderr. No-op on
// non-Windows. Call once at the very start of main().
void installCrashHandler();

}  // namespace tpt::util
