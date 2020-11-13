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

#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/handlers/WalletPropose.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx/TestSuite.h>

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
    char const* passphrase;
    char const* passphrase_warning;
};

namespace common {
static char const* passphrase = "REINDEER FLOTILLA";
static char const* master_key =
    "SCAT BERN ISLE FOR ROIL BUS SOAK AQUA FREE FOR DRAM BRIG";
static char const* master_seed = "snMwVWs2hZzfDUF3p2tHZ3EgmyhFs";
static char const* master_seed_hex = "BE6A670A19B209E112146D0A7ED2AAD7";
}  // namespace common

static key_strings const secp256k1_strings = {
    "r4Vtj2jrfmTVZGfSP3gH9hQPMqFPQFin8f",
    common::master_key,
    common::master_seed,
    common::master_seed_hex,
    "aBQxK2YFNqzmAaXNczYcjqDjfiKkLsJUizsr1UBf44RCF8FHdrmX",
    "038AAE247B2344B1837FBED8F57389C8C11774510A3F7D784F2A09F0CB6843236C",
    "1949ECD889EA71324BC7A30C8E81F4E93CB73EE19D59E9082111E78CC3DDABC2",
    common::passphrase,
    "This wallet was generated using a user-supplied "
    "passphrase that has low entropy and is vulnerable "
    "to brute-force attacks.",
};

static key_strings const ed25519_strings = {
    "r4qV6xTXerqaZav3MJfSY79ynmc1BSBev1",
    common::master_key,
    common::master_seed,
    common::master_seed_hex,
    "aKEQmgLMyZPMruJFejUuedp169LgW6DbJt1rej1DJ5hWUMH4pHJ7",
    "ED54C3F5BEDA8BD588B203D23A27398FAD9D20F88A974007D6994659CD7273FE1D",
    "77AAED2698D56D6676323629160F4EEF21CFD9EE3D0745CC78FA291461F98278",
    common::passphrase,
    "This wallet was generated using a user-supplied "
    "passphrase that has low entropy and is vulnerable "
    "to brute-force attacks.",
};

static key_strings const strong_brain_strings = {
    "rBcvXmNb7KPkNdMkpckdWPpbvkWgcV3nir",
    "TED AVON CAVE HOUR BRAG JEFF RIFT NEAL TOLD FAT SEW SAN",
    "shKdhWka8hS7Es3bpctCZXBiAwfUN",
    "74BA8389B44F98CF41E795CD91F9C93F",
    "aBRL2sqVuzrsM6zikPB4v8UBHGn1aKkrsxhYEffhcQxB2LKyywE5",
    "03BD334FB9E06C58D69603E9922686528B18A754BC2F2E1ADA095FFE67DE952C64",
    "84262FB16AA25BE407174C7EDAB531220C30FA4D8A28AA9D564673FB3D34502C",
    "A4yKIRGdzrw0YQ$2%TFKYG9HP*&ok^!sy7E@RwICs",
    "This wallet was generated using a user-supplied "
    "passphrase. It may be vulnerable to brute-force "
    "attacks.",
};

class WalletPropose_test : public ripple::TestSuite
{
public:
    void
    testRandomWallet(std::optional<std::string> const& keyType)
    {
        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        Json::Value result = walletPropose(params);

        BEAST_EXPECT(!contains_error(result));
        BEAST_EXPECT(result.isMember(jss::account_id));
        BEAST_EXPECT(result.isMember(jss::master_seed));
        BEAST_EXPECT(result.isMember(jss::master_seed_hex));
        BEAST_EXPECT(result.isMember(jss::public_key));
        BEAST_EXPECT(result.isMember(jss::public_key_hex));
        BEAST_EXPECT(result.isMember(jss::key_type));

        expectEquals(
            result[jss::key_type],
            params.isMember(jss::key_type) ? params[jss::key_type]
                                           : "secp256k1");
        BEAST_EXPECT(!result.isMember(jss::warning));

        std::string seed = result[jss::master_seed].asString();

        result = walletPropose(params);

        // We asked for two random seeds, so they shouldn't match.
        BEAST_EXPECT(result[jss::master_seed].asString() != seed);
    }

