//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/core/DatabaseCon.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Sign.h>

#include <boost/algorithm/string/trim.hpp>

#include <numeric>
#include <stdexcept>

namespace ripple {

std::string
to_string(Manifest const& m)
{
    auto const mk = toBase58(TokenType::NodePublic, m.masterKey);

    if (m.revoked())
        return "Revocation Manifest " + mk;

    if (!m.signingKey)
        Throw<std::runtime_error>("No SigningKey in manifest " + mk);

    return "Manifest " + mk + " (" + std::to_string(m.sequence) + ": " +
        toBase58(TokenType::NodePublic, *m.signingKey) + ")";
}

std::optional<Manifest>
deserializeManifest(Slice s, beast::Journal journal)
{
    if (s.empty())
        return std::nullopt;

    static SOTemplate const manifestFormat{
        // A manifest must include:
        // - the master public key
        {sfPublicKey, soeREQUIRED},

        // - a signature with that public key
        {sfMasterSignature, soeREQUIRED},

        // - a sequence number
        {sfSequence, soeREQUIRED},

        // It may, optionally, contain:
        // - a version number which defaults to 0
        {sfVersion, soeDEFAULT},

        // - a domain name
        {sfDomain, soeOPTIONAL},

        // - an ephemeral signing key that can be changed as necessary
        {sfSigningPubKey, soeOPTIONAL},

        // - a signature using the ephemeral signing key, if it is present
        {sfSignature, soeOPTIONAL},
    };

    try
    {
        SerialIter sit{s};
        STObject st{sit, sfGeneric};

        st.applyTemplate(manifestFormat);

        // We only understand "version 0" manifests at this time:
        if (st.isFieldPresent(sfVersion) && st.getFieldU16(sfVersion) != 0)
            return std::nullopt;

        auto const pk = st.getFieldVL(sfPublicKey);

        if (!publicKeyType(makeSlice(pk)))
            return std::nullopt;

        PublicKey const masterKey = PublicKey(makeSlice(pk));
        std::uint32_t const seq = st.getFieldU32(sfSequence);

        std::string domain;

        std::optional<PublicKey> signingKey;

        if (st.isFieldPresent(sfDomain))
        {
            auto const d = st.getFieldVL(sfDomain);

            domain.assign(reinterpret_cast<char const*>(d.data()), d.size());

            if (!isProperlyFormedTomlDomain(domain))
                return std::nullopt;
        }

        bool const hasEphemeralKey = st.isFieldPresent(sfSigningPubKey);
        bool const hasEphemeralSig = st.isFieldPresent(sfSignature);

        if (Manifest::revoked(seq))
        {
            // Revocation manifests should not specify a new signing key
            // or a signing key signature.
            if (hasEphemeralKey)
                return std::nullopt;

            if (hasEphemeralSig)
                return std::nullopt;
        }
        else
        {
            // Regular manifests should contain a signing key and an
            // associated signature.
            if (!hasEphemeralKey)
                return std::nullopt;

            if (!hasEphemeralSig)
                return std::nullopt;

            auto const spk = st.getFieldVL(sfSigningPubKey);

            if (!publicKeyType(makeSlice(spk)))
                return std::nullopt;

            signingKey.emplace(makeSlice(spk));

            // The signing and master keys can't be the same
            if (*signingKey == masterKey)
                return std::nullopt;
        }

        std::string const serialized(
            reinterpret_cast<char const*>(s.data()), s.size());

        // If the manifest is revoked, then the signingKey will be unseated
        return Manifest(serialized, masterKey, signingKey, seq, domain);
    }
    catch (std::exception const& ex)
    {
        JLOG(journal.error())
            << "Exception in " << __func__ << ": " << ex.what();
        return std::nullopt;
    }
}

template <class Stream>
Stream&
logMftAct(
    Stream& s,
    std::string const& action,
    PublicKey const& pk,
    std::uint32_t seq)
{
    s << "Manifest: " << action
      << ";Pk: " << toBase58(TokenType::NodePublic, pk) << ";Seq: " << seq
      << ";";
    return s;
}

template <class Stream>
Stream&
logMftAct(
    Stream& s,
    std::string const& action,
    PublicKey const& pk,
    std::uint32_t seq,
    std::uint32_t oldSeq)
{
    s << "Manifest: " << action
      << ";Pk: " << toBase58(TokenType::NodePublic, pk) << ";Seq: " << seq
      << ";OldSeq: " << oldSeq << ";";
    return s;
}

bool
Manifest::verify() const
{
    STObject st(sfGeneric);
    SerialIter sit(serialized.data(), serialized.size());
    st.set(sit);

    // The manifest must either have a signing key or be revoked.  This check
    // prevents us from accessing an unseated signingKey in the next check.
    if (!revoked() && !signingKey)
        return false;

    // Signing key and signature are not required for
    // master key revocations
    if (!revoked() && !ripple::verify(st, HashPrefix::manifest, *signingKey))
        return false;

    return ripple::verify(
        st, HashPrefix::manifest, masterKey, sfMasterSignature);
}

uint256
Manifest::hash() const
{
    STObject st(sfGeneric);
    SerialIter sit(serialized.data(), serialized.size());
    st.set(sit);
    return st.getHash(HashPrefix::manifest);
}

bool
Manifest::revoked() const
{
    /*
        The maximum possible sequence number means that the master key
        has been revoked.
    */
    return revoked(sequence);
}

bool
Manifest::revoked(std::uint32_t sequence)
{
    // The maximum possible sequence number means that the master key has
    // been revoked.
    return sequence == std::numeric_limits<std::uint32_t>::max();
}

std::optional<Blob>
Manifest::getSignature() const
{
    STObject st(sfGeneric);
    SerialIter sit(serialized.data(), serialized.size());
    st.set(sit);
    if (!get(st, sfSignature))
        return std::nullopt;
    return st.getFieldVL(sfSignature);
}

Blob
Manifest::getMasterSignature() const
{
    STObject st(sfGeneric);
    SerialIter sit(serialized.data(), serialized.size());
    st.set(sit);
    return st.getFieldVL(sfMasterSignature);
}

std::optional<ValidatorToken>
loadValidatorToken(std::vector<std::string> const& blob, beast::Journal journal)
{
    try
    {
        std::string tokenStr;

        tokenStr.reserve(std::accumulate(
            blob.cbegin(),
            blob.cend(),
            std::size_t(0),
            [](std::size_t init, std::string const& s) {
                return init + s.size();
            }));

        for (auto const& line : blob)
            tokenStr += boost::algorithm::trim_copy(line);

        tokenStr = base64_decode(tokenStr);

        Json::Reader r;
        Json::Value token;

        if (r.parse(tokenStr, token))
        {
            auto const m = token.get("manifest", Json::Value{});
            auto const k = token.get("validation_secret_key", Json::Value{});

            if (m.isString() && k.isString())
            {
                auto const key = strUnHex(k.asString());

                if (key && key->size() == 32)
                    return ValidatorToken{m.asString(), makeSlice(*key)};
            }
        }

        return std::nullopt;
    }
    catch (std::exception const& ex)
    {
        JLOG(journal.error())
            << "Exception in " << __func__ << ": " << ex.what();
        return std::nullopt;
    }
}

std::optional<PublicKey>
ManifestCache::getSigningKey(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};
    auto const iter = map_.find(pk);

