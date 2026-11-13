#include <test/jtx/Env.h>

#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Manifest.h>

#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * Round trips from the validator-keys tool through xrpld's own code: tokens
 * load in ValidatorKeys, tool-signed lists apply in ValidatorList, and
 * verify_list passes no document a server rejects.
 */
class ValidatorKeysTool_test : public beast::unit_test::Suite
{
    // A token's manifest and the signing key pair it delegates to.
    struct Signer
    {
        std::string manifest;
        PublicKey signingKey;
        SecretKey signingSecret;
    };

    // A ValidatorList trusting one publisher, as testApplyLists builds one.
    struct Server
    {
        ManifestCache manifests;
        ValidatorList trusted;
        bool loaded;

        Server(jtx::Env& env, PublicKey const& publisher)
            : trusted(
                  manifests,
                  manifests,
                  env.app().getTimeKeeper(),
                  env.app().config().legacy(Sections::kDatabasePath),
                  env.journal)
            , loaded(trusted.load({}, {}, {strHex(publisher)}))
        {
        }
    };

    // The value a test relies on; an empty optional throws, failing the case.
    template <class T>
    static T
    required(std::optional<T> value)
    {
        if (!value)
            throw std::runtime_error("required value is missing");
        return std::move(*value);
    }

    static Signer
    signerOf(ValidatorToken const& token)
    {
        auto const m = required(deserializeManifest(base64Decode(token.manifest)));
        return {
            .manifest = token.manifest,
            .signingKey = required(m.signingKey),
            .signingSecret = token.validationSecret,
        };
    }

    static std::vector<ValidatorToken>
    makeValidators(std::size_t count)
    {
        std::vector<ValidatorToken> validators;
        for (std::size_t i = 0; i < count; ++i)
        {
            SigningKeys keys(KeyType::Ed25519);
            validators.push_back(keys.createToken(KeyType::Secp256k1));
        }
        return validators;
    }

    static PublicKey
    masterOf(ValidatorToken const& token)
    {
        return required(deserializeManifest(base64Decode(token.manifest))).masterKey;
    }

