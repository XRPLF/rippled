#include <test/jtx.h>

#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/messages.h>

#include <boost/beast/core/multi_buffer.hpp>

namespace ripple {
namespace test {

class ValidatorList_test : public beast::unit_test::suite
{
private:
    struct Validator
    {
        PublicKey masterPublic;
        PublicKey signingPublic;
        std::string manifest;
    };

    static PublicKey
    randomNode()
    {
        return derivePublicKey(KeyType::secp256k1, randomSecretKey());
    }

    static PublicKey
    randomMasterKey()
    {
        return derivePublicKey(KeyType::ed25519, randomSecretKey());
    }

    static std::string
    makeManifestString(
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;

        if (seq != std::numeric_limits<std::uint32_t>::max())
        {
            st[sfSigningPubKey] = spk;
            sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        }

        sign(
            st,
            HashPrefix::manifest,
            *publicKeyType(pk),
            sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return std::string(static_cast<char const*>(s.data()), s.size());
    }

    static std::string
    makeRevocationString(PublicKey const& pk, SecretKey const& sk)
    {
        STObject st(sfGeneric);
        st[sfSequence] = std::numeric_limits<std::uint32_t>::max();
        st[sfPublicKey] = pk;

        sign(
            st,
            HashPrefix::manifest,
            *publicKeyType(pk),
            sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return std::string(static_cast<char const*>(s.data()), s.size());
    }

    static Validator
    randomValidator()
    {
        auto const secret = randomSecretKey();
        auto const masterPublic = derivePublicKey(KeyType::ed25519, secret);
        auto const signingKeys = randomKeyPair(KeyType::secp256k1);
        return {
            masterPublic,
            signingKeys.first,
            base64_encode(makeManifestString(
                masterPublic,
                secret,
                signingKeys.first,
                signingKeys.second,
                1))};
    }

    std::string
    makeList(
        std::vector<Validator> const& validators,
        std::size_t sequence,
        std::size_t validUntil,
        std::optional<std::size_t> validFrom = {})
    {
        std::string data = "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(validUntil);
        if (validFrom)
            data += ",\"effective\":" + std::to_string(*validFrom);
        data += ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val.masterPublic) +
                "\",\"manifest\":\"" + val.manifest + "\"},";
        }

        data.pop_back();
        data += "]}";
        return base64_encode(data);
    }

    std::string
    signList(
        std::string const& blob,
        std::pair<PublicKey, SecretKey> const& keys)
    {
        auto const data = base64_decode(blob);
        return strHex(sign(keys.first, keys.second, makeSlice(data)));
    }

    static hash_set<NodeID>
    asNodeIDs(std::initializer_list<PublicKey> const& pks)
    {
        hash_set<NodeID> res;
        res.reserve(pks.size());
        for (auto const& pk : pks)
            res.insert(calcNodeID(pk));
        return res;
    }

    void
    checkResult(
        ValidatorList::PublisherListStats const& result,
        PublicKey pubKey,
        ListDisposition expectedWorst,
        ListDisposition expectedBest)
    {
        BEAST_EXPECT(
            result.bestDisposition() > ListDisposition::same_sequence ||
            (result.publisherKey && *result.publisherKey == pubKey));
        BEAST_EXPECT(result.bestDisposition() == expectedBest);
        BEAST_EXPECT(result.worstDisposition() == expectedWorst);
    }

    void
    testGenesisQuorum()
    {
        testcase("Genesis Quorum");

        ManifestCache manifests;
        jtx::Env env(*this);
        auto& app = env.app();
        {
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);
            BEAST_EXPECT(trustedKeys->quorum() == 1);
        }
        {
            std::size_t minQuorum = 0;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal,
                minQuorum);
            BEAST_EXPECT(trustedKeys->quorum() == minQuorum);
        }
    }

