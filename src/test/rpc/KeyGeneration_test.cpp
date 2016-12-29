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
#include <test/support/TestSuite.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ripple/rpc/impl/RPCHelpers.h>

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

static key_strings const ed25519_strings =
{
    "r4qV6xTXerqaZav3MJfSY79ynmc1BSBev1",
    common::master_key,
    common::master_seed,
    common::master_seed_hex,
    "aKEQmgLMyZPMruJFejUuedp169LgW6DbJt1rej1DJ5hWUMH4pHJ7",
    "ED54C3F5BEDA8BD588B203D23A27398FAD9D20F88A974007D6994659CD7273FE1D",
    "77AAED2698D56D6676323629160F4EEF21CFD9EE3D0745CC78FA291461F98278",
};

class WalletPropose_test : public ripple::TestSuite
{
public:
    void testRandomWallet(boost::optional<std::string> const& keyType)
    {
        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        Json::Value result = walletPropose (params);

        BEAST_EXPECT(! contains_error (result));
        BEAST_EXPECT(result.isMember (jss::account_id));
        BEAST_EXPECT(result.isMember (jss::master_key));
        BEAST_EXPECT(result.isMember (jss::master_seed));
        BEAST_EXPECT(result.isMember (jss::master_seed_hex));
        BEAST_EXPECT(result.isMember (jss::public_key));
        BEAST_EXPECT(result.isMember (jss::public_key_hex));
        BEAST_EXPECT(result.isMember (jss::key_type));

        expectEquals (result[jss::key_type],
            params.isMember (jss::key_type) ? params[jss::key_type]
                                            : Json::Value{"secp256k1"});

        std::string seed = result[jss::master_seed].asString();

        result = walletPropose (params);

        // We asked for two random seeds, so they shouldn't match.
        BEAST_EXPECT(result[jss::master_seed].asString() != seed);
    }

    void testSecretWallet (Json::Value const& params, key_strings const& s)
    {
        Json::Value result = walletPropose (params);

        BEAST_EXPECT(! contains_error (result));
        expectEquals (result[jss::account_id], s.account_id);
        expectEquals (result[jss::master_key], s.master_key);
        expectEquals (result[jss::master_seed], s.master_seed);
        expectEquals (result[jss::master_seed_hex], s.master_seed_hex);
        expectEquals (result[jss::public_key], s.public_key);
        expectEquals (result[jss::public_key_hex], s.public_key_hex);
        expectEquals (result[jss::key_type],
            params.isMember (jss::key_type) ? params[jss::key_type]
                                            : Json::Value{"secp256k1"});
    }

    void testSeed (boost::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase ("seed");

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.master_seed;

        testSecretWallet (params, strings);
    }

    void testSeedHex (boost::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase ("seed_hex");

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed_hex] = strings.master_seed_hex;