    if (iter != map_.end() && !iter->second.revoked())
        return iter->second.signingKey;

    return pk;
}

PublicKey
ManifestCache::getMasterKey(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};

    if (auto const iter = signingToMasterKeys_.find(pk);
        iter != signingToMasterKeys_.end())
        return iter->second;

    return pk;
}

std::optional<std::uint32_t>
ManifestCache::getSequence(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};
    auto const iter = map_.find(pk);

    if (iter != map_.end() && !iter->second.revoked())
        return iter->second.sequence;

    return std::nullopt;
}

std::optional<std::string>
ManifestCache::getDomain(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};
    auto const iter = map_.find(pk);

    if (iter != map_.end() && !iter->second.revoked())
        return iter->second.domain;

    return std::nullopt;
}

std::optional<std::string>
ManifestCache::getManifest(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};
    auto const iter = map_.find(pk);

    if (iter != map_.end() && !iter->second.revoked())
        return iter->second.serialized;

    return std::nullopt;
}

bool
ManifestCache::revoked(PublicKey const& pk) const
{
    std::shared_lock lock{mutex_};
    auto const iter = map_.find(pk);

    if (iter != map_.end())
        return iter->second.revoked();

    return false;
}

ManifestDisposition
ManifestCache::applyManifest(Manifest m)
{
    // Check the manifest against the conditions that do not require a
    // `unique_lock` (write lock) on the `mutex_`. Since the signature can be
    // relatively expensive, the `checkSignature` parameter determines if the
    // signature should be checked. Since `prewriteCheck` is run twice (see
    // comment below), `checkSignature` only needs to be set to true on the
    // first run.
    auto prewriteCheck =
        [this, &m](auto const& iter, bool checkSignature, auto const& lock)
        -> std::optional<ManifestDisposition> {
        XRPL_ASSERT(
            lock.owns_lock(),
            "ripple::ManifestCache::applyManifest::prewriteCheck : locked");
        (void)lock;  // not used. parameter is present to ensure the mutex is
                     // locked when the lambda is called.
        if (iter != map_.end() && m.sequence <= iter->second.sequence)
        {
            // We received a manifest whose sequence number is not strictly
            // greater than the one we already know about. This can happen in
            // several cases including when we receive manifests from a peer who
            // doesn't have the latest data.
            if (auto stream = j_.debug())
                logMftAct(
                    stream,
                    "Stale",
                    m.masterKey,
                    m.sequence,
                    iter->second.sequence);
            return ManifestDisposition::stale;
        }

        if (checkSignature && !m.verify())
        {
            if (auto stream = j_.warn())
                logMftAct(stream, "Invalid", m.masterKey, m.sequence);
            return ManifestDisposition::invalid;
        }

        // If the master key associated with a manifest is or might be
        // compromised and is, therefore, no longer trustworthy.
        //
        // A manifest revocation essentially marks a manifest as compromised. By
        // setting the sequence number to the highest value possible, the
        // manifest is effectively neutered and cannot be superseded by a forged
        // one.
        bool const revoked = m.revoked();

        if (auto stream = j_.warn(); stream && revoked)
            logMftAct(stream, "Revoked", m.masterKey, m.sequence);

        // Sanity check: the master key of this manifest should not be used as
        // the ephemeral key of another manifest:
        if (auto const x = signingToMasterKeys_.find(m.masterKey);
            x != signingToMasterKeys_.end())
        {
            JLOG(j_.warn()) << to_string(m)
                            << ": Master key already used as ephemeral key for "
                            << toBase58(TokenType::NodePublic, x->second);

            return ManifestDisposition::badMasterKey;
        }

        if (!revoked)
        {
            if (!m.signingKey)
            {
                JLOG(j_.warn()) << to_string(m)
                                << ": is not revoked and the manifest has no "
                                   "signing key. Hence, the manifest is "
                                   "invalid";
                return ManifestDisposition::invalid;
            }

            // Sanity check: the ephemeral key of this manifest should not be
            // used as the master or ephemeral key of another manifest:
            if (auto const x = signingToMasterKeys_.find(*m.signingKey);
                x != signingToMasterKeys_.end())
            {
                JLOG(j_.warn())
                    << to_string(m)
                    << ": Ephemeral key already used as ephemeral key for "
                    << toBase58(TokenType::NodePublic, x->second);

                return ManifestDisposition::badEphemeralKey;
            }

            if (auto const x = map_.find(*m.signingKey); x != map_.end())
            {
                JLOG(j_.warn())
                    << to_string(m) << ": Ephemeral key used as master key for "
                    << to_string(x->second);

                return ManifestDisposition::badEphemeralKey;
            }
        }

        return std::nullopt;
    };

    {
        std::shared_lock sl{mutex_};
        if (auto d =
                prewriteCheck(map_.find(m.masterKey), /*checkSig*/ true, sl))
            return *d;
    }

    std::unique_lock sl{mutex_};
    auto const iter = map_.find(m.masterKey);
    // Since we released the previously held read lock, it's possible that the
    // collections have been written to. This means we need to run
    // `prewriteCheck` again. This re-does work, but `prewriteCheck` is
    // relatively inexpensive to run, and doing it this way allows us to run
    // `prewriteCheck` under a `shared_lock` above.
    // Note, the signature has already been checked above, so it
    // doesn't need to happen again (signature checks are somewhat expensive).
    // Note: It's a mistake to use an upgradable lock. This is a recipe for
    // deadlock.
    if (auto d = prewriteCheck(iter, /*checkSig*/ false, sl))
        return *d;

    bool const revoked = m.revoked();
    // This is the first manifest we are seeing for a master key. This should
    // only ever happen once per validator run.
    if (iter == map_.end())
    {
        if (auto stream = j_.info())
            logMftAct(stream, "AcceptedNew", m.masterKey, m.sequence);

        if (!revoked)
            signingToMasterKeys_.emplace(*m.signingKey, m.masterKey);

        auto masterKey = m.masterKey;
        map_.emplace(std::move(masterKey), std::move(m));
        return ManifestDisposition::accepted;
    }

    // An ephemeral key was revoked and superseded by a new key. This is
    // expected, but should happen infrequently.
    if (auto stream = j_.info())
        logMftAct(
            stream,
            "AcceptedUpdate",
            m.masterKey,
            m.sequence,
            iter->second.sequence);

    signingToMasterKeys_.erase(*iter->second.signingKey);

    if (!revoked)
        signingToMasterKeys_.emplace(*m.signingKey, m.masterKey);

    iter->second = std::move(m);

    // Something has changed. Keep track of it.
    seq_++;

    return ManifestDisposition::accepted;
}

