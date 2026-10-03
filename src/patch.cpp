#include "patch.h"

#include <Windows.h>

#include <cstring>

namespace yap::patch {

bool decode_call(const uint8_t* bytes, uintptr_t site, uintptr_t& target) {
    if (bytes[0] != 0xE8) return false;
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, sizeof rel);
    target = site + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
    return true;
}

int32_t call_rel(uintptr_t site, uintptr_t target) {
    return static_cast<int32_t>(static_cast<intptr_t>(target) - static_cast<intptr_t>(site + 5));
}

bool is_sa_10us(uint32_t dword_at_401000) { return dword_at_401000 == 0x53EC8B55 || dword_at_401000 == 0x16197BE9; }

bool read(uintptr_t addr, void* out, size_t n) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool write(uintptr_t addr, const void* data, size_t n) {
    void* p = reinterpret_cast<void*>(addr);
    DWORD old = 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(p, data, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return true;
}

bool matches(const Site& s) {
    std::vector<uint8_t> cur(s.expect.size());
    return read(s.addr, cur.data(), cur.size()) && cur == s.expect;
}

bool apply(const Site& s, bool on) {
    const auto& bytes = on ? s.with : s.expect;
    return write(s.addr, bytes.data(), bytes.size());
}

}  // namespace yap::patch
