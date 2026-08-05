#include <test/jtx/TestSuite.h>

#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/handlers/admin/keygen/WalletPropose.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <optional>
#include <string>

namespace xrpl::RPC {

struct KeyStrings
{
    char const* accountId;
    char const* masterKey;
    char const* masterSeed;
    char const* masterSeedHex;
    char const* publicKey;
    char const* publicKeyHex;
    char const* secretKeyHex;
    char const* passphrase;
    char const* passphraseWarning;
};

namespace common {
static char const* gPassphrase = "REINDEER FLOTILLA";
static char const* gMasterKey = "SCAT BERN ISLE FOR ROIL BUS SOAK AQUA FREE FOR DRAM BRIG";
static char const* gMasterSeed = "snMwVWs2hZzfDUF3p2tHZ3EgmyhFs";
static char const* gMasterSeedHex = "BE6A670A19B209E112146D0A7ED2AAD7";
}  // namespace common

static KeyStrings const kSecP256K1Strings = {
    .accountId = "r4Vtj2jrfmTVZGfSP3gH9hQPMqFPQFin8f",
    .masterKey = common::gMasterKey,
    .masterSeed = common::gMasterSeed,
    .masterSeedHex = common::gMasterSeedHex,
    .publicKey = "aBQxK2YFNqzmAaXNczYcjqDjfiKkLsJUizsr1UBf44RCF8FHdrmX",
    .publicKeyHex = "038AAE247B2344B1837FBED8F57389C8C11774510A3F7D784F2A09F0CB6843236C",
    .secretKeyHex = "1949ECD889EA71324BC7A30C8E81F4E93CB73EE19D59E9082111E78CC3DDABC2",
    .passphrase = common::gPassphrase,
    .passphraseWarning =
        "This wallet was generated using a user-supplied "
        "passphrase that has low entropy and is vulnerable "
        "to brute-force attacks.",
};

static KeyStrings const kED25519Strings = {
    .accountId = "r4qV6xTXerqaZav3MJfSY79ynmc1BSBev1",
    .masterKey = common::gMasterKey,
    .masterSeed = common::gMasterSeed,
    .masterSeedHex = common::gMasterSeedHex,
    .publicKey = "aKEQmgLMyZPMruJFejUuedp169LgW6DbJt1rej1DJ5hWUMH4pHJ7",
    .publicKeyHex = "ED54C3F5BEDA8BD588B203D23A27398FAD9D20F88A974007D6994659CD7273FE1D",
    .secretKeyHex = "77AAED2698D56D6676323629160F4EEF21CFD9EE3D0745CC78FA291461F98278",
    .passphrase = common::gPassphrase,
    .passphraseWarning =
        "This wallet was generated using a user-supplied "
        "passphrase that has low entropy and is vulnerable "
        "to brute-force attacks.",
};

static KeyStrings const kStrongBrainStrings = {
    .accountId = "rBcvXmNb7KPkNdMkpckdWPpbvkWgcV3nir",
    .masterKey = "TED AVON CAVE HOUR BRAG JEFF RIFT NEAL TOLD FAT SEW SAN",
    .masterSeed = "shKdhWka8hS7Es3bpctCZXBiAwfUN",
    .masterSeedHex = "74BA8389B44F98CF41E795CD91F9C93F",
    .publicKey = "aBRL2sqVuzrsM6zikPB4v8UBHGn1aKkrsxhYEffhcQxB2LKyywE5",
    .publicKeyHex = "03BD334FB9E06C58D69603E9922686528B18A754BC2F2E1ADA095FFE67DE952C64",
    .secretKeyHex = "84262FB16AA25BE407174C7EDAB531220C30FA4D8A28AA9D564673FB3D34502C",
    .passphrase = "A4yKIRGdzrw0YQ$2%TFKYG9HP*&ok^!sy7E@RwICs",
    .passphraseWarning =
        "This wallet was generated using a user-supplied "
        "passphrase. It may be vulnerable to brute-force "
        "attacks.",
};

class WalletPropose_test : public xrpl::TestSuite
{
public:
    void
    testRandomWallet(std::optional<std::string> const& keyType)
    {
        json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        json::Value result = walletPropose(params);

        BEAST_EXPECT(!containsError(result));
        BEAST_EXPECT(result.isMember(jss::account_id));
        BEAST_EXPECT(result.isMember(jss::master_seed));
        BEAST_EXPECT(result.isMember(jss::master_seed_hex));
        BEAST_EXPECT(result.isMember(jss::public_key));
        BEAST_EXPECT(result.isMember(jss::public_key_hex));
        BEAST_EXPECT(result.isMember(jss::key_type));

        expectEquals(
            result[jss::key_type],
            params.isMember(jss::key_type) ? params[jss::key_type] : "secp256k1");
        BEAST_EXPECT(!result.isMember(jss::warning));

        std::string const seed = result[jss::master_seed].asString();

        result = walletPropose(params);

        // We asked for two random seeds, so they shouldn't match.
        BEAST_EXPECT(result[jss::master_seed].asString() != seed);
    }

