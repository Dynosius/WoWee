// The `.cr` cache format round-trips.
//
// A module's HASH_REQUEST constants are written once and read back many
// sessions later; a writer that lays the bytes down in an order the reader
// does not expect is a cache that silently answers a challenge wrong, which a
// server rejects by closing the connection with nothing said. These pin the
// header skip, the record layout and the record count against each other.
#include <catch_amalgamated.hpp>
#include "game/warden_cr_file.hpp"

#include <atomic>
#include <cstring>
#include <openssl/sha.h>
#include <filesystem>

using namespace wowee::game;
namespace fs = std::filesystem;

namespace {
WardenCRRecord makeRecord(uint8_t tag) {
    WardenCRRecord r;
    for (int i = 0; i < 16; ++i) r.seed[i] = static_cast<uint8_t>(tag + i);
    for (int i = 0; i < 20; ++i) r.reply[i] = static_cast<uint8_t>(tag + 100 + i);
    for (int i = 0; i < 16; ++i) r.clientKey[i] = static_cast<uint8_t>(tag + 50 + i);
    for (int i = 0; i < 16; ++i) r.serverKey[i] = static_cast<uint8_t>(tag + 70 + i);
    return r;
}

std::string tempPath() {
    static std::atomic<unsigned> counter{0};
    return (fs::temp_directory_path() /
            ("wowee_cr_test_" + std::to_string(counter++) + ".cr")).string();
}
}  // namespace

TEST_CASE("a written .cr reads back byte for byte", "[wardencr]") {
    const std::string path = tempPath();
    const uint8_t opcodes[9] = {0xF3, 0xB2, 0x98, 0x11, 0x5A, 0x6C, 0x7D, 0x8E, 0x9F};
    std::vector<WardenCRRecord> records = {makeRecord(1), makeRecord(200)};

    REQUIRE(writeWardenCR(path, opcodes, records));

    uint8_t readOpcodes[9] = {};
    std::vector<WardenCRRecord> readBack;
    REQUIRE(readWardenCR(path, readOpcodes, readBack));

    CHECK(std::memcmp(opcodes, readOpcodes, 9) == 0);
    REQUIRE(readBack.size() == records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        CHECK(std::memcmp(readBack[i].seed, records[i].seed, 16) == 0);
        CHECK(std::memcmp(readBack[i].reply, records[i].reply, 20) == 0);
        CHECK(std::memcmp(readBack[i].clientKey, records[i].clientKey, 16) == 0);
        CHECK(std::memcmp(readBack[i].serverKey, records[i].serverKey, 16) == 0);
    }
    fs::remove(path);
}

TEST_CASE("the first eight header bytes are ignored on read", "[wardencr]") {
    // The writer zeroes them and the reader skips them; a reader that counted
    // them as a record would come up one short and misread every field.
    const std::string path = tempPath();
    const uint8_t opcodes[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    const std::vector<WardenCRRecord> records = {makeRecord(42)};
    REQUIRE(writeWardenCR(path, opcodes, records));

    // Header + exactly one record, nothing more.
    const auto size = fs::file_size(path);
    CHECK(size == kWardenCRHeaderSize + kWardenCRRecordSize);

    uint8_t readOpcodes[9] = {};
    std::vector<WardenCRRecord> readBack;
    REQUIRE(readWardenCR(path, readOpcodes, readBack));
    CHECK(readBack.size() == 1);
    CHECK(readOpcodes[8] == 9);
    fs::remove(path);
}

TEST_CASE("a truncated or missing .cr is refused, not half-read", "[wardencr]") {
    uint8_t opcodes[9] = {};
    std::vector<WardenCRRecord> out;

    CHECK_FALSE(readWardenCR("/no/such/warden/file.cr", opcodes, out));

    // A header with no whole record behind it has nothing to answer with.
    const std::string path = tempPath();
    {
        std::ofstream o(path, std::ios::binary);
        const uint8_t header[kWardenCRHeaderSize] = {};
        o.write(reinterpret_cast<const char*>(header), kWardenCRHeaderSize);
    }
    CHECK_FALSE(readWardenCR(path, opcodes, out));
    fs::remove(path);

    // An empty record set is not a file worth writing.
    CHECK_FALSE(writeWardenCR(tempPath(), opcodes, {}));
}

TEST_CASE("the TrinityCore/AzerothCore module answers from the built-in table", "[wardencr]") {
    uint8_t opcodes[9] = {};
    std::vector<WardenCRRecord> out;
    REQUIRE(knownWardenCR("79c0768d657977d697e10bad956cced1", opcodes, out));
    REQUIRE(out.size() == 1);

    // Those servers check the reply against SHA1 of the key the client then
    // encrypts with; a transcription slip in either is a disconnect.
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(out[0].clientKey, 16, digest);
    CHECK(std::memcmp(digest, out[0].reply, 20) == 0);

    CHECK(opcodes[0] == 0xF3);  // MEM_CHECK
    CHECK(opcodes[8] == 0x57);  // TIMING_CHECK

    CHECK_FALSE(knownWardenCR("00000000000000000000000000000000", opcodes, out));
    CHECK(out.empty());
}
