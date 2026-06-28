#include "util/CrashHandler.h"

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <cstdarg>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace {

void logLine(std::FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (f) {
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
    }
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    std::FILE* f = std::fopen("tptracker_crash.log", "a");

    const auto* er = ep->ExceptionRecord;
    logLine(f, "\n=== tptracker crash ===\n");
    logLine(f, "exception code : 0x%08lX\n", er->ExceptionCode);
    logLine(f, "fault address  : %p\n", er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        er->NumberParameters >= 2) {
        logLine(f, "access violation: %s at 0x%llX\n",
                er->ExceptionInformation[0] ? "write/exec" : "read",
                static_cast<unsigned long long>(er->ExceptionInformation[1]));
    }

    const HANDLE proc   = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, NULL, TRUE);  // TRUE: enumerate loaded modules' PDBs

    CONTEXT* ctx = ep->ContextRecord;

    // A call through a null/garbage function pointer faults with RIP at the
    // bad target (often 0), so the faulting frame itself carries no symbol and
    // StackWalk can't seed from it. The CALL already pushed the return address,
    // so [RSP] points back into the *caller* — recover and report it.
    if (ctx->Rip == 0 || er->ExceptionAddress == nullptr) {
        DWORD64 retAddr = 0;
        if (!IsBadReadPtr(reinterpret_cast<void*>(ctx->Rsp), sizeof(DWORD64))) {
            retAddr = *reinterpret_cast<DWORD64*>(ctx->Rsp);
        }
        logLine(f, "null-call: return address (caller) = 0x%llX\n",
                static_cast<unsigned long long>(retAddr));
        // Seed the walk from the caller so the frames below resolve.
        if (retAddr) {
            ctx->Rip = retAddr;
            ctx->Rsp += sizeof(DWORD64);
        }
    }

    STACKFRAME64 frame{};
    frame.AddrPC.Offset    = ctx->Rip;  frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;  frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;  frame.AddrStack.Mode = AddrModeFlat;

    logLine(f, "stack:\n");
    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, ctx,
                         NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) break;

        IMAGEHLP_MODULE64 mod{};
        mod.SizeOfStruct = sizeof(mod);
        const char* modName = "?";
        if (SymGetModuleInfo64(proc, pc, &mod)) modName = mod.ModuleName;

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;
        DWORD64 symDisp = 0;

        if (SymFromAddr(proc, pc, &symDisp, sym)) {
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(proc, pc, &lineDisp, &line)) {
                logLine(f, "  #%02d %s!%s + 0x%llX   (%s:%lu)\n", i, modName,
                        sym->Name, static_cast<unsigned long long>(symDisp),
                        line.FileName, line.LineNumber);
            } else {
                logLine(f, "  #%02d %s!%s + 0x%llX\n", i, modName, sym->Name,
                        static_cast<unsigned long long>(symDisp));
            }
        } else {
            const DWORD64 base = SymGetModuleBase64(proc, pc);
            logLine(f, "  #%02d %s + 0x%llX\n", i, modName,
                    static_cast<unsigned long long>(pc - base));
        }
    }

    logLine(f, "=== end crash ===\n");
    if (f) std::fclose(f);
    return EXCEPTION_EXECUTE_HANDLER;  // terminate
}

}  // namespace

namespace tpt::util {
void installCrashHandler() { SetUnhandledExceptionFilter(crashFilter); }
}  // namespace tpt::util

#else  // !_WIN32

namespace tpt::util {
void installCrashHandler() {}
}  // namespace tpt::util

#endif
