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
#include <cstring>
#include <stdexcept>

namespace ripple {

static
boost::optional<Manifest>
unpackManifest(void const* data, std::size_t size)
{
    STObject st(sfGeneric);
    SerialIter sit(data, size);
    st.set(sit);
    auto const mseq = get(st, sfSequence);
    auto mpk  = get<AnyPublicKey>(st, sfPublicKey);
    auto mspk = get<AnyPublicKey>(st, sfSigningPubKey);
    if (! mseq || ! mpk || ! mspk)
        return boost::optional<Manifest>();
    if (! verify(st, HashPrefix::manifest, *mpk))
        return boost::optional<Manifest>();

    std::string const s (static_cast<char const*>(data), size);
    return Manifest(s, std::move (*mpk), std::move (*mspk), *mseq);
}

void
ManifestCache::primeTrustedKey (AnyPublicKey const& pk, std::string const& comment)
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
ManifestCache::isTrustedKey (AnyPublicKey const& pk) const
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto const it = map_.find(pk);
    
    return it != map_.end()  &&  ! it->second.m;
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
            and are installed via primeTrustedKey().)
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
    std::string const& s, beast::Journal const& journal)
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto const iter = map_.find(pk);

    if (iter == map_.end())
    {
        // no trusted key
        if (journal.warning) journal.warning
            << "Ignoring untrusted manifest #" << seq;
        return false;
    }

    auto& unl = getApp().getUNL();

    auto& old = iter->second.m;

    if (old  &&  seq <= old->seq)
    {
        if (journal.warning) journal.warning
            << "Ignoring manifest #"      << seq
            << "which isn't newer than #" << old->seq;
        return false;  // not a newer manifest, ignore
    }

    // newer manifest
    auto m = unpackManifest (s.data(), s.size());

    if (! m)
    {
        if (journal.warning) journal.warning
            << "Failed to unpack manifest #" << seq;
        return false;
    }

    if (! old)
    {
        if (journal.warning) journal.warning
            << "Adding new manifest #" << seq;
    }
    else
    {
        if (journal.warning) journal.warning
            << "Dropping old manifest #" << old->seq
            << " in favor of #"          << seq;

        unl.deleteEphemeralKey (old->signingKey);
    }

    /*
        The maximum possible sequence number means that the master key
        has been revoked.
    */
    auto const revoked = std::uint32_t (-1);

    if (seq == revoked)
    {
        // The master key is revoked -- don't insert the signing key
        if (auto const& j = journal.warning)
            j << "Ignoring manifest #" << seq << " for revoked master key.";
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
    std::string const& s, STObject const& st, beast::Journal const& journal)
{
    return would_accept (pk, seq)
        && verify(st, HashPrefix::manifest, pk)
        && maybe_insert (pk, seq, s, journal);
}

void
ManifestCache::configManifest (std::string const& s, beast::Journal const& journal)
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

    maybe_insert (pk, seq, s, journal);
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
    if (key[0] != 28)
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

    primeTrustedKey (masterKey, comment);
}

}