    void
    testConfigLoad()
    {
        testcase("Config Load");

        jtx::Env env(
            *this, jtx::envconfig(), nullptr, beast::severities::kDisabled);
        auto& app = env.app();
        std::vector<std::string> const emptyCfgKeys;
        std::vector<std::string> const emptyCfgPublishers;

        auto const localSigningKeys = randomKeyPair(KeyType::secp256k1);
        auto const localSigningPublicOuter = localSigningKeys.first;
        auto const localSigningSecret = localSigningKeys.second;
        auto const localMasterSecret = randomSecretKey();
        auto const localMasterPublic =
            derivePublicKey(KeyType::ed25519, localMasterSecret);

        std::string const cfgManifest(makeManifestString(
            localMasterPublic,
            localMasterSecret,
            localSigningPublicOuter,
            localSigningSecret,
            1));

        auto format = [](PublicKey const& publicKey,
                         char const* comment = nullptr) {
            auto ret = toBase58(TokenType::NodePublic, publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        std::vector<PublicKey> configList;
        configList.reserve(8);

        while (configList.size() != 8)
            configList.push_back(randomNode());

        // Correct configuration
        std::vector<std::string> cfgKeys(
            {format(configList[0]),
             format(configList[1], " Comment"),
             format(configList[2], " Multi Word Comment"),
             format(configList[3], "    Leading Whitespace"),
             format(configList[4], " Trailing Whitespace    "),
             format(configList[5], "    Leading & Trailing Whitespace    "),
             format(
                 configList[6],
                 "    Leading, Trailing & Internal    Whitespace    "),
             format(configList[7], "    ")});

        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            // Correct (empty) configuration
            BEAST_EXPECT(
                trustedKeys->load({}, emptyCfgKeys, emptyCfgPublishers));

            // load local validator key with or without manifest
            BEAST_EXPECT(trustedKeys->load(
                localSigningPublicOuter, emptyCfgKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed(localSigningPublicOuter));

            manifests.applyManifest(*deserializeManifest(cfgManifest));
            BEAST_EXPECT(trustedKeys->load(
                localSigningPublicOuter, emptyCfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->listed(localMasterPublic));
            BEAST_EXPECT(trustedKeys->listed(localSigningPublicOuter));
        }
        {
            // load should add validator keys from config
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            BEAST_EXPECT(trustedKeys->load({}, cfgKeys, emptyCfgPublishers));

            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed(n));

            // load should accept Ed25519 master public keys
            auto const masterNode1 = randomMasterKey();
            auto const masterNode2 = randomMasterKey();

            std::vector<std::string> cfgMasterKeys(
                {format(masterNode1), format(masterNode2, " Comment")});
            BEAST_EXPECT(
                trustedKeys->load({}, cfgMasterKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed(masterNode1));
            BEAST_EXPECT(trustedKeys->listed(masterNode2));

            // load should reject invalid config keys
            BEAST_EXPECT(
                !trustedKeys->load({}, {"NotAPublicKey"}, emptyCfgPublishers));
            BEAST_EXPECT(!trustedKeys->load(
                {}, {format(randomNode(), "!")}, emptyCfgPublishers));

            // load terminates when encountering an invalid entry
            auto const goodKey = randomNode();
            BEAST_EXPECT(!trustedKeys->load(
                {},
                {format(randomNode(), "!"), format(goodKey)},
                emptyCfgPublishers));
            BEAST_EXPECT(!trustedKeys->listed(goodKey));
        }
        {
            // local validator key on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            auto const localSigningPublic =
                parseBase58<PublicKey>(TokenType::NodePublic, cfgKeys.front());

            BEAST_EXPECT(trustedKeys->load(
                *localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed(*localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed(n));
        }
        {
            // local validator key not on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            auto const localSigningPublic = randomNode();
            BEAST_EXPECT(trustedKeys->load(
                localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed(localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed(n));
        }
        {
            // local validator key (with manifest) not on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            manifests.applyManifest(*deserializeManifest(cfgManifest));

            BEAST_EXPECT(trustedKeys->load(
                localSigningPublicOuter, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localMasterPublic);
            BEAST_EXPECT(trustedKeys->listed(localSigningPublicOuter));
            BEAST_EXPECT(trustedKeys->listed(localMasterPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed(n));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            // load should reject invalid validator list signing keys
            std::vector<std::string> badPublishers({"NotASigningKey"});
            BEAST_EXPECT(!trustedKeys->load({}, emptyCfgKeys, badPublishers));

            // load should reject validator list signing keys with invalid
            // encoding
            std::vector<PublicKey> keys(
                {randomMasterKey(), randomMasterKey(), randomMasterKey()});
            badPublishers.clear();
            for (auto const& key : keys)
                badPublishers.push_back(toBase58(TokenType::NodePublic, key));

            BEAST_EXPECT(!trustedKeys->load({}, emptyCfgKeys, badPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(!trustedKeys->trustedPublisher(key));

            // load should accept valid validator list publisher keys
            std::vector<std::string> cfgPublishers;
            for (auto const& key : keys)
                cfgPublishers.push_back(strHex(key));

            BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trustedPublisher(key));
            BEAST_EXPECT(
                trustedKeys->getListThreshold() == keys.size() / 2 + 1);
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            std::vector<PublicKey> keys(
                {randomMasterKey(),
                 randomMasterKey(),
                 randomMasterKey(),
                 randomMasterKey()});
            std::vector<std::string> cfgPublishers;
            for (auto const& key : keys)
                cfgPublishers.push_back(strHex(key));

            // explicitly set the list threshold
            BEAST_EXPECT(trustedKeys->load(
                {}, emptyCfgKeys, cfgPublishers, std::size_t(2)));
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trustedPublisher(key));
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
        }
        {
            // Attempt to load a publisher key that has been revoked.
            // Should fail
            ManifestCache valManifests;
            ManifestCache pubManifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                valManifests,
                pubManifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            auto const pubRevokedSecret = randomSecretKey();
            auto const pubRevokedPublic =
                derivePublicKey(KeyType::ed25519, pubRevokedSecret);
            auto const pubRevokedSigning = randomKeyPair(KeyType::secp256k1);
            // make this manifest revoked (seq num = max)
            //  -- thus should not be loaded
            pubManifests.applyManifest(*deserializeManifest(makeManifestString(
                pubRevokedPublic,
                pubRevokedSecret,
                pubRevokedSigning.first,
                pubRevokedSigning.second,
                std::numeric_limits<std::uint32_t>::max())));

            // these two are not revoked (and not in the manifest cache at all.)
            auto legitKey1 = randomMasterKey();
            auto legitKey2 = randomMasterKey();

            std::vector<std::string> cfgPublishers = {
                strHex(pubRevokedPublic), strHex(legitKey1), strHex(legitKey2)};
            BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgPublishers));

            BEAST_EXPECT(!trustedKeys->trustedPublisher(pubRevokedPublic));
            BEAST_EXPECT(trustedKeys->trustedPublisher(legitKey1));
            BEAST_EXPECT(trustedKeys->trustedPublisher(legitKey2));
            // 2 is the threshold for 3 publishers (even though 1 is revoked)
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
        }
        {
            // One (of two) publisher keys has been revoked, the user had
            // explicitly set validator list threshold to 2.
            ManifestCache valManifests;
            ManifestCache pubManifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                valManifests,
                pubManifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            auto const pubRevokedSecret = randomSecretKey();
            auto const pubRevokedPublic =
                derivePublicKey(KeyType::ed25519, pubRevokedSecret);
            auto const pubRevokedSigning = randomKeyPair(KeyType::secp256k1);
            // make this manifest revoked (seq num = max)
            //  -- thus should not be loaded
            pubManifests.applyManifest(*deserializeManifest(makeManifestString(
                pubRevokedPublic,
                pubRevokedSecret,
                pubRevokedSigning.first,
                pubRevokedSigning.second,
                std::numeric_limits<std::uint32_t>::max())));

            // this one is not revoked (and not in the manifest cache at all.)
            auto legitKey = randomMasterKey();

            std::vector<std::string> cfgPublishers = {
                strHex(pubRevokedPublic), strHex(legitKey)};
            BEAST_EXPECT(trustedKeys->load(
                {}, emptyCfgKeys, cfgPublishers, std::size_t(2)));

            BEAST_EXPECT(!trustedKeys->trustedPublisher(pubRevokedPublic));
            BEAST_EXPECT(trustedKeys->trustedPublisher(legitKey));
            // 2 is the threshold, as requested in configuration
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
        }
    }

    void
    testApplyLists()
    {
        testcase("Apply list");
        using namespace std::chrono_literals;

        std::string const siteUri = "testApplyList.test";

        auto checkAvailable =
            [this](
                auto const& trustedKeys,
                auto const& hexPublic,
                auto const& manifest,
                auto const version,
                std::vector<std::pair<std::string, std::string>> const&
                    expected) {
                auto const available = trustedKeys->getAvailable(hexPublic);

                BEAST_EXPECT(!version || available);
                if (available)
                {
                    auto const& a = *available;
                    BEAST_EXPECT(a[jss::public_key] == hexPublic);
                    BEAST_EXPECT(a[jss::manifest] == manifest);
                    // Because multiple lists were processed, the version was
                    // overridden
                    BEAST_EXPECT(a[jss::version] == version);
                    if (version == 1)
                    {
                        BEAST_EXPECT(expected.size() == 1);
                        BEAST_EXPECT(a[jss::blob] == expected[0].first);
                        BEAST_EXPECT(a[jss::signature] == expected[0].second);
                        BEAST_EXPECT(!a.isMember(jss::blobs_v2));
                    }
                    else if (BEAST_EXPECT(a.isMember(jss::blobs_v2)))
                    {
                        BEAST_EXPECT(!a.isMember(jss::blob));
                        BEAST_EXPECT(!a.isMember(jss::signature));
                        auto const& blobs_v2 = a[jss::blobs_v2];
                        BEAST_EXPECT(
                            blobs_v2.isArray() &&
                            blobs_v2.size() == expected.size());

                        for (unsigned int i = 0; i < expected.size(); ++i)
                        {
                            BEAST_EXPECT(
                                blobs_v2[i][jss::blob] == expected[i].first);
                            BEAST_EXPECT(
                                blobs_v2[i][jss::signature] ==
                                expected[i].second);
                        }
                    }
                }
            };

        ManifestCache manifests;
        jtx::Env env(*this);
        auto& app = env.app();
        auto trustedKeys = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.app().timeKeeper(),
            app.config().legacy("database_path"),
            env.journal);

        auto expectTrusted =
            [this, &trustedKeys](std::vector<Validator> const& list) {
                for (auto const& val : list)
                {
                    BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                    BEAST_EXPECT(trustedKeys->listed(val.signingPublic));
                }
            };

        auto expectUntrusted =
            [this, &trustedKeys](std::vector<Validator> const& list) {
                for (auto const& val : list)
                {
                    BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));
                    BEAST_EXPECT(!trustedKeys->listed(val.signingPublic));
                }
            };

        auto const publisherSecret = randomSecretKey();
        auto const publisherPublic =
            derivePublicKey(KeyType::ed25519, publisherSecret);
        auto const hexPublic =
            strHex(publisherPublic.begin(), publisherPublic.end());
        auto const pubSigningKeys1 = randomKeyPair(KeyType::secp256k1);
        auto const manifest1 = base64_encode(makeManifestString(
            publisherPublic,
            publisherSecret,
            pubSigningKeys1.first,
            pubSigningKeys1.second,
            1));

        std::vector<std::string> cfgKeys1({strHex(publisherPublic)});
        std::vector<std::string> emptyCfgKeys;

        BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgKeys1));

        std::map<std::size_t, std::vector<Validator>> const lists = []() {
            auto constexpr listSize = 20;
            auto constexpr numLists = 9;
            std::map<std::size_t, std::vector<Validator>> lists;
            // 1-based to correspond with the individually named blobs below.
            for (auto i = 1; i <= numLists; ++i)
            {
                auto& list = lists[i];
                list.reserve(listSize);
                while (list.size() < listSize)
                    list.push_back(randomValidator());
            }
            return lists;
        }();

        // Attempt an expired list (fail) and a single list (succeed)
        env.timeKeeper().set(env.timeKeeper().now() + 1s);
        auto const version = 1;
        auto const sequence1 = 1;
        auto const expiredblob = makeList(
            lists.at(1),
            sequence1,
            env.timeKeeper().now().time_since_epoch().count());
        auto const expiredSig = signList(expiredblob, pubSigningKeys1);

        NetClock::time_point const validUntil = env.timeKeeper().now() + 3600s;
        auto const sequence2 = 2;
        auto const blob2 = makeList(
            lists.at(2), sequence2, validUntil.time_since_epoch().count());
        auto const sig2 = signList(blob2, pubSigningKeys1);

        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version,
                {{expiredblob, expiredSig, {}}, {blob2, sig2, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::expired,
            ListDisposition::accepted);

        expectTrusted(lists.at(2));

        checkAvailable(
            trustedKeys, hexPublic, manifest1, version, {{blob2, sig2}});

        // Do not apply future lists, but process them
        auto const version2 = 2;
        auto const sequence7 = 7;
        auto const effective7 = validUntil - 60s;
        auto const expiration7 = effective7 + 3600s;
        auto const blob7 = makeList(
            lists.at(7),
            sequence7,
            expiration7.time_since_epoch().count(),
            effective7.time_since_epoch().count());
        auto const sig7 = signList(blob7, pubSigningKeys1);

        auto const sequence8 = 8;
        auto const effective8 = expiration7 - 60s;
        auto const expiration8 = effective8 + 3600s;
        auto const blob8 = makeList(
            lists.at(8),
            sequence8,
            expiration8.time_since_epoch().count(),
            effective8.time_since_epoch().count());
        auto const sig8 = signList(blob8, pubSigningKeys1);

        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version2,
                {{blob7, sig7, {}}, {blob8, sig8, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::pending,
            ListDisposition::pending);

        expectUntrusted(lists.at(7));
        expectUntrusted(lists.at(8));

        // Do not apply out-of-order future list, but process it
        auto const sequence6 = 6;
        auto const effective6 = effective7 - 60s;
        auto const expiration6 = effective6 + 3600s;
        auto const blob6 = makeList(
            lists.at(6),
            sequence6,
            expiration6.time_since_epoch().count(),
            effective6.time_since_epoch().count());
        auto const sig6 = signList(blob6, pubSigningKeys1);

        // Process future list that is overridden by a later list
        auto const sequence6a = 5;
        auto const effective6a = effective6 + 60s;
        auto const expiration6a = effective6a + 3600s;
        auto const blob6a = makeList(
            lists.at(5),
            sequence6a,
            expiration6a.time_since_epoch().count(),
            effective6a.time_since_epoch().count());
        auto const sig6a = signList(blob6a, pubSigningKeys1);

        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version,
                {{blob6a, sig6a, {}}, {blob6, sig6, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::pending,
            ListDisposition::pending);

        expectUntrusted(lists.at(6));
        expectTrusted(lists.at(2));

        // Do not apply re-process lists known future sequence numbers

        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version,
                {{blob7, sig7, {}}, {blob6, sig6, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::known_sequence,
            ListDisposition::known_sequence);

        expectUntrusted(lists.at(6));
        expectUntrusted(lists.at(7));
        expectTrusted(lists.at(2));

        // try empty or mangled manifest
        checkResult(
            trustedKeys->applyLists(
                "", version, {{blob7, sig7, {}}, {blob6, sig6, {}}}, siteUri),
            publisherPublic,
            ListDisposition::invalid,
            ListDisposition::invalid);

        checkResult(
            trustedKeys->applyLists(
                base64_encode("not a manifest"),
                version,
                {{blob7, sig7, {}}, {blob6, sig6, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::invalid,
            ListDisposition::invalid);

        // do not use list from untrusted publisher
        auto const untrustedManifest = base64_encode(makeManifestString(
            randomMasterKey(),
            publisherSecret,
            pubSigningKeys1.first,
            pubSigningKeys1.second,
            1));

        checkResult(
            trustedKeys->applyLists(
                untrustedManifest, version, {{blob2, sig2, {}}}, siteUri),
            publisherPublic,
            ListDisposition::untrusted,
            ListDisposition::untrusted);

        // do not use list with unhandled version
        auto const badVersion = 666;
        checkResult(
            trustedKeys->applyLists(
                manifest1, badVersion, {{blob2, sig2, {}}}, siteUri),
            publisherPublic,
            ListDisposition::unsupported_version,
            ListDisposition::unsupported_version);

        // apply list with highest sequence number
        auto const sequence3 = 3;
        auto const blob3 = makeList(
            lists.at(3), sequence3, validUntil.time_since_epoch().count());
        auto const sig3 = signList(blob3, pubSigningKeys1);

        checkResult(
            trustedKeys->applyLists(
                manifest1, version, {{blob3, sig3, {}}}, siteUri),
            publisherPublic,
            ListDisposition::accepted,
            ListDisposition::accepted);

        expectUntrusted(lists.at(1));
        expectUntrusted(lists.at(2));
        expectTrusted(lists.at(3));

        // Note that blob6a is not present, because it was dropped during
        // processing
        checkAvailable(
            trustedKeys,
            hexPublic,
            manifest1,
            2,
            {{blob3, sig3}, {blob6, sig6}, {blob7, sig7}, {blob8, sig8}});

        // do not re-apply lists with past or current sequence numbers
        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version,
                {{blob2, sig2, {}}, {blob3, sig3, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::stale,
            ListDisposition::same_sequence);

        // apply list with new publisher key updated by manifest. Also send some
        // old lists along with the old manifest
        auto const pubSigningKeys2 = randomKeyPair(KeyType::secp256k1);
        auto manifest2 = base64_encode(makeManifestString(
            publisherPublic,
            publisherSecret,
            pubSigningKeys2.first,
            pubSigningKeys2.second,
            2));

        auto const sequence4 = 4;
        auto const blob4 = makeList(
            lists.at(4), sequence4, validUntil.time_since_epoch().count());
        auto const sig4 = signList(blob4, pubSigningKeys2);

        checkResult(
            trustedKeys->applyLists(
                manifest2,
                version,
                {{blob2, sig2, manifest1},
                 {blob3, sig3, manifest1},
                 {blob4, sig4, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::stale,
            ListDisposition::accepted);

        expectUntrusted(lists.at(2));
        expectUntrusted(lists.at(3));
        expectTrusted(lists.at(4));

        checkAvailable(
            trustedKeys,
            hexPublic,
            manifest2,
            2,
            {{blob4, sig4}, {blob6, sig6}, {blob7, sig7}, {blob8, sig8}});

        auto const sequence5 = 5;
        auto const blob5 = makeList(
            lists.at(5), sequence5, validUntil.time_since_epoch().count());
        auto const badSig = signList(blob5, pubSigningKeys1);
        checkResult(
            trustedKeys->applyLists(
                manifest1, version, {{blob5, badSig, {}}}, siteUri),
            publisherPublic,
            ListDisposition::invalid,
            ListDisposition::invalid);

        expectUntrusted(lists.at(2));
        expectUntrusted(lists.at(3));
        expectTrusted(lists.at(4));
        expectUntrusted(lists.at(5));

        // Reprocess the pending list, but the signature is no longer valid
        checkResult(
            trustedKeys->applyLists(
                manifest1,
                version,
                {{blob7, sig7, {}}, {blob8, sig8, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::invalid,
            ListDisposition::invalid);

        expectTrusted(lists.at(4));
        expectUntrusted(lists.at(7));
        expectUntrusted(lists.at(8));

        // Automatically rotate the first pending already processed list using
        // updateTrusted. Note that the timekeeper is NOT moved, so the close
        // time will be ahead of the test's wall clock
        trustedKeys->updateTrusted(
            {},
            effective6 + 1s,
            env.app().getOPs(),
            env.app().overlay(),
            env.app().getHashRouter());

        expectUntrusted(lists.at(3));
        expectTrusted(lists.at(6));

        checkAvailable(
            trustedKeys,
            hexPublic,
            manifest2,
            2,
            {{blob6, sig6}, {blob7, sig7}, {blob8, sig8}});

        // Automatically rotate the LAST pending list using updateTrusted,
        // bypassing blob7. Note that the timekeeper IS moved, so the provided
        // close time will be behind the test's wall clock, and thus the wall
        // clock is used.
        env.timeKeeper().set(effective8);
        trustedKeys->updateTrusted(
            {},
            effective8 + 1s,
            env.app().getOPs(),
            env.app().overlay(),
            env.app().getHashRouter());

        expectUntrusted(lists.at(6));
        expectUntrusted(lists.at(7));
        expectTrusted(lists.at(8));

        checkAvailable(trustedKeys, hexPublic, manifest2, 2, {{blob8, sig8}});

        // resign the pending list with new key and validate it, but it's
        // already valid Also try reprocessing the pending list with an
        // explicit manifest
        // - it is still invalid
        auto const sig8_2 = signList(blob8, pubSigningKeys2);

        checkResult(
            trustedKeys->applyLists(
                manifest2,
                version,
                {{blob8, sig8, manifest1}, {blob8, sig8_2, {}}},
                siteUri),
            publisherPublic,
            ListDisposition::invalid,
            ListDisposition::same_sequence);

        expectTrusted(lists.at(8));

        checkAvailable(trustedKeys, hexPublic, manifest2, 2, {{blob8, sig8}});

        // do not apply list with revoked publisher key
        // applied list is removed due to revoked publisher key
        auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
        auto maxManifest = base64_encode(
            makeRevocationString(publisherPublic, publisherSecret));

        auto const sequence9 = 9;
        auto const blob9 = makeList(
            lists.at(9), sequence9, validUntil.time_since_epoch().count());
        auto const sig9 = signList(blob9, signingKeysMax);

        checkResult(
            trustedKeys->applyLists(
                maxManifest, version, {{blob9, sig9, {}}}, siteUri),
            publisherPublic,
            ListDisposition::untrusted,
            ListDisposition::untrusted);

        BEAST_EXPECT(!trustedKeys->trustedPublisher(publisherPublic));
        for (auto const& [num, list] : lists)
        {
            (void)num;
            expectUntrusted(list);
        }

        checkAvailable(trustedKeys, hexPublic, manifest2, 0, {});
    }

    void
    testGetAvailable()
    {
        testcase("GetAvailable");
        using namespace std::chrono_literals;

        std::string const siteUri = "testApplyList.test";

        ManifestCache manifests;
        jtx::Env env(*this);
        auto& app = env.app();
        auto trustedKeys = std::make_unique<ValidatorList>(
            manifests,
            manifests,
            env.app().timeKeeper(),
            app.config().legacy("database_path"),
            env.journal);

        auto const publisherSecret = randomSecretKey();
        auto const publisherPublic =
            derivePublicKey(KeyType::ed25519, publisherSecret);
        auto const hexPublic =
            strHex(publisherPublic.begin(), publisherPublic.end());
        auto const pubSigningKeys1 = randomKeyPair(KeyType::secp256k1);
        auto const manifest = base64_encode(makeManifestString(
            publisherPublic,
            publisherSecret,
            pubSigningKeys1.first,
            pubSigningKeys1.second,
            1));

        std::vector<std::string> cfgKeys1({strHex(publisherPublic)});
        std::vector<std::string> emptyCfgKeys;

        BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgKeys1));

        std::vector<Validator> const list = []() {
            auto constexpr listSize = 20;
            std::vector<Validator> list;
            list.reserve(listSize);
            while (list.size() < listSize)
                list.push_back(randomValidator());
            return list;
        }();

        // Process a list
        env.timeKeeper().set(env.timeKeeper().now() + 1s);
        NetClock::time_point const validUntil = env.timeKeeper().now() + 3600s;
        auto const blob =
            makeList(list, 1, validUntil.time_since_epoch().count());
        auto const sig = signList(blob, pubSigningKeys1);

        {
            // list unavailable
            auto const available = trustedKeys->getAvailable(hexPublic);
            BEAST_EXPECT(!available);
        }

        BEAST_EXPECT(
            trustedKeys->applyLists(manifest, 1, {{blob, sig, {}}}, siteUri)
                .bestDisposition() == ListDisposition::accepted);

        {
            // invalid public key
            auto const available =
                trustedKeys->getAvailable(hexPublic + "invalid", 1);
            BEAST_EXPECT(!available);
        }

        {
            // unknown public key
            auto const badSecret = randomSecretKey();
            auto const badPublic = derivePublicKey(KeyType::ed25519, badSecret);
            auto const hexBad = strHex(badPublic.begin(), badPublic.end());

            auto const available = trustedKeys->getAvailable(hexBad, 1);
            BEAST_EXPECT(!available);
        }
        {
            // bad version 0
            auto const available = trustedKeys->getAvailable(hexPublic, 0);
            if (BEAST_EXPECT(available))
            {
                auto const& a = *available;
                BEAST_EXPECT(!a);
            }
        }
        {
            // bad version 3
            auto const available = trustedKeys->getAvailable(hexPublic, 3);
            if (BEAST_EXPECT(available))
            {
                auto const& a = *available;
                BEAST_EXPECT(!a);
            }
        }
        {
            // version 1
            auto const available = trustedKeys->getAvailable(hexPublic, 1);
            if (BEAST_EXPECT(available))
            {
                auto const& a = *available;
                BEAST_EXPECT(a[jss::public_key] == hexPublic);
                BEAST_EXPECT(a[jss::manifest] == manifest);
                BEAST_EXPECT(a[jss::version] == 1);

                BEAST_EXPECT(a[jss::blob] == blob);
                BEAST_EXPECT(a[jss::signature] == sig);
                BEAST_EXPECT(!a.isMember(jss::blobs_v2));
            }
        }

        {
            // version 2
            auto const available = trustedKeys->getAvailable(hexPublic, 2);
            if (BEAST_EXPECT(available))
            {
                auto const& a = *available;
                BEAST_EXPECT(a[jss::public_key] == hexPublic);
                BEAST_EXPECT(a[jss::manifest] == manifest);
                BEAST_EXPECT(a[jss::version] == 2);

                if (BEAST_EXPECT(a.isMember(jss::blobs_v2)))
                {
                    BEAST_EXPECT(!a.isMember(jss::blob));
                    BEAST_EXPECT(!a.isMember(jss::signature));
                    auto const& blobs_v2 = a[jss::blobs_v2];
                    BEAST_EXPECT(blobs_v2.isArray() && blobs_v2.size() == 1);

                    BEAST_EXPECT(blobs_v2[0u][jss::blob] == blob);
                    BEAST_EXPECT(blobs_v2[0u][jss::signature] == sig);
                }
            }
        }
    }

    void
    testUpdateTrusted()
    {
        testcase("Update trusted");

        std::string const siteUri = "testUpdateTrusted.test";

        ManifestCache manifestsOuter;
        jtx::Env env(*this);
        auto& app = env.app();
        auto trustedKeysOuter = std::make_unique<ValidatorList>(
            manifestsOuter,
            manifestsOuter,
            env.timeKeeper(),
            app.config().legacy("database_path"),
            env.journal);

        std::vector<std::string> cfgPublishersOuter;
        hash_set<NodeID> activeValidatorsOuter;

        std::size_t const maxKeys = 40;
        {
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(maxKeys);
            hash_set<NodeID> unseenValidators;

            while (cfgKeys.size() != maxKeys)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back(toBase58(TokenType::NodePublic, valKey));
                if (cfgKeys.size() <= maxKeys - 5)
                    activeValidatorsOuter.emplace(calcNodeID(valKey));
                else
                    unseenValidators.emplace(calcNodeID(valKey));
            }

            BEAST_EXPECT(
                trustedKeysOuter->load({}, cfgKeys, cfgPublishersOuter));

            // updateTrusted should make all configured validators trusted
            // even if they are not active/seen
            TrustChanges changes = trustedKeysOuter->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());

            for (auto const& val : unseenValidators)
                activeValidatorsOuter.emplace(val);

            BEAST_EXPECT(changes.added == activeValidatorsOuter);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                trustedKeysOuter->quorum() == std::ceil(cfgKeys.size() * 0.8f));
            for (auto const& val : cfgKeys)
            {
                if (auto const valKey =
                        parseBase58<PublicKey>(TokenType::NodePublic, val))
                {
                    BEAST_EXPECT(trustedKeysOuter->listed(*valKey));
                    BEAST_EXPECT(trustedKeysOuter->trusted(*valKey));
                }
                else
                    fail();
            }

            changes = trustedKeysOuter->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                trustedKeysOuter->quorum() == std::ceil(cfgKeys.size() * 0.8f));
        }
        {
            // update with manifests
            auto const masterPrivate = randomSecretKey();
            auto const masterPublic =
                derivePublicKey(KeyType::ed25519, masterPrivate);

            std::vector<std::string> cfgKeys(
                {toBase58(TokenType::NodePublic, masterPublic)});

            BEAST_EXPECT(
                trustedKeysOuter->load({}, cfgKeys, cfgPublishersOuter));

            auto const signingKeys1 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic1 = signingKeys1.first;
            activeValidatorsOuter.emplace(calcNodeID(masterPublic));

            // Should not trust ephemeral signing key if there is no manifest
            TrustChanges changes = trustedKeysOuter->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.added == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                trustedKeysOuter->quorum() == std::ceil((maxKeys + 1) * 0.8f));
            BEAST_EXPECT(trustedKeysOuter->listed(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->trusted(masterPublic));
            BEAST_EXPECT(!trustedKeysOuter->listed(signingPublic1));
            BEAST_EXPECT(!trustedKeysOuter->trusted(signingPublic1));

            // Should trust the ephemeral signing key from the applied manifest
            auto m1 = deserializeManifest(makeManifestString(
                masterPublic,
                masterPrivate,
                signingPublic1,
                signingKeys1.second,
                1));

            BEAST_EXPECT(
                manifestsOuter.applyManifest(std::move(*m1)) ==
                ManifestDisposition::accepted);
            BEAST_EXPECT(trustedKeysOuter->listed(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->trusted(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->listed(signingPublic1));
            BEAST_EXPECT(trustedKeysOuter->trusted(signingPublic1));

            // Should only trust the ephemeral signing key
            // from the newest applied manifest
            auto const signingKeys2 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic2 = signingKeys2.first;
            auto m2 = deserializeManifest(makeManifestString(
                masterPublic,
                masterPrivate,
                signingPublic2,
                signingKeys2.second,
                2));
            BEAST_EXPECT(
                manifestsOuter.applyManifest(std::move(*m2)) ==
                ManifestDisposition::accepted);
            BEAST_EXPECT(trustedKeysOuter->listed(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->trusted(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->listed(signingPublic2));
            BEAST_EXPECT(trustedKeysOuter->trusted(signingPublic2));
            BEAST_EXPECT(!trustedKeysOuter->listed(signingPublic1));
            BEAST_EXPECT(!trustedKeysOuter->trusted(signingPublic1));

            // Should not trust keys from revoked master public key
            auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
            auto const signingPublicMax = signingKeysMax.first;
            activeValidatorsOuter.emplace(calcNodeID(signingPublicMax));
            auto mMax = deserializeManifest(
                makeRevocationString(masterPublic, masterPrivate));

            BEAST_EXPECT(mMax->revoked());
            BEAST_EXPECT(
                manifestsOuter.applyManifest(std::move(*mMax)) ==
                ManifestDisposition::accepted);
            BEAST_EXPECT(
                manifestsOuter.getSigningKey(masterPublic) == masterPublic);
            BEAST_EXPECT(manifestsOuter.revoked(masterPublic));

            // Revoked key remains trusted until list is updated
            BEAST_EXPECT(trustedKeysOuter->listed(masterPublic));
            BEAST_EXPECT(trustedKeysOuter->trusted(masterPublic));

            changes = trustedKeysOuter->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(
                trustedKeysOuter->quorum() == std::ceil(maxKeys * 0.8f));
            BEAST_EXPECT(trustedKeysOuter->listed(masterPublic));
            BEAST_EXPECT(!trustedKeysOuter->trusted(masterPublic));
            BEAST_EXPECT(!trustedKeysOuter->listed(signingPublicMax));
            BEAST_EXPECT(!trustedKeysOuter->trusted(signingPublicMax));
            BEAST_EXPECT(!trustedKeysOuter->listed(signingPublic2));
            BEAST_EXPECT(!trustedKeysOuter->trusted(signingPublic2));
            BEAST_EXPECT(!trustedKeysOuter->listed(signingPublic1));
            BEAST_EXPECT(!trustedKeysOuter->trusted(signingPublic1));
        }
        {
            // Make quorum unattainable if lists from any publishers are
            // unavailable
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifestsOuter,
                manifestsOuter,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);
            auto const publisherSecret = randomSecretKey();
            auto const publisherPublic =
                derivePublicKey(KeyType::ed25519, publisherSecret);

            std::vector<std::string> cfgPublishers({strHex(publisherPublic)});
            std::vector<std::string> emptyCfgKeys;

            BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgPublishers));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(
                trustedKeys->quorum() ==
                std::numeric_limits<std::size_t>::max());
        }
        {
            // Trust explicitly listed validators also when list threshold is
            // higher than 1
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifestsOuter,
                manifestsOuter,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);
            auto const masterPrivate = randomSecretKey();
            auto const masterPublic =
                derivePublicKey(KeyType::ed25519, masterPrivate);
            std::vector<std::string> cfgKeys(
                {toBase58(TokenType::NodePublic, masterPublic)});

            auto const publisher1Secret = randomSecretKey();
            auto const publisher1Public =
                derivePublicKey(KeyType::ed25519, publisher1Secret);
            auto const publisher2Secret = randomSecretKey();
            auto const publisher2Public =
                derivePublicKey(KeyType::ed25519, publisher2Secret);
            std::vector<std::string> cfgPublishers(
                {strHex(publisher1Public), strHex(publisher2Public)});

            BEAST_EXPECT(
                trustedKeys->load({}, cfgKeys, cfgPublishers, std::size_t(2)));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidatorsOuter,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.size() == 1);
            BEAST_EXPECT(trustedKeys->listed(masterPublic));
            BEAST_EXPECT(trustedKeys->trusted(masterPublic));
        }
        {
            // Should use custom minimum quorum
            std::size_t const minQuorum = 1;
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal,
                minQuorum);

            std::size_t n = 10;
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(n);
            hash_set<NodeID> expectedTrusted;
            hash_set<NodeID> activeValidators;
            NodeID toBeSeen;

            while (cfgKeys.size() < n)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back(toBase58(TokenType::NodePublic, valKey));
                expectedTrusted.emplace(calcNodeID(valKey));
                if (cfgKeys.size() < std::ceil(n * 0.8f))
                    activeValidators.emplace(calcNodeID(valKey));
                else if (cfgKeys.size() < std::ceil(n * 0.8f))
                    toBeSeen = calcNodeID(valKey);
            }

            BEAST_EXPECT(trustedKeys->load({}, cfgKeys, cfgPublishersOuter));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == expectedTrusted);
            BEAST_EXPECT(trustedKeys->quorum() == minQuorum);

            // Use configured quorum even when seen validators >= quorum
            activeValidators.emplace(toBeSeen);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum() == minQuorum);
        }
        {
            // Remove expired published list
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifestsOuter,
                manifestsOuter,
                env.app().timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            std::vector<std::string> emptyCfgKeys;
            auto const publisherKeys = randomKeyPair(KeyType::secp256k1);
            auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
            auto const manifest = base64_encode(makeManifestString(
                publisherKeys.first,
                publisherKeys.second,
                pubSigningKeys.first,
                pubSigningKeys.second,
                1));

            std::vector<std::string> cfgKeys({strHex(publisherKeys.first)});

            BEAST_EXPECT(trustedKeys->load({}, emptyCfgKeys, cfgKeys));

            std::vector<Validator> list({randomValidator(), randomValidator()});
            hash_set<NodeID> activeValidators(
                asNodeIDs({list[0].masterPublic, list[1].masterPublic}));

            // do not apply expired list
            auto const version = 1;
            auto const sequence = 1;
            using namespace std::chrono_literals;
            NetClock::time_point const validUntil =
                env.timeKeeper().now() + 60s;
            auto const blob =
                makeList(list, sequence, validUntil.time_since_epoch().count());
            auto const sig = signList(blob, pubSigningKeys);

            BEAST_EXPECT(
                ListDisposition::accepted ==
                trustedKeys
                    ->applyLists(manifest, version, {{blob, sig, {}}}, siteUri)
                    .bestDisposition());

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == activeValidators);
            for (Validator const& val : list)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.signingPublic));
            }
            BEAST_EXPECT(trustedKeys->quorum() == 2);

            env.timeKeeper().set(validUntil);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed == activeValidators);
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(!trustedKeys->trusted(list[0].masterPublic));
            BEAST_EXPECT(!trustedKeys->trusted(list[1].masterPublic));
            BEAST_EXPECT(
                trustedKeys->quorum() ==
                std::numeric_limits<std::size_t>::max());

            // (Re)trust validators from new valid list
            std::vector<Validator> list2({list[0], randomValidator()});
            activeValidators.insert(calcNodeID(list2[1].masterPublic));
            auto const sequence2 = 2;
            NetClock::time_point const expiration2 =
                env.timeKeeper().now() + 60s;
            auto const blob2 = makeList(
                list2, sequence2, expiration2.time_since_epoch().count());
            auto const sig2 = signList(blob2, pubSigningKeys);

            BEAST_EXPECT(
                ListDisposition::accepted ==
                trustedKeys
                    ->applyLists(
                        manifest, version, {{blob2, sig2, {}}}, siteUri)
                    .bestDisposition());

            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                changes.added ==
                asNodeIDs({list2[0].masterPublic, list2[1].masterPublic}));
            for (Validator const& val : list2)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.signingPublic));
            }
            BEAST_EXPECT(!trustedKeys->trusted(list[1].masterPublic));
            BEAST_EXPECT(!trustedKeys->trusted(list[1].signingPublic));
            BEAST_EXPECT(trustedKeys->quorum() == 2);
        }
        {
            // Test 1-9 configured validators
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifestsOuter,
                manifestsOuter,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;

            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back(toBase58(TokenType::NodePublic, valKey));
                activeValidators.emplace(calcNodeID(valKey));
                activeKeys.emplace(valKey);
                BEAST_EXPECT(trustedKeys->load({}, cfgKeys, cfgPublishers));
                TrustChanges changes = trustedKeys->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(changes.removed.empty());
                BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                BEAST_EXPECT(
                    trustedKeys->quorum() == std::ceil(cfgKeys.size() * 0.8f));
                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted(key));
            }
        }
        {
            // Test 2-9 configured validators as validator
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifestsOuter,
                manifestsOuter,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            auto const localKey = randomNode();
            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;
            std::vector<std::string> cfgKeys{
                toBase58(TokenType::NodePublic, localKey)};
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back(toBase58(TokenType::NodePublic, valKey));
                activeValidators.emplace(calcNodeID(valKey));
                activeKeys.emplace(valKey);

                BEAST_EXPECT(
                    trustedKeys->load(localKey, cfgKeys, cfgPublishers));
                TrustChanges changes = trustedKeys->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(changes.removed.empty());
                if (cfgKeys.size() > 2)
                    BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                else
                    BEAST_EXPECT(
                        changes.added == asNodeIDs({localKey, valKey}));

                BEAST_EXPECT(
                    trustedKeys->quorum() == std::ceil(cfgKeys.size() * 0.8f));

                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted(key));
            }
        }
        {
            // Trusted set should include all validators from multiple lists
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            hash_set<NodeID> activeValidators;
            std::vector<Validator> valKeys;
            valKeys.reserve(maxKeys);

            while (valKeys.size() != maxKeys)
            {
                valKeys.push_back(randomValidator());
                activeValidators.emplace(
                    calcNodeID(valKeys.back().masterPublic));
            }

            // locals[0]: from 0 to maxKeys - 4
            // locals[1]: from 1 to maxKeys - 2
            // locals[2]: from 2 to maxKeys
            constexpr static int publishers = 3;
            std::array<
                std::pair<
                    decltype(valKeys)::const_iterator,
                    decltype(valKeys)::const_iterator>,
                publishers>
                locals = {
                    std::make_pair(valKeys.cbegin(), valKeys.cend() - 4),
                    std::make_pair(valKeys.cbegin() + 1, valKeys.cend() - 2),
                    std::make_pair(valKeys.cbegin() + 2, valKeys.cend()),
                };

            auto addPublishedList = [&, this](int i) {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = base64_encode(makeManifestString(
                    publisherPublic,
                    publisherSecret,
                    pubSigningKeys.first,
                    pubSigningKeys.second,
                    1));

                std::vector<std::string> cfgPublishers(
                    {strHex(publisherPublic)});
                std::vector<std::string> emptyCfgKeys;

                // Threshold of 1 will result in a union of all the lists
                BEAST_EXPECT(trustedKeys->load(
                    {}, emptyCfgKeys, cfgPublishers, std::size_t(1)));

                auto const version = 1;
                auto const sequence = 1;
                using namespace std::chrono_literals;
                NetClock::time_point const validUntil =
                    env.timeKeeper().now() + 3600s;
                std::vector<Validator> localKeys{
                    locals[i].first, locals[i].second};
                auto const blob = makeList(
                    localKeys, sequence, validUntil.time_since_epoch().count());
                auto const sig = signList(blob, pubSigningKeys);

                BEAST_EXPECT(
                    ListDisposition::accepted ==
                    trustedKeys
                        ->applyLists(
                            manifest, version, {{blob, sig, {}}}, siteUri)
                        .bestDisposition());
            };

            // Apply multiple published lists
            for (auto i = 0; i < publishers; ++i)
                addPublishedList(i);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());

            BEAST_EXPECT(
                trustedKeys->quorum() == std::ceil(valKeys.size() * 0.8f));

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());
        }
        {
            // Trusted set should include validators from intersection of lists
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            hash_set<NodeID> activeValidators;
            std::vector<Validator> valKeys;
            valKeys.reserve(maxKeys);

            while (valKeys.size() != maxKeys)
            {
                valKeys.push_back(randomValidator());
                activeValidators.emplace(
                    calcNodeID(valKeys.back().masterPublic));
            }

            // locals[0]: from 0 to maxKeys - 4
            // locals[1]: from 1 to maxKeys - 2
            // locals[2]: from 2 to maxKeys
            // interesection of at least 2: same as locals[1]
            // intersection when 1 is dropped: from 2 to maxKeys - 4
            constexpr static int publishers = 3;
            std::array<
                std::pair<
                    decltype(valKeys)::const_iterator,
                    decltype(valKeys)::const_iterator>,
                publishers>
                locals = {
                    std::make_pair(valKeys.cbegin(), valKeys.cend() - 4),
                    std::make_pair(valKeys.cbegin() + 1, valKeys.cend() - 2),
                    std::make_pair(valKeys.cbegin() + 2, valKeys.cend()),
                };

            auto addPublishedList = [&, this](
                                        int i,
                                        NetClock::time_point& validUntil1,
                                        NetClock::time_point& validUntil2) {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = base64_encode(makeManifestString(
                    publisherPublic,
                    publisherSecret,
                    pubSigningKeys.first,
                    pubSigningKeys.second,
                    1));

                std::vector<std::string> cfgPublishers(
                    {strHex(publisherPublic)});
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(
                    trustedKeys->load({}, emptyCfgKeys, cfgPublishers));

                auto const version = 1;
                auto const sequence = 1;
                using namespace std::chrono_literals;
                // Want to drop 1 sooner
                NetClock::time_point const validUntil = env.timeKeeper().now() +
                    (i == 2       ? 120s
                         : i == 1 ? 60s
                                  : 3600s);
                if (i == 1)
                    validUntil1 = validUntil;
                else if (i == 2)
                    validUntil2 = validUntil;
                std::vector<Validator> localKeys{
                    locals[i].first, locals[i].second};
                auto const blob = makeList(
                    localKeys, sequence, validUntil.time_since_epoch().count());
                auto const sig = signList(blob, pubSigningKeys);

                BEAST_EXPECT(
                    ListDisposition::accepted ==
                    trustedKeys
                        ->applyLists(
                            manifest, version, {{blob, sig, {}}}, siteUri)
                        .bestDisposition());
            };

            // Apply multiple published lists
            // validUntil1 is expiration time for locals[1]
            NetClock::time_point validUntil1, validUntil2;
            for (auto i = 0; i < publishers; ++i)
                addPublishedList(i, validUntil1, validUntil2);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());

            BEAST_EXPECT(
                trustedKeys->quorum() ==
                std::ceil((valKeys.size() - 3) * 0.8f));

            for (auto const& val : valKeys)
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));

            hash_set<NodeID> added;
            for (std::size_t i = 0; i < maxKeys; ++i)
            {
                auto const& val = valKeys[i];
                if (i >= 1 && i < maxKeys - 2)
                {
                    BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                    added.insert(calcNodeID(val.masterPublic));
                }
                else
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire locals[1]
            env.timeKeeper().set(validUntil1);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());

            BEAST_EXPECT(
                trustedKeys->quorum() ==
                std::ceil((valKeys.size() - 6) * 0.8f));

            for (auto const& val : valKeys)
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));

            hash_set<NodeID> removed;
            for (std::size_t i = 0; i < maxKeys; ++i)
            {
                auto const& val = valKeys[i];
                if (i >= 2 && i < maxKeys - 4)
                    BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                else
                {
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    if (i >= 1 && i < maxKeys - 2)
                        removed.insert(calcNodeID(val.masterPublic));
                }
            }

            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);

            // Expire locals[2], which removes all validators
            env.timeKeeper().set(validUntil2);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());

            BEAST_EXPECT(
                trustedKeys->quorum() ==
                std::numeric_limits<std::size_t>::max());

            removed.clear();
            for (std::size_t i = 0; i < maxKeys; ++i)
            {
                auto const& val = valKeys[i];
                if (i < maxKeys - 4)
                    BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                else
                    BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));

                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                if (i >= 2 && i < maxKeys - 4)
                    removed.insert(calcNodeID(val.masterPublic));
            }

            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
    }

    void
    testExpires()
    {
        testcase("Expires");

        std::string const siteUri = "testExpires.test";

        jtx::Env env(*this);
        auto& app = env.app();

        auto toStr = [](PublicKey const& publicKey) {
            return toBase58(TokenType::NodePublic, publicKey);
        };

        // Config listed keys
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            // Empty list has no expiration
            BEAST_EXPECT(trustedKeys->expires() == std::nullopt);

            // Config listed keys have maximum expiry
            PublicKey localCfgListed = randomNode();
            trustedKeys->load({}, {toStr(localCfgListed)}, {});
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().value() == NetClock::time_point::max());
            BEAST_EXPECT(trustedKeys->listed(localCfgListed));
        }

        // Published keys with expirations
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests,
                manifests,
                env.app().timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            std::vector<Validator> validators = {randomValidator()};
            hash_set<NodeID> activeValidators;
            for (Validator const& val : validators)
                activeValidators.insert(calcNodeID(val.masterPublic));
            // Store prepared list data to control when it is applied
            struct PreparedList
            {
                PublicKey publisherPublic;
                std::string manifest;
                std::vector<ValidatorBlobInfo> blobs;
                int version;
                std::vector<NetClock::time_point> expirations;
            };

            using namespace std::chrono_literals;
            auto addPublishedList = [this, &env, &trustedKeys, &validators]() {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = base64_encode(makeManifestString(
                    publisherPublic,
                    publisherSecret,
                    pubSigningKeys.first,
                    pubSigningKeys.second,
                    1));

                std::vector<std::string> cfgPublishers(
                    {strHex(publisherPublic)});
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(
                    trustedKeys->load({}, emptyCfgKeys, cfgPublishers));

                auto const version = 2;
                auto const sequence1 = 1;
                NetClock::time_point const expiration1 =
                    env.timeKeeper().now() + 1800s;
                auto const blob1 = makeList(
                    validators,
                    sequence1,
                    expiration1.time_since_epoch().count());
                auto const sig1 = signList(blob1, pubSigningKeys);

                NetClock::time_point const effective2 = expiration1 - 300s;
                NetClock::time_point const expiration2 = effective2 + 1800s;
                auto const sequence2 = 2;
                auto const blob2 = makeList(
                    validators,
                    sequence2,
                    expiration2.time_since_epoch().count(),
                    effective2.time_since_epoch().count());
                auto const sig2 = signList(blob2, pubSigningKeys);

                return PreparedList{
                    publisherPublic,
                    manifest,
                    {{blob1, sig1, {}}, {blob2, sig2, {}}},
                    version,
                    {expiration1, expiration2}};
            };

            // Configure two publishers and prepare 2 lists
            PreparedList prep1 = addPublishedList();
            env.timeKeeper().set(env.timeKeeper().now() + 200s);
            PreparedList prep2 = addPublishedList();

            // Initially, no list has been published, so no known expiration
            BEAST_EXPECT(trustedKeys->expires() == std::nullopt);

            // Apply first list
            checkResult(
                trustedKeys->applyLists(
                    prep1.manifest, prep1.version, prep1.blobs, siteUri),
                prep1.publisherPublic,
                ListDisposition::pending,
                ListDisposition::accepted);

            // One list still hasn't published, so expiration is still
            // unknown
            BEAST_EXPECT(trustedKeys->expires() == std::nullopt);

            // Apply second list
            checkResult(
                trustedKeys->applyLists(
                    prep2.manifest, prep2.version, prep2.blobs, siteUri),
                prep2.publisherPublic,
                ListDisposition::pending,
                ListDisposition::accepted);
            // We now have loaded both lists, so expiration is known
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().value() == prep1.expirations.back());

            // Advance past the first list's LAST validFrom date. It remains
            // the earliest validUntil, while rotating in the second list
            {
                env.timeKeeper().set(prep1.expirations.front() - 1s);
                auto changes = trustedKeys->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(
                    trustedKeys->expires() &&
                    trustedKeys->expires().value() == prep1.expirations.back());
                BEAST_EXPECT(!changes.added.empty());
                BEAST_EXPECT(changes.removed.empty());
            }

            // Advance past the first list's LAST validUntil, but it remains
            // the earliest validUntil, while being invalidated
            {
                env.timeKeeper().set(prep1.expirations.back() + 1s);
                auto changes = trustedKeys->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(
                    trustedKeys->expires() &&
                    trustedKeys->expires().value() == prep1.expirations.back());
                BEAST_EXPECT(changes.added.empty());
                BEAST_EXPECT(changes.removed.empty());
            }
        }
    }

    void
    testNegativeUNL()
    {
        testcase("NegativeUNL");
        jtx::Env env(*this);
        ManifestCache manifests;

        auto createValidatorList =
            [&](std::uint32_t vlSize,
                std::optional<std::size_t> minimumQuorum = {})
            -> std::shared_ptr<ValidatorList> {
            auto trustedKeys = std::make_shared<ValidatorList>(
                manifests,
                manifests,
                env.timeKeeper(),
                env.app().config().legacy("database_path"),
                env.journal,
                minimumQuorum);

            std::vector<std::string> cfgPublishers;
            std::vector<std::string> cfgKeys;
            hash_set<NodeID> activeValidators;
            cfgKeys.reserve(vlSize);
            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back(toBase58(TokenType::NodePublic, valKey));
                activeValidators.emplace(calcNodeID(valKey));
            }
            if (trustedKeys->load({}, cfgKeys, cfgPublishers))
            {
                trustedKeys->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                if (minimumQuorum == trustedKeys->quorum() ||
                    trustedKeys->quorum() == std::ceil(cfgKeys.size() * 0.8f))
                    return trustedKeys;
            }
            return nullptr;
        };

        /*
         * Test NegativeUNL
         * == Combinations ==
         * -- UNL size: 34, 35, 57
         * -- nUNL size: 0%, 20%, 30%, 50%
         *
         * == with UNL size 60
         * -- set == get,
         * -- check quorum, with nUNL size: 0, 12, 30, 18
         * -- nUNL overlap: |nUNL - UNL| = 5, with nUNL size: 18
         * -- with command line minimumQuorum = 50%,
         *    seen_reliable affected by nUNL
         */

        {
            hash_set<NodeID> activeValidators;
            //== Combinations ==
            std::array<std::uint32_t, 4> unlSizes = {34, 35, 39, 60};
            std::array<std::uint32_t, 4> nUnlPercent = {0, 20, 30, 50};
            for (auto us : unlSizes)
            {
                for (auto np : nUnlPercent)
                {
                    auto validators = createValidatorList(us);
                    BEAST_EXPECT(validators);
                    if (validators)
                    {
                        std::uint32_t nUnlSize = us * np / 100;
                        auto unl = validators->getTrustedMasterKeys();
                        hash_set<PublicKey> nUnl;
                        auto it = unl.begin();
                        for (std::uint32_t i = 0; i < nUnlSize; ++i)
                        {
                            nUnl.insert(*it);
                            ++it;
                        }
                        validators->setNegativeUNL(nUnl);
                        validators->updateTrusted(
                            activeValidators,
                            env.timeKeeper().now(),
                            env.app().getOPs(),
                            env.app().overlay(),
                            env.app().getHashRouter());
                        BEAST_EXPECT(
                            validators->quorum() ==
                            static_cast<std::size_t>(std::ceil(
                                std::max((us - nUnlSize) * 0.8f, us * 0.6f))));
                    }
                }
            }
        }

        {
            //== with UNL size 60
            auto validators = createValidatorList(60);
            BEAST_EXPECT(validators);
            if (validators)
            {
                hash_set<NodeID> activeValidators;
                auto unl = validators->getTrustedMasterKeys();
                BEAST_EXPECT(unl.size() == 60);
                {
                    //-- set == get,
                    //-- check quorum, with nUNL size: 0, 30, 18, 12
                    auto nUnlChange = [&](std::uint32_t nUnlSize,
                                          std::uint32_t quorum) -> bool {
                        hash_set<PublicKey> nUnl;
                        auto it = unl.begin();
                        for (std::uint32_t i = 0; i < nUnlSize; ++i)
                        {
                            nUnl.insert(*it);
                            ++it;
                        }
                        validators->setNegativeUNL(nUnl);
                        auto nUnl_temp = validators->getNegativeUNL();
                        if (nUnl_temp.size() == nUnl.size())
                        {
                            for (auto& n : nUnl_temp)
                            {
                                if (nUnl.find(n) == nUnl.end())
                                    return false;
                            }
                            validators->updateTrusted(
                                activeValidators,
                                env.timeKeeper().now(),
                                env.app().getOPs(),
                                env.app().overlay(),
                                env.app().getHashRouter());
                            return validators->quorum() == quorum;
                        }
                        return false;
                    };
                    BEAST_EXPECT(nUnlChange(0, 48));
                    BEAST_EXPECT(nUnlChange(30, 36));
                    BEAST_EXPECT(nUnlChange(18, 36));
                    BEAST_EXPECT(nUnlChange(12, 39));
                }

                {
                    // nUNL overlap: |nUNL - UNL| = 5, with nUNL size:
                    // 18
                    auto nUnl = validators->getNegativeUNL();
                    BEAST_EXPECT(nUnl.size() == 12);
                    std::size_t ss = 33;
                    std::vector<uint8_t> data(ss, 0);
                    data[0] = 0xED;
                    for (int i = 0; i < 6; ++i)
                    {
                        Slice s(data.data(), ss);
                        data[1]++;
                        nUnl.emplace(s);
                    }
                    validators->setNegativeUNL(nUnl);
                    validators->updateTrusted(
                        activeValidators,
                        env.timeKeeper().now(),
                        env.app().getOPs(),
                        env.app().overlay(),
                        env.app().getHashRouter());
                    BEAST_EXPECT(validators->quorum() == 39);
                }
            }
        }

        {
            //== with UNL size 60
            //-- with command line minimumQuorum = 50%,
            //   seen_reliable affected by nUNL
            auto validators = createValidatorList(60, 30);
            BEAST_EXPECT(validators);
            if (validators)
            {
                hash_set<NodeID> activeValidators;
                hash_set<PublicKey> unl = validators->getTrustedMasterKeys();
                auto it = unl.begin();
                for (std::uint32_t i = 0; i < 50; ++i)
                {
                    activeValidators.insert(calcNodeID(*it));
                    ++it;
                }
                validators->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(validators->quorum() == 30);
                hash_set<PublicKey> nUnl;
                it = unl.begin();
                for (std::uint32_t i = 0; i < 20; ++i)
                {
                    nUnl.insert(*it);
                    ++it;
                }
                validators->setNegativeUNL(nUnl);
                validators->updateTrusted(
                    activeValidators,
                    env.timeKeeper().now(),
                    env.app().getOPs(),
                    env.app().overlay(),
                    env.app().getHashRouter());
                BEAST_EXPECT(validators->quorum() == 30);
            }
        }
    }

    void
    testSha512Hash()
    {
        testcase("Sha512 hashing");
        // Tests that ValidatorList hash_append helpers with a single blob
        // returns the same result as ripple::Sha512Half used by the
        // TMValidatorList protocol message handler
        std::string const manifest = "This is not really a manifest";
        std::string const blob = "This is not really a blob";
        std::string const signature = "This is not really a signature";
        std::uint32_t const version = 1;

        auto const global = sha512Half(manifest, blob, signature, version);
        BEAST_EXPECT(!!global);

        std::vector<ValidatorBlobInfo> blobVector(1);
        blobVector[0].blob = blob;
        blobVector[0].signature = signature;
        BEAST_EXPECT(global == sha512Half(manifest, blobVector, version));
        BEAST_EXPECT(global != sha512Half(signature, blobVector, version));

        {
            std::map<std::size_t, ValidatorBlobInfo> blobMap{
                {99, blobVector[0]}};
            BEAST_EXPECT(global == sha512Half(manifest, blobMap, version));
            BEAST_EXPECT(global != sha512Half(blob, blobMap, version));
        }

        {
            protocol::TMValidatorList msg1;
            msg1.set_manifest(manifest);
            msg1.set_blob(blob);
            msg1.set_signature(signature);
            msg1.set_version(version);
            BEAST_EXPECT(global == sha512Half(msg1));
            msg1.set_signature(blob);
            BEAST_EXPECT(global != sha512Half(msg1));
        }

        {
            protocol::TMValidatorListCollection msg2;
            msg2.set_manifest(manifest);
            msg2.set_version(version);
            auto& bi = *msg2.add_blobs();
            bi.set_blob(blob);
            bi.set_signature(signature);
            BEAST_EXPECT(global == sha512Half(msg2));
            bi.set_manifest(manifest);
            BEAST_EXPECT(global != sha512Half(msg2));
        }
    }

    void
    testBuildMessages()
    {
        testcase("Build and split messages");

        std::uint32_t const manifestCutoff = 7;
        auto extractHeader = [this](Message& message) {
            auto const& buffer =
                message.getBuffer(compression::Compressed::Off);

            boost::beast::multi_buffer buffers;

            // simulate multi-buffer
            auto start = buffer.begin();
            auto end = buffer.end();
            std::vector<std::uint8_t> slice(start, end);
            buffers.commit(boost::asio::buffer_copy(
                buffers.prepare(slice.size()), boost::asio::buffer(slice)));

            boost::system::error_code ec;
            auto header =
                detail::parseMessageHeader(ec, buffers.data(), buffers.size());
            BEAST_EXPECT(!ec);
            return std::make_pair(header, buffers);
        };
        auto extractProtocolMessage1 = [this,
                                        &extractHeader](Message& message) {
            auto [header, buffers] = extractHeader(message);
            if (BEAST_EXPECT(header) &&
                BEAST_EXPECT(header->message_type == protocol::mtVALIDATORLIST))
            {
                auto const msg =
                    detail::parseMessageContent<protocol::TMValidatorList>(
                        *header, buffers.data());
                BEAST_EXPECT(msg);
                return msg;
            }
            return std::shared_ptr<protocol::TMValidatorList>();
        };
        auto extractProtocolMessage2 = [this,
                                        &extractHeader](Message& message) {
            auto [header, buffers] = extractHeader(message);
            if (BEAST_EXPECT(header) &&
                BEAST_EXPECT(
                    header->message_type ==
                    protocol::mtVALIDATORLISTCOLLECTION))
            {
                auto const msg = detail::parseMessageContent<
                    protocol::TMValidatorListCollection>(
                    *header, buffers.data());
                BEAST_EXPECT(msg);
                return msg;
            }
            return std::shared_ptr<protocol::TMValidatorListCollection>();
        };
        auto verifyMessage =
            [this,
             manifestCutoff,
             &extractProtocolMessage1,
             &extractProtocolMessage2](
                auto const version,
                auto const& manifest,
                auto const& blobInfos,
                auto const& messages,
                std::vector<std::pair<std::size_t, std::vector<std::uint32_t>>>
                    expectedInfo) {
                BEAST_EXPECT(messages.size() == expectedInfo.size());
                auto msgIter = expectedInfo.begin();
                for (auto const& messageWithHash : messages)
                {
                    if (!BEAST_EXPECT(msgIter != expectedInfo.end()))
                        break;
                    if (!BEAST_EXPECT(messageWithHash.message))
                        continue;
                    auto const& expectedSeqs = msgIter->second;
                    auto seqIter = expectedSeqs.begin();
                    auto const size =
                        messageWithHash.message
                            ->getBuffer(compression::Compressed::Off)
                            .size();
                    // This size is arbitrary, but shouldn't change
                    BEAST_EXPECT(size == msgIter->first);
                    if (expectedSeqs.size() == 1)
                    {
                        auto const msg =
                            extractProtocolMessage1(*messageWithHash.message);
                        auto const expectedVersion = 1;
                        if (BEAST_EXPECT(msg))
                        {
                            BEAST_EXPECT(msg->version() == expectedVersion);
                            if (!BEAST_EXPECT(seqIter != expectedSeqs.end()))
                                continue;
                            auto const& expectedBlob = blobInfos.at(*seqIter);
                            BEAST_EXPECT(
                                (*seqIter < manifestCutoff) ==
                                !!expectedBlob.manifest);
                            auto const expectedManifest =
                                *seqIter < manifestCutoff &&
                                    expectedBlob.manifest
                                ? *expectedBlob.manifest
                                : manifest;
                            BEAST_EXPECT(msg->manifest() == expectedManifest);
                            BEAST_EXPECT(msg->blob() == expectedBlob.blob);
                            BEAST_EXPECT(
                                msg->signature() == expectedBlob.signature);
                            ++seqIter;
                            BEAST_EXPECT(seqIter == expectedSeqs.end());

                            BEAST_EXPECT(
                                messageWithHash.hash ==
                                sha512Half(
                                    expectedManifest,
                                    expectedBlob.blob,
                                    expectedBlob.signature,
                                    expectedVersion));
                        }
                    }
                    else
                    {
                        std::vector<ValidatorBlobInfo> hashingBlobs;
                        hashingBlobs.reserve(msgIter->second.size());

                        auto const msg =
                            extractProtocolMessage2(*messageWithHash.message);
                        if (BEAST_EXPECT(msg))
                        {
                            BEAST_EXPECT(msg->version() == version);
                            BEAST_EXPECT(msg->manifest() == manifest);
                            for (auto const& blobInfo : msg->blobs())
                            {
                                if (!BEAST_EXPECT(
                                        seqIter != expectedSeqs.end()))
                                    break;
                                auto const& expectedBlob =
                                    blobInfos.at(*seqIter);
                                hashingBlobs.push_back(expectedBlob);
                                BEAST_EXPECT(
                                    blobInfo.has_manifest() ==
                                    !!expectedBlob.manifest);
                                BEAST_EXPECT(
                                    blobInfo.has_manifest() ==
                                    (*seqIter < manifestCutoff));

                                if (*seqIter < manifestCutoff)
                                    BEAST_EXPECT(
                                        blobInfo.manifest() ==
                                        *expectedBlob.manifest);
                                BEAST_EXPECT(
                                    blobInfo.blob() == expectedBlob.blob);
                                BEAST_EXPECT(
                                    blobInfo.signature() ==
                                    expectedBlob.signature);
                                ++seqIter;
                            }
                            BEAST_EXPECT(seqIter == expectedSeqs.end());
                        }
                        BEAST_EXPECT(
                            messageWithHash.hash ==
                            sha512Half(manifest, hashingBlobs, version));
                    }
                    ++msgIter;
                }
                BEAST_EXPECT(msgIter == expectedInfo.end());
            };
        auto verifyBuildMessages =
            [this](
                std::pair<std::size_t, std::size_t> const& result,
                std::size_t expectedSequence,
                std::size_t expectedSize) {
                BEAST_EXPECT(result.first == expectedSequence);
                BEAST_EXPECT(result.second == expectedSize);
            };

        std::string const manifest = "This is not a manifest";
        std::uint32_t const version = 2;
        // Mutable so items can be removed in later tests.
        auto const blobInfos = [manifestCutoff = manifestCutoff]() {
            std::map<std::size_t, ValidatorBlobInfo> bis;

            for (auto seq : {5, 6, 7, 10, 12})
            {
                auto& b = bis[seq];
                std::stringstream s;
                s << "This is not a blob with sequence " << seq;
                b.blob = s.str();
                s.str(std::string());
                s << "This is not a signature for sequence " << seq;
                b.signature = s.str();
                if (seq < manifestCutoff)
                {
                    // add a manifest for the "early" blobs
                    s.str(std::string());
                    s << "This is not manifest " << seq;
                    b.manifest = s.str();
                }
            }
            return bis;
        }();
        auto const maxSequence = blobInfos.rbegin()->first;
        BEAST_EXPECT(maxSequence == 12);

        std::vector<ValidatorList::MessageWithHash> messages;

        // Version 1

        // This peer has a VL ahead of our "current"
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                1, 8, maxSequence, version, manifest, blobInfos, messages),
            0,
            0);
        BEAST_EXPECT(messages.size() == 0);

        // Don't repeat the work if messages is populated, even though the
        // peerSequence provided indicates it should. Note that this
        // situation is contrived for this test and should never happen in
        // real code.
        messages.emplace_back();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                1, 3, maxSequence, version, manifest, blobInfos, messages),
            5,
            0);
        BEAST_EXPECT(messages.size() == 1 && !messages.front().message);

        // Generate a version 1 message
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                1, 3, maxSequence, version, manifest, blobInfos, messages),
            5,
            1);
        if (BEAST_EXPECT(messages.size() == 1) &&
            BEAST_EXPECT(messages.front().message))
        {
            auto const& messageWithHash = messages.front();
            auto const msg = extractProtocolMessage1(*messageWithHash.message);
            auto const size =
                messageWithHash.message->getBuffer(compression::Compressed::Off)
                    .size();
            // This size is arbitrary, but shouldn't change
            BEAST_EXPECT(size == 108);
            auto const& expected = blobInfos.at(5);
            if (BEAST_EXPECT(msg))
            {
                BEAST_EXPECT(msg->version() == 1);
                BEAST_EXPECT(msg->manifest() == *expected.manifest);
                BEAST_EXPECT(msg->blob() == expected.blob);
                BEAST_EXPECT(msg->signature() == expected.signature);
            }
            BEAST_EXPECT(
                messageWithHash.hash ==
                sha512Half(
                    *expected.manifest, expected.blob, expected.signature, 1));
        }

        // Version 2

        messages.clear();

        // This peer has a VL ahead of us.
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2,
                maxSequence * 2,
                maxSequence,
                version,
                manifest,
                blobInfos,
                messages),
            0,
            0);
        BEAST_EXPECT(messages.size() == 0);

        // Don't repeat the work if messages is populated, even though the
        // peerSequence provided indicates it should. Note that this
        // situation is contrived for this test and should never happen in
        // real code.
        messages.emplace_back();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 3, maxSequence, version, manifest, blobInfos, messages),
            maxSequence,
            0);
        BEAST_EXPECT(messages.size() == 1 && !messages.front().message);

        // Generate a version 2 message. Don't send the current
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 5, maxSequence, version, manifest, blobInfos, messages),
            maxSequence,
            4);
        verifyMessage(
            version, manifest, blobInfos, messages, {{372, {6, 7, 10, 12}}});

        // Test message splitting on size limits.

        // Set a limit that should give two messages
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 5, maxSequence, version, manifest, blobInfos, messages, 300),
            maxSequence,
            4);
        verifyMessage(
            version,
            manifest,
            blobInfos,
            messages,
            {{212, {6, 7}}, {192, {10, 12}}});

        // Set a limit between the size of the two earlier messages so one
        // will split and the other won't
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 5, maxSequence, version, manifest, blobInfos, messages, 200),
            maxSequence,
            4);
        verifyMessage(
            version,
            manifest,
            blobInfos,
            messages,
            {{108, {6}}, {108, {7}}, {192, {10, 12}}});

        // Set a limit so that all the VLs are sent individually
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 5, maxSequence, version, manifest, blobInfos, messages, 150),
            maxSequence,
            4);
        verifyMessage(
            version,
            manifest,
            blobInfos,
            messages,
            {{108, {6}}, {108, {7}}, {110, {10}}, {110, {12}}});

        // Set a limit smaller than some of the messages. Because single
        // messages send regardless, they will all still be sent
        messages.clear();
        verifyBuildMessages(
            ValidatorList::buildValidatorListMessages(
                2, 5, maxSequence, version, manifest, blobInfos, messages, 108),
            maxSequence,
            4);
        verifyMessage(
            version,
            manifest,
            blobInfos,
            messages,
            {{108, {6}}, {108, {7}}, {110, {10}}, {110, {12}}});
    }

    void
    testQuorumDisabled()
    {
        testcase("Test quorum disabled");

        std::string const siteUri = "testQuorumDisabled.test";
        jtx::Env env(*this);
        auto& app = env.app();

        constexpr std::size_t maxKeys = 20;
        hash_set<NodeID> activeValidators;
        std::vector<Validator> valKeys;
        while (valKeys.size() != maxKeys)
        {
            valKeys.push_back(randomValidator());
            activeValidators.emplace(calcNodeID(valKeys.back().masterPublic));
        }

        struct Publisher
        {
            bool revoked;
            PublicKey pubKey;
            std::pair<PublicKey, SecretKey> signingKeys;
            std::string manifest;
            NetClock::time_point expiry = {};
        };

        // Create ValidatorList with a set of countTotal publishers, of which
        // first countRevoked are revoked and the last one expires early
        auto makeValidatorList = [&, this](
                                     std::size_t countTotal,
                                     std::size_t countRevoked,
                                     std::size_t listThreshold,
                                     ManifestCache& pubManifests,
                                     ManifestCache& valManifests,
                                     std::optional<Validator> self,
                                     std::vector<Publisher>& publishers  // out
                                     ) -> std::unique_ptr<ValidatorList> {
            auto result = std::make_unique<ValidatorList>(
                valManifests,
                pubManifests,
                env.timeKeeper(),
                app.config().legacy("database_path"),
                env.journal);

            std::vector<std::string> cfgPublishers;
            for (std::size_t i = 0; i < countTotal; ++i)
            {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                cfgPublishers.push_back(strHex(publisherPublic));

                constexpr auto revoked =
                    std::numeric_limits<std::uint32_t>::max();
                auto const manifest = base64_encode(makeManifestString(
                    publisherPublic,
                    publisherSecret,
                    pubSigningKeys.first,
                    pubSigningKeys.second,
                    i < countRevoked ? revoked : 1));
                publishers.push_back(Publisher{
                    i < countRevoked,
                    publisherPublic,
                    pubSigningKeys,
                    manifest});
            }

            std::vector<std::string> const emptyCfgKeys;
            auto threshold =
                listThreshold > 0 ? std::optional(listThreshold) : std::nullopt;
            if (self)
            {
                valManifests.applyManifest(
                    *deserializeManifest(base64_decode(self->manifest)));
                BEAST_EXPECT(result->load(
                    self->signingPublic,
                    emptyCfgKeys,
                    cfgPublishers,
                    threshold));
            }
            else
            {
                BEAST_EXPECT(
                    result->load({}, emptyCfgKeys, cfgPublishers, threshold));
            }

            for (std::size_t i = 0; i < countTotal; ++i)
            {
                using namespace std::chrono_literals;
                publishers[i].expiry = env.timeKeeper().now() +
                    (i == countTotal - 1 ? 60s : 3600s);
                auto const blob = makeList(
                    valKeys,
                    1,
                    publishers[i].expiry.time_since_epoch().count());
                auto const sig = signList(blob, publishers[i].signingKeys);

                BEAST_EXPECT(
                    result
                        ->applyLists(
                            publishers[i].manifest,
                            1,
                            {{blob, sig, {}}},
                            siteUri)
                        .bestDisposition() ==
                    (publishers[i].revoked ? ListDisposition::untrusted
                                           : ListDisposition::accepted));
            }

            return result;
        };

        // Test cases use 5 publishers.
        constexpr auto quorumDisabled = std::numeric_limits<std::size_t>::max();
        {
            // List threshold = 5 (same as number of trusted publishers)
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                5,  //
                0,
                5,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 5);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 5 (same as number of trusted publishers)
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                0,
                5,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 5);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no trusted validators
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 0);

            hash_set<NodeID> removed;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 4, 1 publisher is revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[1];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                1,
                4,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 4);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                if (val.masterPublic != self.masterPublic)
                {
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    removed.insert(calcNodeID(val.masterPublic));
                }
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 3 (default), 2 publishers are revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                5,  //
                2,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 3);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 2);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 3 (default), 2 publishers are revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[5];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                2,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 3);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 2);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                if (val.masterPublic != self.masterPublic)
                {
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    removed.insert(calcNodeID(val.masterPublic));
                }
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 3 (default), 2 publishers are revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                2,
                0,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 3);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 2);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, no trusted validators
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 0);

            hash_set<NodeID> removed;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 2, 1 publisher is revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                5,  //
                1,
                2,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
        }
        {
            // List threshold = 1
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                5,  //
                0,
                1,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
        }
        {
            // List threshold = 1
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[7];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                0,
                1,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
        }
        {
            // List threshold = 1
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                5,  //
                0,
                1,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
        }

        // Test cases use 2 publishers
        {
            // List threshold = 1, 1 publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                2,  //
                1,
                1,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 1, 1 publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[5];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                2,  //
                1,
                1,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                if (val.masterPublic != self.masterPublic)
                {
                    BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    removed.insert(calcNodeID(val.masterPublic));
                }
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 1, 1 publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                2,  //
                1,
                1,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 1);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, no trusted validators
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 0);

            hash_set<NodeID> removed;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 2 (same as number of trusted publishers)
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                2,  //
                0,
                2,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 2 (same as number of trusted publishers)
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[5];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                2,  //
                0,
                2,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                if (val.masterPublic != self.masterPublic)
                {
                    BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    removed.insert(calcNodeID(val.masterPublic));
                }
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
        {
            // List threshold = 2 (same as number of trusted publishers)
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                2,  //
                0,
                2,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no trusted validators
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 0);

            hash_set<NodeID> removed;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }

        // Test case for 1 publisher
        {
            // List threshold = 1 (default), no publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                1,  //
                0,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 1);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(!trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }

        // Test case for 3 publishers (for 2 is a block above)
        {
            // List threshold = 2 (default), no publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[2];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                3,  //
                0,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 2);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());
        }

        // Test case for 4 publishers
        {
            // List threshold = 3 (default), no publisher revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                4,  //
                0,
                0,
                pubManifests,
                valManifests,
                {},
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 3);
            for (auto const& p : publishers)
                BEAST_EXPECT(trustedKeys->trustedPublisher(p.pubKey));

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());
        }

        // Test case for 6 publishers (for 5 is a large block above)
        {
            // List threshold = 4 (default), 2 publishers revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is a random validator
            auto const self = randomValidator();
            auto const keysTotal = valKeys.size() + 1;
            auto trustedKeys = makeValidatorList(
                6,  //
                2,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 4);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 2);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            added.insert(calcNodeID(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - no quorum, only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                removed.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }

        // Test case for 7 publishers
        {
            // List threshold = 4 (default), 3 publishers revoked
            ManifestCache pubManifests;
            ManifestCache valManifests;
            std::vector<Publisher> publishers;
            // Self is in UNL
            auto const self = valKeys[2];
            auto const keysTotal = valKeys.size();
            auto trustedKeys = makeValidatorList(
                7,  //
                3,
                0,
                pubManifests,
                valManifests,
                self,
                publishers);
            BEAST_EXPECT(trustedKeys->getListThreshold() == 4);
            int untrustedCount = 0;
            for (auto const& p : publishers)
            {
                bool const trusted = trustedKeys->trustedPublisher(p.pubKey);
                BEAST_EXPECT(p.revoked ^ trusted);
                untrustedCount += trusted ? 0 : 1;
            }
            BEAST_EXPECT(untrustedCount == 3);

            TrustChanges changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == std::ceil(keysTotal * 0.8f));
            BEAST_EXPECT(
                trustedKeys->getTrustedMasterKeys().size() == keysTotal);

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted(val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // Expire one publisher - only trusted validator is self
            env.timeKeeper().set(publishers.back().expiry);
            changes = trustedKeys->updateTrusted(
                activeValidators,
                env.timeKeeper().now(),
                env.app().getOPs(),
                env.app().overlay(),
                env.app().getHashRouter());
            BEAST_EXPECT(trustedKeys->quorum() == quorumDisabled);
            BEAST_EXPECT(trustedKeys->getTrustedMasterKeys().size() == 1);

            hash_set<NodeID> removed;
            BEAST_EXPECT(trustedKeys->trusted(self.masterPublic));
            for (auto const& val : valKeys)
            {
                if (val.masterPublic != self.masterPublic)
                {
                    BEAST_EXPECT(trustedKeys->listed(val.masterPublic));
                    BEAST_EXPECT(!trustedKeys->trusted(val.masterPublic));
                    removed.insert(calcNodeID(val.masterPublic));
                }
            }
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed == removed);
        }
    }

public:
    void
    run() override
    {
        testGenesisQuorum();
        testConfigLoad();
        testApplyLists();
        testGetAvailable();
        testUpdateTrusted();
        testExpires();
        testNegativeUNL();
        testSha512Hash();
        testBuildMessages();
        testQuorumDisabled();
    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

}  // namespace test
}  // namespace ripple
