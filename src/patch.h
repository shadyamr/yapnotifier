#pragma once
// Code patching in gta_sa.exe: call-site decoding (pure, tested), SEH-guarded reads and
// byte writes that only happen when the site still holds the bytes we expect.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace yap::patch {

// `bytes` = the 5 bytes at `site`. True if they are a rel32 CALL (E8); `target` gets where it goes.
bool decode_call(const uint8_t* bytes, uintptr_t site, uintptr_t& target);
// The rel32 operand a CALL at `site` needs to reach `target`.
int32_t call_rel(uintptr_t site, uintptr_t target);
// gta_sa.exe 1.0 US, from the dword at 0x401000 (plugin-sdk GameVersion.cpp: compact or hoodlum exe).
bool is_sa_10us(uint32_t dword_at_401000);

// Copies `n` bytes from `addr`; false if any of it is unreadable.
bool read(uintptr_t addr, void* out, size_t n);
// VirtualProtect + copy + restore protection + flush; false if the page can't be made writable.
bool write(uintptr_t addr, const void* data, size_t n);

// A verified patch: `with` goes in only over `expect`, and `expect` is what restoring writes back.
struct Site {
    uintptr_t addr;
    std::vector<uint8_t> expect, with;
};
bool matches(const Site& s);       // the site currently holds `expect`
bool apply(const Site& s, bool on);  // on: write `with`; off: write `expect`

}  // namespace yap::patch
