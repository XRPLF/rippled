//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2015 Ripple Labs Inc.

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

#include <beast/core/detail/base64.hpp>
#include <ripple/basics/Slice.h>
#include <ripple/basics/strHex.h>
#include <ripple/app/misc/ValidatorList.h>
#include <test/jtx.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>


namespace ripple {
namespace test {

class ValidatorList_test : public beast::unit_test::suite
{
private:
    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (KeyType::secp256k1, randomSecretKey());
    }

    static
    PublicKey
    randomMasterKey ()
    {
        return derivePublicKey (KeyType::ed25519, randomSecretKey());
    }

    std::string
    makeManifestString (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return std::string(static_cast<char const*> (s.data()), s.size());
    }

    std::string
    makeList (
        std::vector <PublicKey> const& validators,
        std::size_t sequence,
        std::size_t expiration)
    {
        std::string data =
            "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(expiration) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val) + "\"},";
        }

        data.pop_back();
        data += "]}";
        return beast::detail::base64_encode(data);
    }

    std::string
    signList (
        std::string const& blob,
        std::pair<PublicKey, SecretKey> const& keys)
    {
        auto const data = beast::detail::base64_decode (blob);
        return strHex(sign(
            keys.first, keys.second, makeSlice(data)));
    }

    void
    testGenesisQuorum ()
    {
        testcase ("Genesis Quorum");

        beast::Journal journal;
        ManifestCache manifests;
        jtx::Env env (*this);
        {
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);
            BEAST_EXPECT(trustedKeys->quorum () == 1);
        }
        {
            std::size_t minQuorum = 0;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal, minQuorum);
            BEAST_EXPECT(trustedKeys->quorum () == minQuorum);
        }
    }

    void
    testCalculateQuorum ()
    {
        testcase ("Calculate Quorum");

        for(std::size_t i = 1; i < 20; ++i)
        {
            auto const quorum = ValidatorList::calculateQuorum(i);
            if (i < 10)
                BEAST_EXPECT(quorum >= (i/2 + 1));
            else
                BEAST_EXPECT(quorum == std::ceil (i * 0.8));
        }
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        beast::Journal journal;
        jtx::Env env (*this);
        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgKeys;
        std::vector<std::string> emptyCfgPublishers;

        auto const localSigningKeys = randomKeyPair(KeyType::secp256k1);
        auto const localSigningPublic = localSigningKeys.first;
        auto const localSigningSecret = localSigningKeys.second;
        auto const localMasterSecret = randomSecretKey();
        auto const localMasterPublic = derivePublicKey(
            KeyType::ed25519, localMasterSecret);

        std::string const cfgManifest (makeManifestString (
            localMasterPublic, localMasterSecret,
            localSigningPublic, localSigningSecret, 1));

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58 (TokenType::TOKEN_NODE_PUBLIC, publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        std::vector<PublicKey> configList;
        configList.reserve(8);

        while (configList.size () != 8)
            configList.push_back (randomNode());

        // Correct configuration
        std::vector<std::string> cfgKeys ({
            format (configList[0]),
            format (configList[1], " Comment"),
            format (configList[2], " Multi Word Comment"),
            format (configList[3], "    Leading Whitespace"),
            format (configList[4], " Trailing Whitespace    "),
            format (configList[5], "    Leading & Trailing Whitespace    "),
            format (configList[6], "    Leading, Trailing & Internal    Whitespace    "),
            format (configList[7], "    ")
        });

        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            // Correct (empty) configuration
            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, emptyCfgPublishers));

            // load local validator key with or without manifest
            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, emptyCfgKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));

            manifests.applyManifest (*Manifest::make_Manifest(cfgManifest));
            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, emptyCfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->listed (localMasterPublic));
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
        }
        {
            // load should add validator keys from config
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, emptyCfgPublishers));

            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));

            // load should accept Ed25519 master public keys
            auto const masterNode1 = randomMasterKey ();
            auto const masterNode2 = randomMasterKey ();

            std::vector<std::string> cfgMasterKeys({
                format (masterNode1),
                format (masterNode2, " Comment")
            });
            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgMasterKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed (masterNode1));
            BEAST_EXPECT(trustedKeys->listed (masterNode2));

            // load should reject invalid config keys
            std::vector<std::string> badKeys({"NotAPublicKey"});
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, badKeys, emptyCfgPublishers));

            badKeys[0] = format (randomNode(), "!");
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, badKeys, emptyCfgPublishers));

            badKeys[0] = format (randomNode(), "!  Comment");
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, badKeys, emptyCfgPublishers));

            // load terminates when encountering an invalid entry
            auto const goodKey = randomNode();
            badKeys.push_back (format (goodKey));
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, badKeys, emptyCfgPublishers));
            BEAST_EXPECT(!trustedKeys->listed (goodKey));
        }
        {
            // local validator key on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            auto const localSigningPublic = parseBase58<PublicKey> (
                TokenType::TOKEN_NODE_PUBLIC, cfgKeys.front());

            BEAST_EXPECT(trustedKeys->load (
                *localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed (*localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            // local validator key not on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            auto const localSigningPublic = randomNode();
            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            // local validator key (with manifest) not on config list
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            manifests.applyManifest (*Manifest::make_Manifest(cfgManifest));

            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localMasterPublic);
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
            BEAST_EXPECT(trustedKeys->listed (localMasterPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), journal);

            // load should reject invalid validator list signing keys
            std::vector<std::string> badPublishers(
                {"NotASigningKey"});
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, badPublishers));

            // load should reject validator list signing keys with invalid encoding
            std::vector<PublicKey> keys ({
                randomMasterKey(), randomMasterKey(), randomMasterKey()});
            badPublishers.clear();
            for (auto const& key : keys)
                badPublishers.push_back (
                    toBase58 (TokenType::TOKEN_NODE_PUBLIC, key));

            BEAST_EXPECT(! trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, badPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(!trustedKeys->trustedPublisher (key));

            // load should accept valid validator list publisher keys
            std::vector<std::string> cfgPublishers;
            for (auto const& key : keys)
                cfgPublishers.push_back (strHex(key));

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trustedPublisher (key));
        }
    }

    void
    testApplyList ()
    {
        testcase ("Apply list");

        beast::Journal journal;
        ManifestCache manifests;
        jtx::Env env (*this);
        auto trustedKeys = std::make_unique<ValidatorList> (
            manifests, manifests, env.app().timeKeeper(), journal);

        auto const publisherSecret = randomSecretKey();
        auto const publisherPublic =
            derivePublicKey(KeyType::ed25519, publisherSecret);
        auto const pubSigningKeys1 = randomKeyPair(KeyType::secp256k1);
        auto const manifest1 = beast::detail::base64_encode(makeManifestString (
            publisherPublic, publisherSecret,
            pubSigningKeys1.first, pubSigningKeys1.second, 1));

        std::vector<std::string> cfgKeys1({
            strHex(publisherPublic)});
        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgKeys;

        BEAST_EXPECT(trustedKeys->load (
            emptyLocalKey, emptyCfgKeys, cfgKeys1));

        auto constexpr listSize = 20;
        std::vector<PublicKey> list1;
        list1.reserve (listSize);
        while (list1.size () < listSize)
            list1.push_back (randomNode());

        std::vector<PublicKey> list2;
        list2.reserve (listSize);
        while (list2.size () < listSize)
            list2.push_back (randomNode());

        // do not apply expired list
        auto const version = 1;
        auto const sequence = 1;
        auto const expiredblob = makeList (
            list1, sequence, env.timeKeeper().now().time_since_epoch().count());
        auto const expiredSig = signList (expiredblob, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, expiredblob, expiredSig, version));

        // apply single list
        NetClock::time_point const expiration =
            env.timeKeeper().now() + 3600s;
        auto const blob1 = makeList (
            list1, sequence, expiration.time_since_epoch().count());
        auto const sig1 = signList (blob1, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::accepted == trustedKeys->applyList (
            manifest1, blob1, sig1, version));

        for (auto const& val : list1)
            BEAST_EXPECT(trustedKeys->listed (val));

        // do not use list from untrusted publisher
        auto const untrustedManifest = beast::detail::base64_encode(
            makeManifestString (
                randomMasterKey(), publisherSecret,
                pubSigningKeys1.first, pubSigningKeys1.second, 1));

        BEAST_EXPECT(ListDisposition::untrusted == trustedKeys->applyList (
            untrustedManifest, blob1, sig1, version));

        // do not use list with unhandled version
        auto const badVersion = 666;
        BEAST_EXPECT(ListDisposition::unsupported_version ==
            trustedKeys->applyList (
                manifest1, blob1, sig1, badVersion));

        // apply list with highest sequence number
        auto const sequence2 = 2;
        auto const blob2 = makeList (
            list2, sequence2, expiration.time_since_epoch().count());
        auto const sig2 = signList (blob2, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::accepted ==
            trustedKeys->applyList (
                manifest1, blob2, sig2, version));

        for (auto const& val : list1)
            BEAST_EXPECT(! trustedKeys->listed (val));

        for (auto const& val : list2)
            BEAST_EXPECT(trustedKeys->listed (val));

        // do not re-apply lists with past or current sequence numbers
        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, blob1, sig1, version));

        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, blob2, sig2, version));

        // apply list with new publisher key updated by manifest
        auto const pubSigningKeys2 = randomKeyPair(KeyType::secp256k1);
        auto manifest2 = beast::detail::base64_encode(makeManifestString (
            publisherPublic, publisherSecret,
            pubSigningKeys2.first, pubSigningKeys2.second, 2));

        auto const sequence3 = 3;
        auto const blob3 = makeList (
            list1, sequence3, expiration.time_since_epoch().count());
        auto const sig3 = signList (blob3, pubSigningKeys2);

        BEAST_EXPECT(ListDisposition::accepted ==
            trustedKeys->applyList (
                manifest2, blob3, sig3, version));

        auto const sequence4 = 4;
        auto const blob4 = makeList (
            list1, sequence4, expiration.time_since_epoch().count());
        auto const badSig = signList (blob4, pubSigningKeys1);
        BEAST_EXPECT(ListDisposition::invalid ==
            trustedKeys->applyList (
                manifest1, blob4, badSig, version));

        // do not apply list with revoked publisher key
        // applied list is removed due to revoked publisher key
        auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
        auto maxManifest = beast::detail::base64_encode(makeManifestString (
            publisherPublic, publisherSecret,
            pubSigningKeys2.first, pubSigningKeys2.second,
            std::numeric_limits<std::uint32_t>::max ()));

        auto const sequence5 = 5;
        auto const blob5 = makeList (
            list1, sequence5, expiration.time_since_epoch().count());
        auto const sig5 = signList (blob5, signingKeysMax);

        BEAST_EXPECT(ListDisposition::untrusted ==
            trustedKeys->applyList (
                maxManifest, blob5, sig5, version));

        BEAST_EXPECT(! trustedKeys->trustedPublisher(publisherPublic));
        for (auto const& val : list1)
            BEAST_EXPECT(! trustedKeys->listed (val));
    }

    void
    testUpdate ()
    {
        testcase ("Update");

        PublicKey emptyLocalKey;
        ManifestCache manifests;
        jtx::Env env (*this);
        auto trustedKeys = std::make_unique <ValidatorList> (
            manifests, manifests, env.timeKeeper(), beast::Journal ());

        std::vector<std::string> cfgPublishers;
        hash_set<PublicKey> activeValidators;

        {
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(20);

            while (cfgKeys.size () != 20)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                if (cfgKeys.size () <= 15)
                    activeValidators.emplace (valKey);
            }

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            // onConsensusStart should make all available configured
            // validators trusted
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 12);
            std::size_t i = 0;
            for (auto const& val : cfgKeys)
            {
                if (auto const valKey = parseBase58<PublicKey>(
                    TokenType::TOKEN_NODE_PUBLIC, val))
                {
                    BEAST_EXPECT(trustedKeys->listed (*valKey));
                    if (i++ < activeValidators.size ())
                        BEAST_EXPECT(trustedKeys->trusted (*valKey));
                    else
                        BEAST_EXPECT(!trustedKeys->trusted (*valKey));
                }
                else
                    fail ();
            }
        }
        {
            // update with manifests
            auto const masterPrivate  = randomSecretKey();
            auto const masterPublic =
                derivePublicKey(KeyType::ed25519, masterPrivate);

            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, masterPublic)});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            auto const signingKeys1 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic1 = signingKeys1.first;
            activeValidators.emplace (masterPublic);

            // Should not trust ephemeral signing key if there is no manifest
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));

            // Should trust the ephemeral signing key from the applied manifest
            auto m1 = Manifest::make_Manifest (makeManifestString (
                masterPublic, masterPrivate,
                signingPublic1, signingKeys1.second, 1));

            BEAST_EXPECT(
                manifests.applyManifest(std::move (*m1)) ==
                    ManifestDisposition::accepted);
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 13);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(trustedKeys->trusted (signingPublic1));

            // Should only trust the ephemeral signing key
            // from the newest applied manifest
            auto const signingKeys2 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic2 = signingKeys2.first;
            auto m2 = Manifest::make_Manifest (makeManifestString (
                masterPublic, masterPrivate,
                signingPublic2, signingKeys2.second, 2));

            BEAST_EXPECT(
                manifests.applyManifest(std::move (*m2)) ==
                    ManifestDisposition::accepted);
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 13);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(trustedKeys->listed (signingPublic2));
            BEAST_EXPECT(trustedKeys->trusted (signingPublic2));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));

            // Should not trust keys from revoked master public key
            auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
            auto const signingPublicMax = signingKeysMax.first;
            activeValidators.emplace (signingPublicMax);
            auto mMax = Manifest::make_Manifest (makeManifestString (
                masterPublic, masterPrivate,
                signingPublicMax, signingKeysMax.second,
                std::numeric_limits<std::uint32_t>::max ()));

            BEAST_EXPECT(mMax->revoked ());
            BEAST_EXPECT(
                manifests.applyManifest(std::move (*mMax)) ==
                    ManifestDisposition::accepted);
            BEAST_EXPECT(manifests.getSigningKey (masterPublic) == masterPublic);
            BEAST_EXPECT(manifests.revoked (masterPublic));
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 12);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(!trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(!trustedKeys->listed (signingPublicMax));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublicMax));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic2));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic2));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));
        }
        {
            // Make quorum unattainable if lists from any publishers are unavailable
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());
            auto const publisherSecret = randomSecretKey();
            auto const publisherPublic =
                derivePublicKey(KeyType::ed25519, publisherSecret);

            std::vector<std::string> cfgPublishers({
                strHex(publisherPublic)});
            std::vector<std::string> emptyCfgKeys;

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgPublishers));

            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::numeric_limits<std::size_t>::max());
        }
        {
            // Trust all listed validators if none are active
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<PublicKey> keys ({ randomNode (), randomNode () });
            hash_set<PublicKey> activeValidators;
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[0]),
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[1])});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            trustedKeys->onConsensusStart (activeValidators);

            BEAST_EXPECT(trustedKeys->quorum () == 2);
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trusted (key));
        }
        {
            // Should use custom minimum quorum
            std::size_t const minQuorum = 0;
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal (), minQuorum);

            auto const node = randomNode ();
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, node)});
            hash_set<PublicKey> activeValidators;

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == minQuorum);

            activeValidators.emplace (node);
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 1);
        }
        {
            // Increase quorum when running as an unlisted validator
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<PublicKey> keys ({ randomNode (), randomNode () });
            hash_set<PublicKey> activeValidators ({ keys[0] });
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[0]),
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[1])});

            auto const localKey = randomNode ();
            BEAST_EXPECT(trustedKeys->load (
                localKey, cfgKeys, cfgPublishers));

            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->quorum () == 3);

            // local validator key is always trusted
            BEAST_EXPECT(trustedKeys->trusted (localKey));
        }
        {
            // Remove expired published list
            auto trustedKeys = std::make_unique<ValidatorList> (
                manifests, manifests, env.app().timeKeeper(), beast::Journal ());

            PublicKey emptyLocalKey;
            std::vector<std::string> emptyCfgKeys;
            auto const publisherKeys = randomKeyPair(KeyType::secp256k1);
            auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
            auto const manifest = beast::detail::base64_encode (
                makeManifestString (
                    publisherKeys.first, publisherKeys.second,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

            std::vector<std::string> cfgKeys ({
                strHex(publisherKeys.first)});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgKeys));

            std::vector<PublicKey> list ({randomNode()});
            hash_set<PublicKey> activeValidators ({ list[0] });

            // do not apply expired list
            auto const version = 1;
            auto const sequence = 1;
            NetClock::time_point const expiration =
                env.timeKeeper().now() + 60s;
            auto const blob = makeList (
                list, sequence, expiration.time_since_epoch().count());
            auto const sig = signList (blob, pubSigningKeys);

            BEAST_EXPECT(ListDisposition::accepted ==
                trustedKeys->applyList (
                    manifest, blob, sig, version));

            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(trustedKeys->trusted (list[0]));

            env.timeKeeper().set(expiration);
            trustedKeys->onConsensusStart (activeValidators);
            BEAST_EXPECT(! trustedKeys->trusted (list[0]));
        }
        {
            // Test 1-9 configured validators
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<std::string> cfgPublishers;
            hash_set<PublicKey> activeValidators;

            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                activeValidators.emplace (valKey);

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, cfgKeys, cfgPublishers));
                trustedKeys->onConsensusStart (activeValidators);
                BEAST_EXPECT(trustedKeys->quorum () ==
                    ValidatorList::calculateQuorum(cfgKeys.size()));
                for (auto const& key : activeValidators)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
        {
            // Test 2-9 configured validators as validator
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            auto const localKey = randomNode();
            std::vector<std::string> cfgPublishers;
            hash_set<PublicKey> activeValidators;

            std::vector<std::string> cfgKeys {
                toBase58(TokenType::TOKEN_NODE_PUBLIC, localKey)};
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                activeValidators.emplace (valKey);

                BEAST_EXPECT(trustedKeys->load (
                    localKey, cfgKeys, cfgPublishers));
                trustedKeys->onConsensusStart (activeValidators);

                // When running as an unlisted validator,
                // the quorum is incremented by 1 for 3 or 5 trusted validators.
                auto expectedQuorum = ValidatorList::calculateQuorum(cfgKeys.size());
                if (cfgKeys.size() == 3 || cfgKeys.size() == 5)
                    ++expectedQuorum;
                BEAST_EXPECT(trustedKeys->quorum () == expectedQuorum);
                for (auto const& key : activeValidators)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
    }

public:
    void
    run() override
    {
        testGenesisQuorum ();
        testCalculateQuorum ();
        testConfigLoad ();
        testApplyList ();
        testUpdate ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

} // test
} // ripple