        testSecretWallet (params, strings);
    }

    void testLegacyPassphrase (char const* value,
        boost::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::passphrase] = value;

        testSecretWallet (params, strings);
    }

    void testLegacyPassphrase(boost::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase ("passphrase");

        testLegacyPassphrase (common::passphrase, keyType, strings);
        testLegacyPassphrase (strings.master_key, keyType, strings);
        testLegacyPassphrase (strings.master_seed, keyType, strings);
        testLegacyPassphrase (strings.master_seed_hex, keyType, strings);
    }

    void testKeyType (boost::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase (keyType ? *keyType : "no key_type");

        testRandomWallet (keyType);
        testSeed (keyType, strings);
        testSeedHex (keyType, strings);
        testLegacyPassphrase (keyType, strings);

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.master_seed;
        params[jss::seed_hex] = strings.master_seed_hex;

        // Secret fields are mutually exclusive.
        BEAST_EXPECT(contains_error (walletPropose (params)));
    }

    void testBadInput ()
    {
        testcase ("Bad inputs");

        // Passing non-strings where strings are required
        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 20160506;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::objectValue;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid field 'seed', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = "ed25519";
            params[jss::seed_hex] = Json::arrayValue;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
        }

        // Specifying multiple items at once
        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = common::master_key;
            params[jss::seed_hex] = common::master_seed_hex;
            params[jss::seed] = common::master_seed;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Exactly one of the following must be specified: passphrase, seed or seed_hex");
        }

        // Specifying bad key types:
        {
            Json::Value params;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::master_key;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid parameters.");
        }

        {
            Json::Value params;
            params[jss::key_type] = Json::objectValue;
            params[jss::seed_hex] = common::master_seed_hex;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid field 'key_type', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = Json::arrayValue;
            params[jss::seed] = common::master_seed;
            auto result = walletPropose (params);
            BEAST_EXPECT(contains_error (result));
            BEAST_EXPECT(result[jss::error_message] ==
                "Invalid field 'key_type', not string.");
        }
    }

    void testKeypairForSignature (
        boost::optional<std::string> keyType,
        key_strings const& strings)
    {
        testcase ("keypairForSignature - " +
            (keyType ? *keyType : "no key_type"));

        auto const publicKey = parseBase58<PublicKey>(
            TokenType::TOKEN_ACCOUNT_PUBLIC, strings.public_key);
        BEAST_EXPECT(publicKey);

        if (!keyType)
        {
            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_seed;

                auto ret = keypairForSignature (params, error);
                BEAST_EXPECT(! contains_error (error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_seed_hex;

                auto ret = keypairForSignature (params, error);
                BEAST_EXPECT(! contains_error (error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_key;

                auto ret = keypairForSignature (params, error);
                BEAST_EXPECT(! contains_error (error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            keyType.emplace ("secp256k1");
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed] = strings.master_seed;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(! contains_error (error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed_hex] = strings.master_seed_hex;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(! contains_error (error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::passphrase] = strings.master_key;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(! contains_error (error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }
    }

    void testKeypairForSignatureErrors()
    {
        // Specify invalid "secret"
        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = 314159265;
            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'secret', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = Json::arrayValue;
            params[jss::secret].append ("array:0");

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'secret', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = Json::objectValue;
            params[jss::secret]["string"] = "string";
            params[jss::secret]["number"] = 702;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(ret.first.size() == 0);
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'secret', not string.");
        }

        // Specify "secret" and "key_type"
        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "ed25519";
            params[jss::secret] = common::master_seed;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "The secret field is not allowed if key_type is used.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify unknown or bad "key_type"
        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::master_key;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'key_type'.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = Json::objectValue;
            params[jss::seed_hex] = common::master_seed_hex;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'key_type', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = Json::arrayValue;
            params[jss::seed] = common::master_seed;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'key_type', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify non-string passphrase
        { // not a passphrase: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 1234567890;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a passphrase: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = Json::objectValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a passphrase: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = Json::arrayValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a passphrase: empty string
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = "";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }


        // Specify non-string or invalid seed
        { // not a seed: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = 443556;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a string: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::objectValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a string: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::arrayValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a seed: empty
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a seed: invalid characters
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "s M V s h z D F p t Z E m h s";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a seed: random string
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "pnnjkbnobnml43679nbvjdsklnbjs";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify non-string or invalid seed_hex
        { // not a string: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = 443556;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a string: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = Json::objectValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not a string: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = Json::arrayValue;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // empty
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // short
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "A670A19B";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // not hex
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = common::passphrase;

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        { // overlong
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "BE6A670A19B209E112146D0A7ED2AAD72567D0FC913";

            auto ret = keypairForSignature (params, error);
            BEAST_EXPECT(contains_error (error));
            BEAST_EXPECT(error[jss::error_message] ==
                "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }
    }

    void run()
    {
        testKeyType (boost::none, secp256k1_strings);
        testKeyType (std::string("secp256k1"), secp256k1_strings);
        testKeyType (std::string("ed25519"), ed25519_strings);
        testBadInput ();

        testKeypairForSignature (boost::none, secp256k1_strings);
        testKeypairForSignature (std::string("secp256k1"), secp256k1_strings);
        testKeypairForSignature (std::string("ed25519"), ed25519_strings);
        testKeypairForSignatureErrors ();
    }
};

BEAST_DEFINE_TESTSUITE(WalletPropose,ripple_basics,ripple);

} // RPC
} // ripple
