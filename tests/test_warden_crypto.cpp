// A Warden cipher state handed over by a module ciphers like the keys it came from.
//
// A module that answers HASH_REQUEST itself hands the client the new cipher
// as raw RC4 state, client->server first. Installed the wrong way round, or a
// byte off, every packet after the exchange is noise to the server, which
// drops the connection without saying why.
#include <catch_amalgamated.hpp>
#include "game/warden_crypto.hpp"

#include <utility>

using wowee::game::WardenCrypto;

namespace {
// RC4 key schedule, laid out as a module hands it over: [256 S][i][j].
void appendState(std::vector<uint8_t>& out, const std::vector<uint8_t>& key) {
    std::vector<uint8_t> s(256);
    for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + s[i] + key[i % key.size()]) & 0xFF;
        std::swap(s[i], s[j]);
    }
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    out.push_back(0);
}
}  // namespace

TEST_CASE("an installed RC4 state ciphers as its keys do", "[wardencrypto]") {
    const std::vector<uint8_t> sessionKey(40, 0x42);
    const std::vector<uint8_t> clientKey = {0x7F, 0x96, 0xEE, 0xFD, 0xA5, 0xB6, 0x3D, 0x20,
                                            0xA4, 0xDF, 0x8E, 0x00, 0xCB, 0xF4, 0x83, 0x04};
    const std::vector<uint8_t> serverKey = {0xC2, 0xB7, 0xAD, 0xED, 0xFC, 0xCC, 0xA9, 0xC2,
                                            0xBF, 0xB3, 0xF8, 0x56, 0x02, 0xBA, 0x80, 0x9B};

    WardenCrypto viaKeys;
    REQUIRE(viaKeys.initFromSessionKey(sessionKey));
    viaKeys.replaceKeys(clientKey, serverKey);

    std::vector<uint8_t> pair;
    appendState(pair, clientKey);
    appendState(pair, serverKey);
    WardenCrypto viaState;
    REQUIRE(viaState.initFromSessionKey(sessionKey));
    REQUIRE(viaState.replaceState(pair));

    const std::vector<uint8_t> probe = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    CHECK(viaState.encrypt(probe) == viaKeys.encrypt(probe));
    CHECK(viaState.decrypt(probe) == viaKeys.decrypt(probe));
}

TEST_CASE("a state of the wrong size is refused and the cipher left alone", "[wardencrypto]") {
    const std::vector<uint8_t> sessionKey(40, 0x42);
    WardenCrypto before, after;
    REQUIRE(before.initFromSessionKey(sessionKey));
    REQUIRE(after.initFromSessionKey(sessionKey));

    CHECK_FALSE(after.replaceState(std::vector<uint8_t>(515, 0)));

    const std::vector<uint8_t> probe(16, 0xA5);
    CHECK(after.encrypt(probe) == before.encrypt(probe));
}
