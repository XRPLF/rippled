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
    struct Validator
    {
        PublicKey masterPublic;
        PublicKey signingPublic;
        std::string manifest;
    };

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

    static
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

    static
    Validator
    randomValidator ()
    {
        auto const secret = randomSecretKey();
        auto const masterPublic =
            derivePublicKey(KeyType::ed25519, secret);
        auto const signingKeys = randomKeyPair(KeyType::secp256k1);
        return { masterPublic, signingKeys.first,
            beast::detail::base64_encode(makeManifestString (
            masterPublic, secret, signingKeys.first, signingKeys.second, 1)) };
    }

    std::string
    makeList (
        std::vector <Validator> const& validators,
        std::size_t sequence,
        std::size_t expiration)
    {
        std::string data =
            "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(expiration) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val.masterPublic) +
                "\",\"manifest\":\"" + val.manifest + "\"},";
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
        std::vector<Validator> list1;
        list1.reserve (listSize);
        while (list1.size () < listSize)
            list1.push_back (randomValidator());

        std::vector<Validator> list2;
        list2.reserve (listSize);
        while (list2.size () < listSize)
            list2.push_back (randomValidator());

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
        {
            BEAST_EXPECT(trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(trustedKeys->listed (val.signingPublic));
        }

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
        {
            BEAST_EXPECT(! trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(! trustedKeys->listed (val.signingPublic));
        }

        for (auto const& val : list2)
        {
            BEAST_EXPECT(trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(trustedKeys->listed (val.signingPublic));
        }

        // do not re-apply lists with past or current sequence numbers
        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, blob1, sig1, version));

        BEAST_EXPECT(ListDisposition::same_sequence ==
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
        {
            BEAST_EXPECT(! trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(! trustedKeys->listed (val.signingPublic));
        }
    }

    void
    testUpdateTrusted ()
    {
        testcase ("Update trusted");

        PublicKey emptyLocalKey;
        ManifestCache manifests;
        jtx::Env env (*this);
        auto trustedKeys = std::make_unique <ValidatorList> (
            manifests, manifests, env.timeKeeper(), beast::Journal ());

        std::vector<std::string> cfgPublishers;
        hash_set<NodeID> activeValidators;
        hash_set<NodeID> secondAddedValidators;

        // BFT: n >= 3f+1
        std::size_t const n = 40;
        std::size_t const f = 13;
        {
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(n);

            while (cfgKeys.size () != n)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                if (cfgKeys.size () <= n - 5)
                    activeValidators.emplace (calcNodeID(valKey));
            }

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            // updateTrusted should make all available configured
            // validators trusted
            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.added == activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            // Add 1 to n because I'm not on a published list.
            BEAST_EXPECT(trustedKeys->quorum () == n + 1 - f);
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

            {
                // Quorum should be 80% with all listed validators active
                hash_set<NodeID> activeValidatorsNew{activeValidators};
                for (auto const valKey : cfgKeys)
                {
                    auto const ins = activeValidatorsNew.emplace(
                        calcNodeID(*parseBase58<PublicKey>(
                            TokenType::TOKEN_NODE_PUBLIC, valKey)));
                    if(ins.second)
                        secondAddedValidators.insert(*ins.first);
                }
                TrustChanges changes =
                    trustedKeys->updateTrusted(activeValidatorsNew);
                BEAST_EXPECT(changes.added == secondAddedValidators);
                BEAST_EXPECT(changes.removed.empty());
                BEAST_EXPECT(trustedKeys->quorum () == cfgKeys.size() * 4/5);
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
            activeValidators.emplace (calcNodeID(masterPublic));

            // Should not trust ephemeral signing key if there is no manifest
            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.added == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.removed == secondAddedValidators);
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
            changes = trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == n + 2 - f);
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
            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == n + 2 - f);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(trustedKeys->listed (signingPublic2));
            BEAST_EXPECT(trustedKeys->trusted (signingPublic2));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));

            // Should not trust keys from revoked master public key
            auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
            auto const signingPublicMax = signingKeysMax.first;
            activeValidators.emplace (calcNodeID(signingPublicMax));
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
            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == n + 1 - f);
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

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::numeric_limits<std::size_t>::max());
        }
        {
            // Trust all listed validators if none are active
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<PublicKey> keys ({ randomNode (), randomNode () });
            hash_set<NodeID> activeValidators;
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[0]),
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[1])});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == asNodeIDs({keys[0], keys[1]}));

            BEAST_EXPECT(trustedKeys->quorum () == 2);
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trusted (key));
        }
        {
            // Should use custom minimum quorum
            std::size_t const minQuorum = 1;
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal (), minQuorum);

            auto const node = randomNode ();
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, node)});
            hash_set<NodeID> activeValidators;

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == asNodeIDs({node}));
            BEAST_EXPECT(trustedKeys->quorum () == minQuorum);

            activeValidators.emplace (calcNodeID(node));
            changes = trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == 1);
        }
        {
            // Increase quorum when running as an unlisted validator
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<PublicKey> keys ({ randomNode (), randomNode () });
            hash_set<NodeID> activeValidators (asNodeIDs({ keys[0] }));
            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[0]),
                toBase58 (TokenType::TOKEN_NODE_PUBLIC, keys[1])});

            auto const localKey = randomNode ();
            BEAST_EXPECT(trustedKeys->load (
                localKey, cfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                changes.added == asNodeIDs({keys[0], keys[1], localKey}));
            BEAST_EXPECT(trustedKeys->quorum () == 2);

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

            std::vector<Validator> list ({randomValidator(), randomValidator()});
            hash_set<NodeID> activeValidators(
                asNodeIDs({list[0].masterPublic, list[1].masterPublic}));

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

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == activeValidators);
            for(Validator const & val : list)
            {
                BEAST_EXPECT(trustedKeys->trusted (val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted (val.signingPublic));
            }
            BEAST_EXPECT(trustedKeys->quorum () == 2);

            env.timeKeeper().set(expiration);
            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed == activeValidators);
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(! trustedKeys->trusted (list[0].masterPublic));
            BEAST_EXPECT(! trustedKeys->trusted (list[1].masterPublic));
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::numeric_limits<std::size_t>::max());

            // (Re)trust validators from new valid list
            std::vector<Validator> list2 ({list[0], randomValidator()});
            activeValidators.insert(calcNodeID(list2[1].masterPublic));
            auto const sequence2 = 2;
            NetClock::time_point const expiration2 =
                env.timeKeeper().now() + 60s;
            auto const blob2 = makeList (
                list2, sequence2, expiration2.time_since_epoch().count());
            auto const sig2 = signList (blob2, pubSigningKeys);

            BEAST_EXPECT(ListDisposition::accepted ==
                trustedKeys->applyList (
                    manifest, blob2, sig2, version));

            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                changes.added ==
                asNodeIDs({list2[0].masterPublic, list2[1].masterPublic}));
            for(Validator const & val : list2)
            {
                BEAST_EXPECT(trustedKeys->trusted (val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted (val.signingPublic));
            }
            BEAST_EXPECT(! trustedKeys->trusted (list[1].masterPublic));
            BEAST_EXPECT(! trustedKeys->trusted (list[1].signingPublic));
            BEAST_EXPECT(trustedKeys->quorum () == 2);
        }
        {
            // Test 1-9 configured validators
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;

            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                activeValidators.emplace (calcNodeID(valKey));
                activeKeys.emplace(valKey);

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, cfgKeys, cfgPublishers));
                TrustChanges changes =
                    trustedKeys->updateTrusted(activeValidators);
                BEAST_EXPECT(changes.removed.empty());
                BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                BEAST_EXPECT(trustedKeys->quorum () ==
                    ((cfgKeys.size() <= 6) ? cfgKeys.size()/2 + 1 :
                        cfgKeys.size() * 2/3 + 1));
                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
        {
            // Test 2-9 configured validators as validator
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            auto const localKey = randomNode();
            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;
            std::vector<std::string> cfgKeys {
                toBase58(TokenType::TOKEN_NODE_PUBLIC, localKey)};
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::TOKEN_NODE_PUBLIC, valKey));
                activeValidators.emplace (calcNodeID(valKey));
                activeKeys.emplace(valKey);

                BEAST_EXPECT(trustedKeys->load (
                    localKey, cfgKeys, cfgPublishers));
                TrustChanges changes =
                    trustedKeys->updateTrusted(activeValidators);
                BEAST_EXPECT(changes.removed.empty());
                if (cfgKeys.size() > 2)
                    BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                else
                    BEAST_EXPECT(
                        changes.added == asNodeIDs({localKey, valKey}));

                BEAST_EXPECT(trustedKeys->quorum () ==
                    ((cfgKeys.size() <= 6) ? cfgKeys.size()/2 + 1 :
                        (cfgKeys.size() + 1) * 2/3 + 1));

                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
        {
            // Trusted set should be trimmed with multiple validator lists
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), beast::Journal ());

            hash_set<NodeID> activeValidators;
            std::vector<Validator> valKeys;
            valKeys.reserve(n);

            while (valKeys.size () != n)
            {
                valKeys.push_back (randomValidator());
                activeValidators.emplace(
                    calcNodeID(valKeys.back().masterPublic));
            }

            auto addPublishedList = [this, &env, &trustedKeys, &valKeys]()
            {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = beast::detail::base64_encode(makeManifestString (
                    publisherPublic, publisherSecret,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

                std::vector<std::string> cfgPublishers({
                    strHex(publisherPublic)});
                PublicKey emptyLocalKey;
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, emptyCfgKeys, cfgPublishers));

                auto const version = 1;
                auto const sequence = 1;
                NetClock::time_point const expiration =
                    env.timeKeeper().now() + 3600s;
                auto const blob = makeList (
                    valKeys, sequence, expiration.time_since_epoch().count());
                auto const sig = signList (blob, pubSigningKeys);

                BEAST_EXPECT(ListDisposition::accepted == trustedKeys->applyList (
                    manifest, blob, sig, version));
            };

            // Apply multiple published lists
            for (auto i = 0; i < 3; ++i)
                addPublishedList();

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);

            // Minimum quorum should be used
            BEAST_EXPECT(trustedKeys->quorum () == (valKeys.size() * 2/3 + 1));

            hash_set<NodeID> added;
            std::size_t nTrusted = 0;
            for (auto const& val : valKeys)
            {
                if (trustedKeys->trusted (val.masterPublic))
                {
                    added.insert(calcNodeID(val.masterPublic));
                    ++nTrusted;
                }
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());

            // The number of trusted keys should be 125% of the minimum quorum
            BEAST_EXPECT(nTrusted ==
                static_cast<std::size_t>(trustedKeys->quorum () * 5 / 4));
        }
    }

    void
    testExpires()
    {
        testcase("Expires");

        beast::Journal journal;
        jtx::Env env(*this);

        auto toStr = [](PublicKey const& publicKey) {
            return toBase58(TokenType::TOKEN_NODE_PUBLIC, publicKey);
        };

        // Config listed keys
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests, manifests, env.timeKeeper(), journal);

            // Empty list has no expiration
            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            // Config listed keys have maximum expiry
            PublicKey emptyLocalKey;
            PublicKey localCfgListed = randomNode();
            trustedKeys->load(emptyLocalKey, {toStr(localCfgListed)}, {});
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == NetClock::time_point::max());
            BEAST_EXPECT(trustedKeys->listed(localCfgListed));
        }

        // Published keys with expirations
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests, manifests, env.app().timeKeeper(), journal);

            std::vector<Validator> validators = {randomValidator()};
            hash_set<NodeID> activeValidators;
            for(Validator const & val : validators)
                activeValidators.insert(calcNodeID(val.masterPublic));
            // Store prepared list data to control when it is applied
            struct PreparedList
            {
                std::string manifest;
                std::string blob;
                std::string sig;
                int version;
                NetClock::time_point expiration;
            };

            auto addPublishedList = [this, &env, &trustedKeys, &validators]()
            {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = beast::detail::base64_encode(makeManifestString (
                    publisherPublic, publisherSecret,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

                std::vector<std::string> cfgPublishers({
                    strHex(publisherPublic)});
                PublicKey emptyLocalKey;
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, emptyCfgKeys, cfgPublishers));

                auto const version = 1;
                auto const sequence = 1;
                NetClock::time_point const expiration =
                    env.timeKeeper().now() + 3600s;
                auto const blob = makeList(
                    validators,
                    sequence,
                    expiration.time_since_epoch().count());
                auto const sig = signList (blob, pubSigningKeys);

                return PreparedList{manifest, blob, sig, version, expiration};
            };


            // Configure two publishers and prepare 2 lists
            PreparedList prep1 = addPublishedList();
            env.timeKeeper().set(env.timeKeeper().now() + 200s);
            PreparedList prep2 = addPublishedList();

            // Initially, no list has been published, so no known expiration
            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            // Apply first list
            BEAST_EXPECT(
                ListDisposition::accepted == trustedKeys->applyList(
                    prep1.manifest, prep1.blob, prep1.sig, prep1.version));

            // One list still hasn't published, so expiration is still unknown
            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            // Apply second list
            BEAST_EXPECT(
                ListDisposition::accepted == trustedKeys->applyList(
                    prep2.manifest, prep2.blob, prep2.sig, prep2.version));

            // We now have loaded both lists, so expiration is known
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == prep1.expiration);

            // Advance past the first list's expiration, but it remains the
            // earliest expiration
            env.timeKeeper().set(prep1.expiration + 1s);
            trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == prep1.expiration);
        }
}
public:
    void
    run() override
    {
        testGenesisQuorum ();
        testConfigLoad ();
        testApplyList ();
        testUpdateTrusted ();
        testExpires ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

} // test
} // ripple