    json::Value
    testSecretWallet(json::Value const& params, KeyStrings const& s)
    {
        json::Value result = walletPropose(params);

        BEAST_EXPECT(!containsError(result));
        expectEquals(result[jss::account_id], s.accountId);
        expectEquals(result[jss::master_seed], s.masterSeed);
        expectEquals(result[jss::master_seed_hex], s.masterSeedHex);
        expectEquals(result[jss::public_key], s.publicKey);
        expectEquals(result[jss::public_key_hex], s.publicKeyHex);
        expectEquals(
            result[jss::key_type],
            params.isMember(jss::key_type) ? params[jss::key_type] : "secp256k1");
        return result;
    }

    void
    testSeed(std::optional<std::string> const& keyType, KeyStrings const& strings)
    {
        testcase("seed");

        json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.masterSeed;

        auto const wallet = testSecretWallet(params, strings);
        BEAST_EXPECT(!wallet.isMember(jss::warning));
    }

    void
    testSeedHex(std::optional<std::string> const& keyType, KeyStrings const& strings)
    {
        testcase("seed_hex");

        json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed_hex] = strings.masterSeedHex;

        auto const wallet = testSecretWallet(params, strings);
        BEAST_EXPECT(!wallet.isMember(jss::warning));
    }

    void
    testLegacyPassphrase(
        char const* value,
        std::optional<std::string> const& keyType,
        KeyStrings const& strings)
    {
        json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::passphrase] = value;

