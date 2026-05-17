#pragma once

#include <cstdint>
#include <string>

#include "memory/MemorySource.h"

namespace tpt::dolphin {

enum class Status {
    Unhooked,    // not connected
    NoEmu,       // Dolphin running, but no game booted
    NotRunning,  // Dolphin not running
    Hooked,      // connected and game running
};

// Wrapper around DolphinComm::DolphinAccessor. Addresses passed to read* are
// console virtual addresses (e.g. 0x804061C0 for the US TP save block) — the
// wrapper converts to Dolphin's internal offset.
//
// Implements the generic memory::MemorySource interface so code in core/
// can read state without knowing it's reading from Dolphin specifically.
class Client : public tpt::memory::MemorySource {
public:
    Client();
    ~Client() override;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // MemorySource interface
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return refresh() == Status::Hooked; }
    std::string sourceName() const override { return "Dolphin"; }
    std::string gameId() const override;
    bool readBytes(std::uint32_t addr, void* out, std::size_t size) const override;
    bool writeBytes(std::uint32_t addr, const void* data, std::size_t size) const override;

    // Dolphin-specific status detail. The interface only exposes a binary
    // isConnected(); for richer UI like "Dolphin running, no game booted"
    // call status() through the concrete type.
    Status refresh() const;
    Status status() const { return refresh(); }
};

const char* toString(Status s);

}  // namespace tpt::dolphin
