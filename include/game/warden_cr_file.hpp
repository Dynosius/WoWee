#pragma once

// The `.cr` file: a module's Warden challenge/response constants, cached.
//
// A Warden module answers the server's HASH_REQUEST with four fixed values -
// the server's seed, the 20-byte reply, and the two 16-byte RC4 keys the
// exchange switches to - and tags each cheat-check with nine module-specific
// opcodes. All of that is constant for a given module, so it is cached per
// module MD5 rather than recomputed. The format is small and fixed:
//
//   [4 memoryRead][4 pageScanCheck][9 checkOpcodes]   = 17-byte header
//   ( [16 seed][20 reply][16 clientKey][16 serverKey] ) * N  = 68 bytes each
//
// The first eight header bytes are historical and unread; the nine opcodes and
// the records are what matter. Reading and writing live here, together, so the
// two cannot drift apart - the reader skipped the first eight bytes and the
// writer has to lay down something there for it to skip.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace game {

/// One module's HASH_REQUEST answer: the seed it is asked with, the reply it
/// gives, and the RC4 keys the exchange then switches to. clientKey encrypts
/// client->server, serverKey decrypts server->client.
struct WardenCRRecord {
    uint8_t seed[16] = {};
    uint8_t reply[20] = {};
    uint8_t clientKey[16] = {};
    uint8_t serverKey[16] = {};
};

inline constexpr size_t kWardenCRHeaderSize = 17;
inline constexpr size_t kWardenCRRecordSize = 68;  // 16 + 20 + 16 + 16

/// Where the Warden cache lives on this platform. The one definition, so the
/// reader, the writer and the module cache all look in the same place.
inline std::string wardenCacheDir() {
#ifdef _WIN32
    if (const char* h = std::getenv("APPDATA")) return std::string(h) + "\\wowee\\warden_cache";
    return ".\\warden_cache";
#else
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/share/wowee/warden_cache";
    return "./warden_cache";
#endif
}

inline std::string wardenCRPath(const std::string& moduleHashHex) {
    return wardenCacheDir() + "/" + moduleHashHex + ".cr";
}

/// Read a `.cr` file. Fills the nine opcodes and the records; returns false if
/// the file is absent or too small to hold a header and at least one record.
inline bool readWardenCR(const std::string& path, uint8_t outOpcodes[9],
                         std::vector<WardenCRRecord>& outRecords) {
    outRecords.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < static_cast<std::streamoff>(kWardenCRHeaderSize + kWardenCRRecordSize)) return false;

    in.seekg(8);  // skip memoryRead + pageScanCheck
    in.read(reinterpret_cast<char*>(outOpcodes), 9);

    const size_t count =
        (static_cast<size_t>(size) - kWardenCRHeaderSize) / kWardenCRRecordSize;
    outRecords.resize(count);
    for (auto& r : outRecords) {
        in.read(reinterpret_cast<char*>(r.seed), 16);
        in.read(reinterpret_cast<char*>(r.reply), 20);
        in.read(reinterpret_cast<char*>(r.clientKey), 16);
        in.read(reinterpret_cast<char*>(r.serverKey), 16);
    }
    return in.good() && !outRecords.empty();
}

/// Write a `.cr` file. The eight historical header bytes are written as zero,
/// which is what the reader skips. Returns false if the path cannot be opened.
inline bool writeWardenCR(const std::string& path, const uint8_t opcodes[9],
                          const std::vector<WardenCRRecord>& records) {
    if (records.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    const uint8_t zero8[8] = {};
    out.write(reinterpret_cast<const char*>(zero8), 8);
    out.write(reinterpret_cast<const char*>(opcodes), 9);
    for (const auto& r : records) {
        out.write(reinterpret_cast<const char*>(r.seed), 16);
        out.write(reinterpret_cast<const char*>(r.reply), 20);
        out.write(reinterpret_cast<const char*>(r.clientKey), 16);
        out.write(reinterpret_cast<const char*>(r.serverKey), 16);
    }
    return out.good();
}

}  // namespace game
}  // namespace wowee
