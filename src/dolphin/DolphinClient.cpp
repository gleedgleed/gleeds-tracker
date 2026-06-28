#include "dolphin/DolphinClient.h"

#include "Common/CommonUtils.h"
#include "DolphinProcess/DolphinAccessor.h"

namespace tpt::dolphin {

namespace {

Status mapStatus(DolphinComm::DolphinAccessor::DolphinStatus s) {
    using DS = DolphinComm::DolphinAccessor::DolphinStatus;
    switch (s) {
        case DS::hooked:     return Status::Hooked;
        case DS::noEmu:      return Status::NoEmu;
        case DS::notRunning: return Status::NotRunning;
        case DS::unHooked:   return Status::Unhooked;
    }
    return Status::Unhooked;
}

}  // namespace

Client::Client() {
    DolphinComm::DolphinAccessor::init();
}

Client::~Client() {
    // NOT DolphinAccessor::free(): upstream's free() does `delete m_instance`
    // but leaves the pointer dangling (non-null). Because the app recreates the
    // memory source on hook-retry ticks, the next Client's init() then sees a
    // non-null m_instance and calls reset()/readFromRAM through the freed
    // object — an intermittent use-after-free that crashes with RIP=0 when the
    // heap has reused the memory. unHook() deletes *and* nulls the pointer, so
    // the next init() allocates a fresh instance instead.
    DolphinComm::DolphinAccessor::unHook();
}

bool Client::connect() {
    DolphinComm::DolphinAccessor::hook();
    return refresh() == Status::Hooked;
}

void Client::disconnect() {
    DolphinComm::DolphinAccessor::unHook();
}

Status Client::refresh() const {
    return mapStatus(DolphinComm::DolphinAccessor::getStatus());
}

std::string Client::gameId() const {
    if (refresh() != Status::Hooked) return {};
    // Don't trust DolphinAccessor::getGameID() — upstream populates s_gameID
    // inside hook() *before* it calls UpdateMemoryValues(), so the address
    // translation in readGameID() sees s_mem1_end=0 and silently fails. By
    // the time we call this getter, our readBytes works fine, so just read
    // it ourselves.
    char id[7] = {};
    if (!readBytes(0x80000000u, id, 6)) return {};
    for (char& c : id) {
        if (c != 0 && (c < 0x20 || c > 0x7E)) c = '?';
    }
    return std::string(id, 6);
}

bool Client::readBytes(std::uint32_t addr, void* out, std::size_t size) const {
    if (refresh() != Status::Hooked) return false;
    const u32 offset = Common::dolphinAddrToOffset(
        addr, DolphinComm::DolphinAccessor::isARAMAccessible());
    return DolphinComm::DolphinAccessor::readFromRAM(
        offset, static_cast<char*>(out), size, /*withBSwap=*/false);
}

bool Client::writeBytes(std::uint32_t addr, const void* data, std::size_t size) const {
    if (refresh() != Status::Hooked) return false;
    const u32 offset = Common::dolphinAddrToOffset(
        addr, DolphinComm::DolphinAccessor::isARAMAccessible());
    return DolphinComm::DolphinAccessor::writeToRAM(
        offset, static_cast<const char*>(data), size, /*withBSwap=*/false);
}

const char* toString(Status s) {
    switch (s) {
        case Status::Hooked:     return "hooked";
        case Status::NoEmu:      return "Dolphin up, no game booted";
        case Status::NotRunning: return "Dolphin not running";
        case Status::Unhooked:   return "unhooked";
    }
    return "?";
}

}  // namespace tpt::dolphin