        auto const wallet = testSecretWallet(params, strings);
        if (value == strings.passphrase)
        {
            BEAST_EXPECT(wallet[jss::warning] == strings.passphraseWarning);
        }
        else
        {
            BEAST_EXPECT(!wallet.isMember(jss::warning));
        }
    }

    void
    testLegacyPassphrase(std::optional<std::string> const& keyType, KeyStrings const& strings)
    {
        testcase("passphrase");

        testLegacyPassphrase(strings.passphrase, keyType, strings);
        testLegacyPassphrase(strings.masterKey, keyType, strings);
        testLegacyPassphrase(strings.masterSeed, keyType, strings);
        testLegacyPassphrase(strings.masterSeedHex, keyType, strings);
    }

    void
    testKeyType(std::optional<std::string> const& keyType, KeyStrings const& strings)
    {
        testcase(keyType ? *keyType : "no key_type");

        testRandomWallet(keyType);
        testSeed(keyType, strings);
        testSeedHex(keyType, strings);
        testLegacyPassphrase(keyType, strings);

        json::Value params;
        if (keyType)
            params[jss::key_type] = *keyType;
        params[jss::seed] = strings.masterSeed;
        params[jss::seed_hex] = strings.masterSeedHex;

        // Secret fields are mutually exclusive.
        BEAST_EXPECT(containsError(walletPropose(params)));
    }

    void
    testBadInput()
    {
        testcase("Bad inputs");

        // Passing non-strings where strings are required
        {
            json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 20160506;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'passphrase', not string.");
        }

        {
            json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = json::ValueType::Object;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'seed', not string.");
        }

        {
            json::Value params;
            params[jss::key_type] = "ed25519";
            params[jss::seed_hex] = json::ValueType::Array;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'seed_hex', not string.");
        }

        // Specifying multiple items at once
        {
            json::Value params;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = common::gMasterKey;
            params[jss::seed_hex] = common::gMasterSeedHex;
            params[jss::seed] = common::gMasterSeed;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Exactly one of the following must be specified: passphrase, "
                "seed or seed_hex");
        }

        // Specifying bad key types:
        {
            json::Value params;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::gMasterKey;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid parameters.");
        }

        {
            json::Value params;
            params[jss::key_type] = json::ValueType::Object;
            params[jss::seed_hex] = common::gMasterSeedHex;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'key_type', not string.");
        }

        {
            json::Value params;
            params[jss::key_type] = json::ValueType::Array;
            params[jss::seed] = common::gMasterSeed;
            auto result = walletPropose(params);
            BEAST_EXPECT(containsError(result));
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'key_type', not string.");
        }
    }

    void
    testKeypairForSignature(std::optional<std::string> keyType, KeyStrings const& strings)
    {
        testcase("keypairForSignature - " + (keyType ? *keyType : "no key_type"));

        auto const publicKey = parseBase58<PublicKey>(TokenType::AccountPublic, strings.publicKey);
        BEAST_EXPECT(publicKey);

        if (!keyType)
        {
            {
                json::Value params;
                json::Value error;
                params[jss::secret] = strings.masterSeed;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!containsError(error));
                if (BEAST_EXPECT(ret); ret.has_value())
                {
                    BEAST_EXPECT(ret->first.size() != 0);
                    BEAST_EXPECT(ret->first == publicKey);
                }
            }

            {
                json::Value params;
                json::Value error;
                params[jss::secret] = strings.masterSeedHex;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!containsError(error));
                if (BEAST_EXPECT(ret); ret.has_value())
                {
                    BEAST_EXPECT(ret->first.size() != 0);
                    BEAST_EXPECT(ret->first == publicKey);
                }
            }

            {
                json::Value params;
                json::Value error;
                params[jss::secret] = strings.masterKey;

                auto ret = keypairForSignature(params, error);
                BEAST_EXPECT(!containsError(error));
                if (BEAST_EXPECT(ret); ret.has_value())
                {
                    BEAST_EXPECT(ret->first.size() != 0);
                    BEAST_EXPECT(ret->first == publicKey);
                }
            }

            keyType.emplace("secp256k1");
        }

        {
            json::Value params;
            json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed] = strings.masterSeed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!containsError(error));
            if (BEAST_EXPECT(ret); ret.has_value())
            {
                BEAST_EXPECT(ret->first.size() != 0);
                BEAST_EXPECT(ret->first == publicKey);
            }
        }

        {
            json::Value params;
            json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::seed_hex] = strings.masterSeedHex;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!containsError(error));
            if (BEAST_EXPECT(ret); ret.has_value())
            {
                BEAST_EXPECT(ret->first.size() != 0);
                BEAST_EXPECT(ret->first == publicKey);
            }
        }

        {
            json::Value params;
            json::Value error;

            params[jss::key_type] = *keyType;
            params[jss::passphrase] = strings.masterKey;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(!containsError(error));
            if (BEAST_EXPECT(ret); ret.has_value())
            {
                BEAST_EXPECT(ret->first.size() != 0);
                BEAST_EXPECT(ret->first == publicKey);
            }
        }
    }

    void
    testKeypairForSignatureErrors()
    {
        // Specify invalid "secret"
        {
            json::Value params;
            json::Value error;
            params[jss::secret] = 314159265;
            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'secret', not string.");
        }

        {
            json::Value params;
            json::Value error;
            params[jss::secret] = json::ValueType::Array;
            params[jss::secret].append("array:0");

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'secret', not string.");
        }

        {
            json::Value params;
            json::Value error;
            params[jss::secret] = json::ValueType::Object;
            params[jss::secret]["string"] = "string";
            params[jss::secret]["number"] = 702;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'secret', not string.");
        }

        // Specify "secret" and "key_type"
        {
            json::Value params;
            json::Value error;
            params[jss::key_type] = "ed25519";
            params[jss::secret] = common::gMasterSeed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(
                error[jss::error_message] ==
                "The secret field is not allowed if key_type is used.");
        }

        // Specify unknown or bad "key_type"
        {
            json::Value params;
            json::Value error;
            params[jss::key_type] = "prime256v1";
            params[jss::passphrase] = common::gMasterKey;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'key_type'.");
        }

        {
            json::Value params;
            json::Value error;
            params[jss::key_type] = json::ValueType::Object;
            params[jss::seed_hex] = common::gMasterSeedHex;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'key_type', not string.");
        }

        {
            json::Value params;
            json::Value error;
            params[jss::key_type] = json::ValueType::Array;
            params[jss::seed] = common::gMasterSeed;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'key_type', not string.");
        }

        // Specify non-string passphrase
        {  // not a passphrase: number
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = 1234567890;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'passphrase', not string.");
        }

        {  // not a passphrase: object
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = json::ValueType::Object;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'passphrase', not string.");
        }

        {  // not a passphrase: array
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = json::ValueType::Array;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'passphrase', not string.");
        }

        {  // not a passphrase: empty string
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::passphrase] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        // Specify non-string or invalid seed
        {  // not a seed: number
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = 443556;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed', not string.");
        }

        {  // not a string: object
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = json::ValueType::Object;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed', not string.");
        }

        {  // not a string: array
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = json::ValueType::Array;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed', not string.");
        }

        {  // not a seed: empty
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        {  // not a seed: invalid characters
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "s M V s h z D F p t Z E m h s";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        {  // not a seed: random string
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed] = "pnnjkbnobnml43679nbvjdsklnbjs";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        // Specify non-string or invalid seed_hex
        {  // not a string: number
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = 443556;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed_hex', not string.");
        }

        {  // not a string: object
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = json::ValueType::Object;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed_hex', not string.");
        }

        {  // not a string: array
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = json::ValueType::Array;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Invalid field 'seed_hex', not string.");
        }

        {  // empty
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        {  // short
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "A670A19B";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        {  // not hex
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = common::gPassphrase;

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }

        {  // overlong
            json::Value params;
            json::Value error;
            params[jss::key_type] = "secp256k1";
            params[jss::seed_hex] = "BE6A670A19B209E112146D0A7ED2AAD72567D0FC913";

            auto ret = keypairForSignature(params, error);
            BEAST_EXPECT(containsError(error));
            BEAST_EXPECT(!ret);
            BEAST_EXPECT(error[jss::error_message] == "Disallowed seed.");
        }
    }

    void
    testXrplLibEd25519()
    {
        testcase("XrplLib encoded Ed25519 keys");

        auto test = [this](char const* seed, char const* addr) {
            {
                json::Value params;
                json::Value error;

                params[jss::passphrase] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(!containsError(error));
                if (BEAST_EXPECT(ret); ret.has_value())
                {
                    BEAST_EXPECT(ret->first.size() != 0);
                    BEAST_EXPECT(toBase58(calcAccountID(ret->first)) == addr);
                }
            }

            {
                json::Value params;
                json::Value error;

                params[jss::key_type] = "secp256k1";
                params[jss::passphrase] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(containsError(error));
                BEAST_EXPECT(
                    error[jss::error_message] == "Specified seed is for an Ed25519 wallet.");
            }

            {
                json::Value params;
                json::Value error;

                params[jss::key_type] = "ed25519";
                params[jss::seed] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(!containsError(error));
                if (BEAST_EXPECT(ret); ret.has_value())
                {
                    BEAST_EXPECT(ret->first.size() != 0);
                    BEAST_EXPECT(toBase58(calcAccountID(ret->first)) == addr);
                }
            }

            {
                json::Value params;
                json::Value error;

                params[jss::key_type] = "secp256k1";
                params[jss::seed] = seed;

                auto ret = keypairForSignature(params, error);

                BEAST_EXPECT(containsError(error));
                BEAST_EXPECT(
                    error[jss::error_message] == "Specified seed is for an Ed25519 wallet.");
            }
        };

        test("sEdVWZmeUDgQdMEFKTK9kYVX71FKB7o", "r34XnDB2zS11NZ1wKJzpU1mjWExGVugTaQ");
        test("sEd7zJoVnqg1FxB9EuaHC1AB5UPfHWz", "rDw51qRrBEeMw7Na1Nh79LN7HYZDo7nZFE");
        test("sEdSxVntbihdLyabbfttMCqsaaucVR9", "rwiyBDfAYegXZyaQcN2L1vAbKRYn2wNFMq");
        test("sEdSVwJjEXTYCztqDK4JD9WByH3otDX", "rQJ4hZzNGkLQhLtKPCmu1ywEw1ai2vgUJN");
        test("sEdV3jXjKuUoQTSr1Rb4yw8Kyn9r46U", "rERRw2Pxbau4tevE61V5vZUwD7Rus5Y6vW");
        test("sEdVeUZjuYT47Uy51FQCnzivsuWyiwB", "rszewT5gRjUgWNEmnfMjvVYzJCkhvWY32i");
        test("sEd7MHTewdw4tFYeS7rk7XT4qHiA9jH", "rBB2rvnf4ztwjgNhinFXQJ91nAZjkFgR3p");
        test("sEd7A5jFBSdWbNeKGriQvLr1thBScJh", "rLAXz8Nz7aDivz7PwThsLFqaKrizepNCdA");
        test("sEdVPU9M2uyzVNT4Yb5Dn4tUtYjbFAw", "rHbHRFPCxD5fnn98TBzsQHJ7SsRq7eHkRj");
        test("sEdVfF2zhAmS8gfMYzJ4yWBMeR4BZKc", "r9PsneKHcAE7kUfiTixomM5Mnwi28tCc7h");
        test("sEdTjRtcsQkwthDXUSLi9DHNyJcR8GW", "rM4soF4XS3wZrmLurvE6ZmudG16Lk5Dur5");
        test("sEdVNKeu1Lhpfh7Nf6tRDbxnmMyZ4Dv", "r4ZwJxq6FDtWjapDtCGhjG6mtNm1nWdJcD");
        test("sEd7bK4gf5BHJ1WbaEWx8pKMA9MLHpC", "rD6tnn51m4o1uXeEK9CFrZ3HR7DcFhiYnp");
        test("sEd7jCh3ppnQMsLdGcZ6TZayZaHhBLg", "rTcBkiRQ1EfFQ4FCCwqXNHpn1yUTAACkj");
        test("sEdTFJezurQwSJAbkLygj2gQXBut2wh", "rnXaMacNbRwcJddbbPbqdcpSUQcfzFmrR8");
        test("sEdSWajfQAAWFuDvVZF3AiGucReByLt", "rBJtow6V3GTdsWMamrxetRDwWs6wwTxcKa");
    }

    void
    run() override
    {
        testKeyType(std::nullopt, kSecP256K1Strings);
        testKeyType(std::string("secp256k1"), kSecP256K1Strings);
        testKeyType(std::string("ed25519"), kED25519Strings);
        testKeyType(std::string("secp256k1"), kStrongBrainStrings);
        testBadInput();

        testKeypairForSignature(std::nullopt, kSecP256K1Strings);
        testKeypairForSignature(std::string("secp256k1"), kSecP256K1Strings);
        testKeypairForSignature(std::string("ed25519"), kED25519Strings);
        testKeypairForSignature(std::string("secp256k1"), kStrongBrainStrings);

        testXrplLibEd25519();

        testKeypairForSignatureErrors();
    }
};

BEAST_DEFINE_TESTSUITE(WalletPropose, rpc, xrpl);

}  // namespace xrpl::RPC