    // One `validators` entry: the validator's master key and its manifest.
    static std::string
    entryOf(ValidatorToken const& token, std::string const& manifest)
    {
        return R"({"validation_public_key": ")" + strHex(masterOf(token)) + R"(", "manifest": ")" +
            manifest + "\"}";
    }

    static std::string
    entriesOf(std::vector<ValidatorToken> const& validators)
    {
        std::string entries;
        for (auto const& v : validators)
            entries += (entries.empty() ? "" : ", ") + entryOf(v, v.manifest);
        return entries;
    }

    // The text of an unsigned list whose `validators` array holds @p entries.
    static std::string
    listText(
        std::string const& entries,
        std::uint32_t sequence,
        std::uint32_t expiration,
        std::optional<std::uint32_t> effective = std::nullopt)
    {
        std::string text = R"({"sequence": )" + std::to_string(sequence);
        if (effective)
            text += R"(, "effective": )" + std::to_string(*effective);
        return text + R"(, "expiration": )" + std::to_string(expiration) + R"(, "validators": [)" +
            entries + "]}";
    }

    static std::string
    signText(std::string const& text, Signer const& signer)
    {
        return strHex(sign(signer.signingKey, signer.signingSecret, makeSlice(text)));
    }

    static json::Value
    signedList(
        Signer const& signer,
        PublicKey const& master,
        UnsignedList const& list,
        unsigned version,
        std::optional<json::Value> const& append,
        std::uint32_t now)
    {
        return makeSignedList(
            signer.manifest,
            master,
            list,
            signList(list, signer.signingKey, signer.signingSecret),
            version,
            append,
            now);
    }

    // A version 1 document of @p doc's manifest whose blob is @p text signed
    // by @p signer.
    static json::Value
    withBlob(json::Value doc, std::string const& text, Signer const& signer)
    {
        doc[jss::blob] = base64Encode(text);
        doc[jss::signature] = signText(text, signer);
        return doc;
    }

    // Applies a published document as ValidatorSite applies a fetched one.
    static ValidatorList::PublisherListStats
    apply(ValidatorList& trusted, json::Value const& doc)
    {
        json::Reader reader;
        json::Value body;
        if (!reader.parse(to_string(doc), body) || !body.isObject() ||
            !body.isMember(jss::manifest) || !body[jss::manifest].isString() ||
            !body.isMember(jss::version) || !body[jss::version].isInt())
            return ValidatorList::PublisherListStats{ListDisposition::Invalid};
        auto const version = body[jss::version].asUInt();
        auto const blobs = ValidatorList::parseBlobs(version, body);
        if (blobs.empty())
            return ValidatorList::PublisherListStats{ListDisposition::Invalid};
        return trusted.applyLists(body[jss::manifest].asString(), version, blobs, "tool");
    }

    // The best disposition of a fresh server trusting @p publisher.
    ListDisposition
    serverBest(jtx::Env& env, PublicKey const& publisher, json::Value const& doc)
    {
        Server server(env, publisher);
        BEAST_EXPECT(server.loaded);
        return apply(server.trusted, doc).bestDisposition();
    }

    static std::uint32_t
    nowOf(jtx::Env& env)
    {
        return static_cast<std::uint32_t>(env.timeKeeper().now().time_since_epoch().count());
    }

    void
    testToolToken()
    {
        testcase("tool token");
        jtx::Env const env(*this);

        SigningKeys keys(KeyType::Ed25519);
        {
            Config config;
            config.section(Sections::kValidatorToken)
                .append(tokenToBase64(keys.createToken(KeyType::Secp256k1)));
            ValidatorKeys const loaded(config, env.journal);
            BEAST_EXPECT(!loaded.configInvalid());
            BEAST_EXPECT(loaded.keys && loaded.keys->masterPublicKey == keys.publicKey());
        }
        {
            Config config;
            config.section(Sections::kValidatorToken)
                .append(tokenToBase64(keys.createToken(KeyType::Ed25519)));
            ValidatorKeys const loaded(config, env.journal);
            BEAST_EXPECT(loaded.configInvalid());
        }
    }

    void
    testToolSignedLists()
    {
        testcase("tool-signed lists");
        jtx::Env env(*this);
        auto const now = nowOf(env);

        SigningKeys publisher(KeyType::Ed25519);
        auto const signer = signerOf(publisher.createToken(KeyType::Ed25519));
        auto const master = publisher.publicKey();
        auto const validators = makeValidators(3);
        auto const list1 = parseUnsignedList(listText(entriesOf(validators), 1, now + 3600));

        // Version 1
        {
            Server server(env, master);
            BEAST_EXPECT(server.loaded);
            auto const stats =
                apply(server.trusted, signedList(signer, master, list1, 1, std::nullopt, now));
            BEAST_EXPECT(stats.bestDisposition() == ListDisposition::Accepted);
            for (auto const& v : validators)
                BEAST_EXPECT(server.trusted.listed(masterOf(v)));
        }

        // Version 2 with a future-effective blob appended
        {
            Server server(env, master);
            BEAST_EXPECT(server.loaded);
            auto const list2 = parseUnsignedList(
                listText(entriesOf(makeValidators(2)), 2, now + 7200, now + 1800));
            auto const doc = signedList(
                signer,
                master,
                list2,
                2,
                signedList(signer, master, list1, 2, std::nullopt, now),
                now);
            BEAST_EXPECT(doc[jss::blobs_v2].size() == 2);
            auto const stats = apply(server.trusted, doc);
            BEAST_EXPECT(stats.bestDisposition() == ListDisposition::Accepted);
            BEAST_EXPECT(stats.dispositions.contains(ListDisposition::Accepted));
            BEAST_EXPECT(stats.dispositions.contains(ListDisposition::Pending));
            for (auto const& v : validators)
                BEAST_EXPECT(server.trusted.listed(masterOf(v)));
        }

        // Assembled as finish_sign_list does, under a manifest whose signing
        // key is held by an external signer
        {
            SigningKeys keys(KeyType::Ed25519);
            SigningKeys const external(KeyType::Ed25519);
            auto const bytes = keys.startToken(KeyType::Ed25519, external.publicKey());
            auto const finished = keys.finishToken(
                required(strUnHex(keys.signHex(bytes))), strUnHex(external.signHex(bytes)));
            auto const manifest = required(deserializeManifest(base64Decode(finished.manifest)));
            auto const sig =
                required(strUnHex(external.signHex(strHex(makeSlice(list1.canonical)))));
            BEAST_EXPECT(verify(external.publicKey(), makeSlice(list1.canonical), makeSlice(sig)));
            auto const doc = makeSignedList(
                base64Encode(manifest.serialized),
                manifest.masterKey,
                list1,
                strHex(sig),
                1,
                std::nullopt,
                now);
            BEAST_EXPECT(serverBest(env, keys.publicKey(), doc) == ListDisposition::Accepted);
        }
    }

    void
    testVerifyListAgreement()
    {
        testcase("verify_list agrees with ValidatorList");
        jtx::Env env(*this);
        auto const now = nowOf(env);

        SigningKeys publisher(KeyType::Ed25519);
        auto const older = signerOf(publisher.createToken(KeyType::Ed25519));
        auto const signer = signerOf(publisher.createToken(KeyType::Ed25519));
        auto const master = publisher.publicKey();
        auto const validators = makeValidators(3);
        auto const entries = entriesOf(validators);
        auto const expiration = now + 3600;
        auto const list1 = parseUnsignedList(listText(entries, 1, expiration));
        auto const list2 = parseUnsignedList(listText(entries, 2, expiration + 3600, now + 1800));

        auto const good1 = signedList(signer, master, list1, 1, std::nullopt, now);
        auto const base2 = signedList(signer, master, list1, 2, std::nullopt, now);
        auto const good2 = signedList(signer, master, list2, 2, base2, now);

        // A version 2 document whose one entry carries the older manifest and
        // a blob signed by @p by.
        auto const entryUnderOlder = [&](Signer const& by) {
            auto doc = base2;
            doc[jss::blobs_v2][0u][jss::manifest] = older.manifest;
            doc[jss::blobs_v2][0u][jss::signature] = signText(list1.canonical, by);
            return doc;
        };

        std::vector<std::pair<std::string, json::Value>> corpus;
        corpus.emplace_back("good v1", good1);
        corpus.emplace_back("good v2", good2);
        {
            auto doc = base2;
            for (int i = 0; i < 5; ++i)
                doc[jss::blobs_v2].append(base2[jss::blobs_v2][0u]);
            corpus.emplace_back("v2 with 6 entries", doc);
        }
        {
            auto doc = good1;
            doc[jss::manifest] = signer.manifest + std::string(400, '!');
            corpus.emplace_back("manifest followed by 400 '!'", doc);
        }
        {
            auto doc = good1;
            doc[jss::manifest] = base64Encode(makeManifest(
                master, randomSecretKey(), signer.signingKey, signer.signingSecret, 3));
            corpus.emplace_back("manifest with a bad signature", doc);
        }
        corpus.emplace_back("older entry manifest, newer key", entryUnderOlder(signer));
        corpus.emplace_back("older entry manifest, older key", entryUnderOlder(older));
        auto const trailing = withBlob(good1, list1.canonical + " x", signer);
        corpus.emplace_back("blob with trailing x", trailing);

        // Documents both sides accept
        for (auto const& [name, doc] : {
                 std::pair<char const*, json::Value const&>{"good v1", good1},
                 {"good v2", good2},
                 {"blob with trailing x", trailing},
             })
        {
            BEAST_EXPECTS(verifyList(doc, std::nullopt, std::nullopt, now)["ok"].asBool(), name);
            BEAST_EXPECTS(serverBest(env, master, doc) == ListDisposition::Accepted, name);
        }

        // Documents verify_list refuses on purpose and a server accepts
        std::vector<std::pair<std::string, json::Value>> stricter;
        stricter.emplace_back(
            "empty validators", withBlob(good1, listText("", 1, expiration), signer));
        stricter.emplace_back(
            "a skipped entry", withBlob(good1, listText("5, " + entries, 1, expiration), signer));
        stricter.emplace_back(
            "a duplicated key",
            withBlob(
                good1,
                listText(
                    entries + ", " + entryOf(validators[0], validators[0].manifest), 1, expiration),
                signer));
        stricter.emplace_back(
            "a bad validator manifest",
            withBlob(good1, listText(entryOf(validators[0], "AAAA"), 1, expiration), signer));
        {
            auto doc = base2;
            json::Value entry(json::ValueType::Object);
            entry[jss::blob] = base64Encode(list2.canonical);
            entry[jss::signature] = signText(list2.canonical, older);
            doc[jss::blobs_v2].append(entry);
            stricter.emplace_back("second blob with a bad signature", doc);
        }
        {
            auto doc = good1;
            doc.removeMember(jss::public_key);
            stricter.emplace_back("no public_key", doc);
        }
        {
            auto doc = good1;
            doc[jss::public_key] = strHex(SigningKeys(KeyType::Ed25519).publicKey());
            stricter.emplace_back("another key's public_key", doc);
        }

        for (auto const& [name, doc] : stricter)
        {
            BEAST_EXPECTS(!verifyList(doc, std::nullopt, std::nullopt, now)["ok"].asBool(), name);
            BEAST_EXPECTS(serverBest(env, master, doc) == ListDisposition::Accepted, name);
            corpus.emplace_back(name, doc);
        }

        for (auto const& [name, doc] : corpus)
        {
            if (!verifyList(doc, std::nullopt, std::nullopt, now)["ok"].asBool())
                continue;
            auto const best = serverBest(env, master, doc);
            BEAST_EXPECTS(
                best == ListDisposition::Accepted || best == ListDisposition::Pending, name);
        }
    }

public:
    void
    run() override
    {
        testToolToken();
        testToolSignedLists();
        testVerifyListAgreement();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorKeysTool, tools, xrpl);

}  // namespace xrpl::test
