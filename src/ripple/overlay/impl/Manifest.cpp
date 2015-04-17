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

#include <ripple/app/main/Application.h>
#include <ripple/app/peers/UniqueNodeList.h>
#include <ripple/overlay/impl/Manifest.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Sign.h>
#include <beast/http/rfc2616.h>
#include <stdexcept>

namespace ripple {

static
boost::optional<Manifest>
unpackManifest(std::string s)
{
    STObject st(sfGeneric);
    SerialIter sit(s.data(), s.size());
    st.set(sit);
    auto const mseq = get(st, sfSequence);
    auto mpk  = get<AnyPublicKey>(st, sfPublicKey);
    auto mspk = get<AnyPublicKey>(st, sfSigningPubKey);
    if (! mseq || ! mpk || ! mspk)
        return boost::none;
    if (! verify(st, HashPrefix::manifest, *mpk))
        return boost::none;

    return Manifest(std::move (s), std::move (*mpk), std::move (*mspk), *mseq);
}

void
ManifestCache::configValidatorKey(std::string const& line, beast::Journal const& journal)
{
    auto const words = beast::rfc2616::split(line.begin(), line.end(), ' ');

    if (words.size() != 2)
    {
        throw std::runtime_error ("[validator_keys] format is `<key> <comment>");
    }

    Blob key;
    if (! Base58::decodeWithCheck (words[0], key))
    {
        throw std::runtime_error ("Error decoding validator key");
    }
    if (key.size() != 34)
    {
        throw std::runtime_error ("Expected 34-byte validator key");
    }
    if (key[0] != VER_NODE_PUBLIC)
    {
        throw std::runtime_error ("Expected VER_NODE_PUBLIC (28)");
    }
    if (key[1] != 0xED)
    {
        throw std::runtime_error ("Expected Ed25519 key (0xED)");
    }

    auto const masterKey = AnyPublicKey (key.data() + 1, key.size() - 1);
    auto const& comment = words[1];

    if (journal.debug) journal.debug
        << masterKey << " " << comment;

    addTrustedKey (masterKey, comment);
}

void
ManifestCache::configManifest (std::string s, beast::Journal const& journal)
{
    STObject st(sfGeneric);
    try
    {
        SerialIter sit(s.data(), s.size());
        st.set(sit);
    }
    catch(...)
    {
        throw std::runtime_error("Malformed manifest in config");
    }

    auto const mseq = get(st, sfSequence);
    auto const msig = get(st, sfSignature);
    auto mpk  = get<AnyPublicKey>(st, sfPublicKey);
    auto mspk = get<AnyPublicKey>(st, sfSigningPubKey);
    if (! mseq || ! msig || ! mpk || ! mspk)
    {
        throw std::runtime_error("Missing fields in manifest in config");
    }
    auto const seq = *mseq;
    auto const& sig = *msig;
    auto const& pk = *mpk;

    if (! verify(st, HashPrefix::manifest, pk))
    {
        throw std::runtime_error("Unverifiable manifest in config");
    }

    maybe_insert (pk, seq, std::move(s), journal);
}

void
ManifestCache::addTrustedKey (AnyPublicKey const& pk, std::string const& comment)
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto& value = map_[pk];

    if (value.m)
    {
        throw std::runtime_error ("New trusted validator key already has a manifest");
    }

    value.comment = comment;
}

bool
ManifestCache::would_accept (AnyPublicKey const& pk, std::uint32_t seq) const
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto const iter = map_.find(pk);
    if (iter == map_.end())
    {
        /*
            We received a manifest for a master key which is not in our
            store.  Reject it.
            (Recognized master keys live in [validator_keys] in the config,
            and are installed via addTrustedKey().)
        */
        return false;
    }

    if (! iter->second.m)
    {
        /*
            We have an entry for this master key but no manifest.  So the
            key is trusted, and we don't already have its sequence number.
            Accept it.
        */
        return true;
    }

    return seq > iter->second.m->seq;
}

bool
ManifestCache::maybe_insert (AnyPublicKey const& pk, std::uint32_t seq,
    std::string s, beast::Journal const& journal)
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto const iter = map_.find(pk);

    if (iter == map_.end())
    {
        /*
            A manifest was received whose master key we don't trust.
            Since rippled always sends all of its current manifests,
            this will happen normally any time a peer connects.
        */
        if (journal.debug) journal.debug
            << "Ignoring manifest #" << seq << " from untrusted key " << pk;
        return false;
    }

    auto& unl = getApp().getUNL();

    auto& old = iter->second.m;

    if (old  &&  seq <= old->seq)
    {
        /*
            A manifest was received for a validator we're tracking, but
            its sequence number is no higher than the one already stored.
            This will happen normally when a peer without the latest gossip
            connects.
        */
        if (journal.debug) journal.debug
            << "Ignoring manifest #"      << seq
            << "which isn't newer than #" << old->seq;
        return false;  // not a newer manifest, ignore
    }

    // newer manifest
    auto m = unpackManifest (std::move(s));

    if (! m)
    {
        /*
            A manifest is missing a field or the signature is invalid.
            This shouldn't happen normally.
        */
        if (journal.warning) journal.warning
            << "Failed to unpack manifest #" << seq;
        return false;
    }

    /*
        The maximum possible sequence number means that the master key
        has been revoked.
    */
    auto const revoked = std::uint32_t (-1);

    if (! old)
    {
        /*
            This is the first received manifest for a trusted master key
            (possibly our own).  This only happens once per validator per
            run (and possibly not at all, if there's an obsolete entry in
            [validator_keys] for a validator that no longer exists).
        */
        if (journal.info) journal.info
            << "Adding new manifest #" << seq;
    }
    else
    {
        if (seq == revoked)
        {
            /*
               The MASTER key for this validator was revoked.  This is
               expected, but should happen at most *very* rarely.
            */
            if (journal.warning) journal.warning
                << "Dropping old manifest #" << old->seq
                << " because the master key was revoked";
        }
        else
        {
            /*
                An ephemeral key was revoked and superseded by a new key.
                This is expected, but should happen infrequently.
            */
            if (journal.warning) journal.warning
                << "Dropping old manifest #" << old->seq
                << " in favor of #"          << seq;
        }

        unl.deleteEphemeralKey (old->signingKey);
    }

    if (seq == revoked)
    {
        // The master key is revoked -- don't insert the signing key
        if (auto const& j = journal.warning)
            j << "Revoking master key: " << pk;

        /*
            A validator master key has been compromised, so its manifests
            are now untrustworthy.  In order to prevent us from accepting
            a forged manifest signed by the compromised master key, store
            this manifest, which has the highest possible sequence number
            and therefore can't be superseded by a forged one.
        */
    }
    else
    {
        unl.insertEphemeralKey (m->signingKey, iter->second.comment);
    }

    old = std::move (m);

    return true;
}

bool
ManifestCache::maybe_accept (AnyPublicKey const& pk, std::uint32_t seq,
    std::string s, STObject const& st, beast::Journal const& journal)
{
    return would_accept (pk, seq)
        && verify(st, HashPrefix::manifest, pk)
        && maybe_insert (pk, seq, std::move(s), journal);
}

}