void
ManifestCache::load(DatabaseCon& dbCon, std::string const& dbTable)
{
    auto db = dbCon.checkoutDb();
    ripple::getManifests(*db, dbTable, *this, j_);
}

bool
ManifestCache::load(
    DatabaseCon& dbCon,
    std::string const& dbTable,
    std::string const& configManifest,
    std::vector<std::string> const& configRevocation)
{
    load(dbCon, dbTable);

    if (!configManifest.empty())
    {
        auto mo = deserializeManifest(base64_decode(configManifest));
        if (!mo)
        {
            JLOG(j_.error()) << "Malformed validator_token in config";
            return false;
        }

        if (mo->revoked())
        {
            JLOG(j_.warn()) << "Configured manifest revokes public key";
        }

        if (applyManifest(std::move(*mo)) == ManifestDisposition::invalid)
        {
            JLOG(j_.error()) << "Manifest in config was rejected";
            return false;
        }
    }

    if (!configRevocation.empty())
    {
        std::string revocationStr;
        revocationStr.reserve(std::accumulate(
            configRevocation.cbegin(),
            configRevocation.cend(),
            std::size_t(0),
            [](std::size_t init, std::string const& s) {
                return init + s.size();
            }));

        for (auto const& line : configRevocation)
            revocationStr += boost::algorithm::trim_copy(line);

        auto mo = deserializeManifest(base64_decode(revocationStr));

        if (!mo || !mo->revoked() ||
            applyManifest(std::move(*mo)) == ManifestDisposition::invalid)
        {
            JLOG(j_.error()) << "Invalid validator key revocation in config";
            return false;
        }
    }

    return true;
}

void
ManifestCache::save(
    DatabaseCon& dbCon,
    std::string const& dbTable,
    std::function<bool(PublicKey const&)> const& isTrusted)
{
    std::shared_lock lock{mutex_};
    auto db = dbCon.checkoutDb();

    saveManifests(*db, dbTable, isTrusted, map_, j_);
}
}  // namespace ripple
