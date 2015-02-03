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
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ripple/rpc/impl/KeypairForSignature.h>

namespace ripple {

namespace RPC {

struct key_strings
{
    char const* account_id;
    char const* master_key;
    char const* master_seed;
    char const* master_seed_hex;
    char const* public_key;
    char const* public_key_hex;
    char const* secret_key_hex;
};

namespace common {
static char const* passphrase = "REINDEER FLOTILLA";
static char const* master_key = "SCAT BERN ISLE FOR ROIL BUS SOAK AQUA FREE FOR DRAM BRIG";
static char const* master_seed = "snMwVWs2hZzfDUF3p2tHZ3EgmyhFs";
static char const* master_seed_hex = "BE6A670A19B209E112146D0A7ED2AAD7";
}

static key_strings const secp256k1_strings =
{
    "r4Vtj2jrfmTVZGfSP3gH9hQPMqFPQFin8f",
    common::master_key,
    common::master_seed,
    common::master_seed_hex,
    "aBQxK2YFNqzmAaXNczYcjqDjfiKkLsJUizsr1UBf44RCF8FHdrmX",
    "038AAE247B2344B1837FBED8F57389C8C11774510A3F7D784F2A09F0CB6843236C",
    "1949ECD889EA71324BC7A30C8E81F4E93CB73EE19D59E9082111E78CC3DDABC2",
};

class WalletPropose_test : public ripple::TestSuite
{
public:
    void testRandomWallet()
    {
        Json::Value params;
        Json::Value result = walletPropose (params);

        expect (! contains_error (result));
        expect (result.isMember ("account_id"));
        expect (result.isMember ("master_key"));
        expect (result.isMember ("master_seed"));
        expect (result.isMember ("master_seed_hex"));
        expect (result.isMember ("public_key"));
        expect (result.isMember ("public_key_hex"));

        std::string seed = result["master_seed"].asString();

        result = walletPropose (params);

        // We asked for two random seeds, so they shouldn't match.
        expect (result["master_seed"].asString() != seed, seed);
    }

    void testSecretWallet (Json::Value const& params, key_strings const& s)
    {
        Json::Value result = walletPropose (params);

        expect (! contains_error (result));
        expectEquals (result["account_id"], s.account_id);
        expectEquals (result["master_key"], s.master_key);
        expectEquals (result["master_seed"], s.master_seed);
        expectEquals (result["master_seed_hex"], s.master_seed_hex);
        expectEquals (result["public_key"], s.public_key);
        expectEquals (result["public_key_hex"], s.public_key_hex);
    }

    void testLegacyPassphrase (char const* value)
    {
        testcase (value);

        Json::Value params;
        params["passphrase"] = value;

        testSecretWallet (params, secp256k1_strings);
    }

    void testLegacyPassphrase()
    {
        testLegacyPassphrase (common::passphrase);
        testLegacyPassphrase (secp256k1_strings.master_key);
        testLegacyPassphrase (secp256k1_strings.master_seed);
        testLegacyPassphrase (secp256k1_strings.master_seed_hex);
    }

    void run()
    {
        testRandomWallet();
        testLegacyPassphrase();
    }
};

class KeypairForSignature_test : public ripple::TestSuite
{
public:
    void testEmpty()
    {
        Json::Value params;
        Json::Value error;

        (void) keypairForSignature (params, error);

        expect (contains_error (error) );
    }

    void testSecretWallet (Json::Value const& params, key_strings const& s)
    {
        Json::Value error;
        KeyPair keypair = keypairForSignature (params, error);

        uint256 secret_key = keypair.secretKey.getAccountPrivate();
        Blob    public_key = keypair.publicKey.getAccountPublic();

        std::string secret_key_hex = strHex (secret_key.data(), secret_key.size());
        std::string public_key_hex = strHex (public_key.data(), public_key.size());

        expectEquals (secret_key_hex, s.secret_key_hex);
        expectEquals (public_key_hex, s.public_key_hex);
    }

    void testLegacySecret (char const* value)
    {
        testcase (value);

        Json::Value params;
        params["secret"] = value;

        testSecretWallet (params, secp256k1_strings);
    }

    void testLegacySecret()
    {
        testLegacySecret (common::passphrase);
        testLegacySecret (secp256k1_strings.master_key);
        testLegacySecret (secp256k1_strings.master_seed);
        testLegacySecret (secp256k1_strings.master_seed_hex);
    }

    void run()
    {
        testEmpty();
        testLegacySecret();
    }
};

BEAST_DEFINE_TESTSUITE(WalletPropose,ripple_basics,ripple);
BEAST_DEFINE_TESTSUITE(KeypairForSignature,ripple_basics,ripple);

} // RPC
} // ripple