    Json::Value
    testSecretWallet(Json::Value const& params, key_strings const& s)
    {
        Json::Value result = walletPropose(params);

        BEAST_EXPECT(!contains_error(result));
        expectEquals(result[jss::account_id], s.account_id);
        expectEquals(result[jss::master_seed], s.master_seed);
        expectEquals(result[jss::master_seed_hex], s.master_seed_hex);
        expectEquals(result[jss::public_key], s.public_key);
        expectEquals(result[jss::public_key_hex], s.public_key_hex);
        expectEquals(
            result[jss::key_type],
            params.isMember(jss::key_type) ? params[jss::key_type]
                                           : "secp256k1");
        return result;
    }

    void
    testSeed(
        std::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase("seed");

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.master_seed;

        auto const wallet = testSecretWallet(params, strings);
        BEAST_EXPECT(!wallet.isMember(jss::warning));
    }

    void
    testSeedHex(
        std::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase("seed_hex");

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed_hex] = strings.master_seed_hex;

        auto const wallet = testSecretWallet(params, strings);
        BEAST_EXPECT(!wallet.isMember(jss::warning));
    }

    void
    testLegacyPassphrase(
        char const* value,
        std::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::passphrase] = value;

        auto const wallet = testSecretWallet(params, strings);
        if (value == strings.passphrase)
            BEAST_EXPECT(wallet[jss::warning] == strings.passphrase_warning);
        else
            BEAST_EXPECT(!wallet.isMember(jss::warning));
    }

    void
    testLegacyPassphrase(
        std::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase("passphrase");

        testLegacyPassphrase(strings.passphrase, keyType, strings);
        testLegacyPassphrase(strings.master_key, keyType, strings);
        testLegacyPassphrase(strings.master_seed, keyType, strings);
        testLegacyPassphrase(strings.master_seed_hex, keyType, strings);
    }

    void
    testKeyType(
        std::optional<std::string> const& keyType,
        key_strings const& strings)
    {
        testcase(keyType ? *keyType : "no key_type");

        testRandomWallet(keyType);
        testSeed(keyType, strings);
        testSeedHex(keyType, strings);
        testLegacyPassphrase(keyType, strings);

        Json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.master_seed;
        params[jss::seed_hex] = strings.master_seed_hex;

        // Secret fields are mutually exclusive.
        BEAST_EXPECT(contains_error(walletPropose(params)));
    }

    void
    testBadInput()
    {
        testcase("Bad inputs");

        // Passing non-strings where strings are required
        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 20160506;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::objectValue;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Invalid field 'seed', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = "ed25519";
            params[jss::seed_hex] = Json::arrayValue;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
        }

        // Specifying multiple items at once
        {
            Json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = common::master_key;
            params[jss::seed_hex] = common::master_seed_hex;
            params[jss::seed] = common::master_seed;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Exactly one of the following must be specified: passphrase, "
                "seed or seed_hex");
        }

        // Specifying bad key types:
        {
            Json::Value params;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::master_key;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value params;
            params[jss::key_type] = Json::objectValue;
            params[jss::seed_hex] = common::master_seed_hex;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Invalid field 'key_type', not string.");
        }

        {
            Json::Value params;
            params[jss::key_type] = Json::arrayValue;
            params[jss::seed] = common::master_seed;
            auto result = walletPropose(params);
            BEAST_EXPECT(contains_error(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Invalid field 'key_type', not string.");
        }
    }

    void
    testKeypairForSignature(
        std::optional<std::string> keyType,
        key_strings const& strings)
    {
        testcase(
            "keypairForSignature - " + (keyType ? *keyType : "no key_type"));

        auto const publicKey = parseBase58<PublicKey>(
            TokenType::AccountPublic, strings.public_key);
        BEAST_EXPECT(publicKey);

        if (!keyType)
        {
            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_seed;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!contains_error(error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_seed_hex;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!contains_error(error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            {
                Json::Value params;
                Json::Value error;
                params[jss::secret] = strings.master_key;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!contains_error(error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(ret.first == publicKey);
            }

            keyType.emplace("secp256k1");
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed] = strings.master_seed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!contains_error(error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed_hex] = strings.master_seed_hex;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!contains_error(error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }

        {
            Json::Value params;
            Json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::passphrase] = strings.master_key;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!contains_error(error));
            BEAST_EXPECT(ret.first.size() != 0);
            BEAST_EXPECT(ret.first == publicKey);
        }
    }

    void
    testKeypairForSignatureErrors()
    {
        // Specify invalid "secret"
        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = 314159265;
            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'secret', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = Json::arrayValue;
            params[jss::secret].append("array:0");

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'secret', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::secret] = Json::objectValue;
            params[jss::secret]["string"] = "string";
            params[jss::secret]["number"] = 702;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(ret.first.size() == 0);
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'secret', not string.");
        }

        // Specify "secret" and "key_type"
        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "ed25519";
            params[jss::secret] = common::master_seed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "The secret field is not allowed if key_type is used.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify unknown or bad "key_type"
        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::master_key;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] == "Invalid field 'key_type'.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = Json::objectValue;
            params[jss::seed_hex] = common::master_seed_hex;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'key_type', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = Json::arrayValue;
            params[jss::seed] = common::master_seed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'key_type', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify non-string passphrase
        {  // not a passphrase: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 1234567890;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a passphrase: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = Json::objectValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a passphrase: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = Json::arrayValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'passphrase', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a passphrase: empty string
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify non-string or invalid seed
        {  // not a seed: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = 443556;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a string: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::objectValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a string: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = Json::arrayValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a seed: empty
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a seed: invalid characters
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "s M V s h z D F p t Z E m h s";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a seed: random string
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "pnnjkbnobnml43679nbvjdsklnbjs";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        // Specify non-string or invalid seed_hex
        {  // not a string: number
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = 443556;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a string: object
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = Json::objectValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not a string: array
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = Json::arrayValue;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(
                error[jss::error_message] ==
                "Invalid field 'seed_hex', not string.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // empty
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // short
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "A670A19B";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // not hex
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = common::passphrase;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }

        {  // overlong
            Json::Value params;
            Json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] =
                "BE6A670A19B209E112146D0A7ED2AAD72567D0FC913";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(contains_error(error));
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
            BEAST_EXPECT(ret.first.size() == 0);
        }
    }

    void
    testRippleLibEd25519()
    {
        testcase("ripple-lib encoded Ed25519 keys");

        auto test = [this](char const* seed, char const* addr) {
            {
                Json::Value params;
                Json::Value error;

                params[jss::passphrase] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(!contains_error(error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(toBase58(calcAccountID(ret.first)) == addr);
            }

            {
                Json::Value params;
                Json::Value error;

                params[jss::key_type] = "secp256k1";
                params[jss::passphrase] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(contains_error(error));
                BEAST_EXPECT(
                    error[jss::error_message] ==
                    "Specified seed is for an Ed25519 wallet.");
            }

            {
                Json::Value params;
                Json::Value error;

                params[jss::key_type] = "ed25519";
                params[jss::seed] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(!contains_error(error));
                BEAST_EXPECT(ret.first.size() != 0);
                BEAST_EXPECT(toBase58(calcAccountID(ret.first)) == addr);
            }

            {
                Json::Value params;
                Json::Value error;

                params[jss::key_type] = "secp256k1";
                params[jss::seed] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(contains_error(error));
                BEAST_EXPECT(
                    error[jss::error_message] ==
                    "Specified seed is for an Ed25519 wallet.");
            }
        };

        test(
            "sEdVWZmeUDgQdMEFKTK9kYVX71FKB7o",
            "r34XnDB2zS11NZ1wKJzpU1mjWExGVugTaQ");
        test(
            "sEd7zJoVnqg1FxB9EuaHC1AB5UPfHWz",
            "rDw51qRrBEeMw7Na1Nh79LN7HYZDo7nZFE");
        test(
            "sEdSxVntbihdLyabbfttMCqsaaucVR9",
            "rwiyBDfAYegXZyaQcN2L1vAbKRYn2wNFMq");
        test(
            "sEdSVwJjEXTYCztqDK4JD9WByH3otDX",
            "rQJ4hZzNGkLQhLtKPCmu1ywEw1ai2vgUJN");
        test(
            "sEdV3jXjKuUoQTSr1Rb4yw8Kyn9r46U",
            "rERRw2Pxbau4tevE61V5vZUwD7Rus5Y6vW");
        test(
            "sEdVeUZjuYT47Uy51FQCnzivsuWyiwB",
            "rszewT5gRjUgWNEmnfMjvVYzJCkhvWY32i");
        test(
            "sEd7MHTewdw4tFYeS7rk7XT4qHiA9jH",
            "rBB2rvnf4ztwjgNhinFXQJ91nAZjkFgR3p");
        test(
            "sEd7A5jFBSdWbNeKGriQvLr1thBScJh",
            "rLAXz8Nz7aDivz7PwThsLFqaKrizepNCdA");
        test(
            "sEdVPU9M2uyzVNT4Yb5Dn4tUtYjbFAw",
            "rHbHRFPCxD5fnn98TBzsQHJ7SsRq7eHkRj");
        test(
            "sEdVfF2zhAmS8gfMYzJ4yWBMeR4BZKc",
            "r9PsneKHcAE7kUfiTixomM5Mnwi28tCc7h");
        test(
            "sEdTjRtcsQkwthDXUSLi9DHNyJcR8GW",
            "rM4soF4XS3wZrmLurvE6ZmudG16Lk5Dur5");
        test(
            "sEdVNKeu1Lhpfh7Nf6tRDbxnmMyZ4Dv",
            "r4ZwJxq6FDtWjapDtCGhjG6mtNm1nWdJcD");
        test(
            "sEd7bK4gf5BHJ1WbaEWx8pKMA9MLHpC",
            "rD6tnn51m4o1uXeEK9CFrZ3HR7DcFhiYnp");
        test(
            "sEd7jCh3ppnQMsLdGcZ6TZayZaHhBLg",
            "rTcBkiRQ1EfFQ4FCCwqXNHpn1yUTAACkj");
        test(
            "sEdTFJezurQwSJAbkLygj2gQXBut2wh",
            "rnXaMacNbRwcJddbbPbqdcpSUQcfzFmrR8");
        test(
            "sEdSWajfQAAWFuDvVZF3AiGucReByLt",
            "rBJtow6V3GTdsWMamrxetRDwWs6wwTxcKa");
    }

    void
    run() override
    {
        testKeyType(std::nullopt, secp256k1_strings);
        testKeyType(std::string("secp256k1"), secp256k1_strings);
        testKeyType(std::string("ed25519"), ed25519_strings);
        testKeyType(std::string("secp256k1"), strong_brain_strings);
        testBadInput();

        testKeypairForSignature(std::nullopt, secp256k1_strings);
        testKeypairForSignature(std::string("secp256k1"), secp256k1_strings);
        testKeypairForSignature(std::string("ed25519"), ed25519_strings);
        testKeypairForSignature(std::string("secp256k1"), strong_brain_strings);

        testRippleLibEd25519();

        testKeypairForSignatureErrors();
    }
};

BEAST_DEFINE_TESTSUITE(WalletPropose, ripple_basics, ripple);

}  // namespace RPC
}  // namespace ripple
