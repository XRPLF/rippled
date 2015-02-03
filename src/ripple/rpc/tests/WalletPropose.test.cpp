//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <beast/unit_test.h>

namespace ripple {

namespace RPC {

struct wallet_strings
{
    char const* account_id;
    char const* master_key;
    char const* master_seed;
    char const* master_seed_hex;
    char const* public_key;
    char const* public_key_hex;
};

#define PASSPHRASE       "REINDEER FLOTILLA"
#define MASTER_KEY       "SCAT BERN ISLE FOR ROIL BUS SOAK AQUA FREE FOR DRAM BRIG"
#define MASTER_SEED      "snMwVWs2hZzfDUF3p2tHZ3EgmyhFs"
#define MASTER_SEED_HEX  "BE6A670A19B209E112146D0A7ED2AAD7"

#define EXPECT_WALLET_FIELD(r, s, f)  expect (r[#f] == s.f, r[#f].asString())

static wallet_strings const secp256k1_strings =
{
    "r4Vtj2jrfmTVZGfSP3gH9hQPMqFPQFin8f",
    MASTER_KEY,
    MASTER_SEED,
    MASTER_SEED_HEX,
    "aBQxK2YFNqzmAaXNczYcjqDjfiKkLsJUizsr1UBf44RCF8FHdrmX",
    "038AAE247B2344B1837FBED8F57389C8C11774510A3F7D784F2A09F0CB6843236C",
};

class WalletPropose_test : public beast::unit_test::suite
{
public:
    void testRandom()
    {
        Json::Value params;

        Json::Value result = WalletPropose (params);

        expect (! contains_error (result));
        expect (result.isMember ("account_id"     ));
        expect (result.isMember ("master_key"     ));
        expect (result.isMember ("master_seed"    ));
        expect (result.isMember ("master_seed_hex"));
        expect (result.isMember ("public_key"     ));
        expect (result.isMember ("public_key_hex" ));

        std::string seed = result["master_seed"].asString();

        result = WalletPropose (params);

        expect (result["master_seed"].asString() != seed);
    }

    void testReindeerFlotilla(Json::Value const& params, wallet_strings const& s)
    {
        Json::Value result = WalletPropose (params);

        expect (! contains_error (result) );
        EXPECT_WALLET_FIELD (result, s, account_id);
        EXPECT_WALLET_FIELD (result, s, master_key);
        EXPECT_WALLET_FIELD (result, s, master_seed);
        EXPECT_WALLET_FIELD (result, s, master_seed_hex);
        EXPECT_WALLET_FIELD (result, s, public_key);
        EXPECT_WALLET_FIELD (result, s, public_key_hex);
    }

    void testLegacyPassphrase(char const* value)
    {
        testcase (value);

        Json::Value params;
        params["passphrase"] = value;

        testReindeerFlotilla (params, secp256k1_strings);
    }

    void testLegacyPassphrase()
    {
        testLegacyPassphrase (PASSPHRASE);
        testLegacyPassphrase (secp256k1_strings.master_key);
        testLegacyPassphrase (secp256k1_strings.master_seed);
        testLegacyPassphrase (secp256k1_strings.master_seed_hex);
    }

    void run()
    {
        testRandom();
        testLegacyPassphrase();
    }
};

#undef PASSPHRASE
#undef MASTER_KEY
#undef MASTER_SEED
#undef MASTER_SEED_HEX
#undef EXPECT_WALLET_FIELD

BEAST_DEFINE_TESTSUITE(WalletPropose,ripple_basics,ripple);

} // RPC
} // ripple
