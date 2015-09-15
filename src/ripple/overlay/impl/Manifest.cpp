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
#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/overlay/impl/Manifest.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Sign.h>
#include <beast/http/rfc2616.h>
#include <stdexcept>

namespace ripple {

boost::optional<Manifest>
make_Manifest (std::string s)
{
    try
    {
        STObject st (sfGeneric);
        SerialIter sit (s.data (), s.size ());
        st.set (sit);
        auto const opt_pk = get<PublicKey>(st, sfPublicKey);
        auto const opt_spk = get<PublicKey>(st, sfSigningPubKey);
        auto const opt_seq = get (st, sfSequence);
        auto const opt_sig = get (st, sfSignature);
        if (!opt_pk || !opt_spk || !opt_seq || !opt_sig)
        {
            return boost::none;
        }
        return Manifest (std::move (s), *opt_pk, *opt_spk, *opt_seq);
    }
    catch (...)
    {
        return boost::none;
    }
}

template<class Stream>
Stream&
logMftAct (
    Stream& s,
    std::string const& action,
    PublicKey const& pk,
    std::uint32_t seq)
{
    s << "Manifest: " << action <<
         ";Pk: " << toBase58 (TokenType::TOKEN_NODE_PUBLIC, pk) <<
         ";Seq: " << seq << ";";
    return s;
}

template<class Stream>
Stream& logMftAct (
    Stream& s,
    std::string const& action,
    PublicKey const& pk,
    std::uint32_t seq,
    std::uint32_t oldSeq)
{
    s << "Manifest: " << action <<
         ";Pk: " << toBase58 (TokenType::TOKEN_NODE_PUBLIC, pk) <<
         ";Seq: " << seq <<
         ";OldSeq: " << oldSeq << ";";
    return s;
}

Manifest::Manifest (std::string s,
                    PublicKey pk,
                    PublicKey spk,
                    std::uint32_t seq)
    : serialized (std::move (s))
    , masterKey (std::move (pk))
    , signingKey (std::move (spk))
    , sequence (seq)
{
}

bool Manifest::verify () const
{
    STObject st (sfGeneric);
    SerialIter sit (serialized.data (), serialized.size ());
    st.set (sit);
    return ripple::verify (st, HashPrefix::manifest, masterKey, true);
}

uint256 Manifest::hash () const
{
    STObject st (sfGeneric);
    SerialIter sit (serialized.data (), serialized.size ());
    st.set (sit);
    return st.getHash (HashPrefix::manifest);
}

bool Manifest::revoked () const
{
    /*
        The maximum possible sequence number means that the master key
        has been revoked.
    */
    return sequence == std::numeric_limits<std::uint32_t>::max ();
}

void
ManifestCache::configValidatorKey(
    std::string const& line, beast::Journal const& journal)
{
    auto const words = beast::rfc2616::split(line.begin(), line.end(), ' ');

    if (words.size () != 2)
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
    if (key[0] != TOKEN_NODE_PUBLIC)
    {
        throw std::runtime_error ("Expected TOKEN_NODE_PUBLIC (28)");
    }
    if (key[1] != 0xED)
    {
        throw std::runtime_error ("Expected Ed25519 key (0xED)");
    }

    auto const masterKey = PublicKey (Slice(key.data() + 1, key.size() - 1));
    std::string comment = std::move(words[1]);

    if (journal.debug) journal.debug
        << toBase58(TokenType::TOKEN_NODE_PUBLIC, masterKey) << " " << comment;

    addTrustedKey (masterKey, std::move(comment));
}

void
ManifestCache::configManifest (
    Manifest m, UniqueNodeList& unl, beast::Journal const& journal)
{
    if (!m.verify())
    {
        throw std::runtime_error("Unverifiable manifest in config");
    }

    auto const result = applyManifest (std::move(m), unl, journal);

    if (result != ManifestDisposition::accepted)
    {
        throw std::runtime_error("Our own validation manifest was not accepted");
    }
}

void
ManifestCache::addTrustedKey (PublicKey const& pk, std::string comment)
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto& value = map_[pk];

    if (value.m)
    {
        throw std::runtime_error (
            "New trusted validator key already has a manifest");
    }

    value.comment = std::move(comment);
}

ManifestDisposition
ManifestCache::canApply (PublicKey const& pk, std::uint32_t seq,
    beast::Journal const& journal) const
{
    auto const iter = map_.find(pk);

    if (iter == map_.end())
    {
        /*
            A manifest was received whose master key we don't trust.
            Since rippled always sends all of its current manifests,
            this will happen normally any time a peer connects.
        */
        if (journal.debug)
            logMftAct(journal.debug, "Untrusted", pk, seq);
        return ManifestDisposition::untrusted;
    }

    auto& old = iter->second.m;

    if (old  &&  seq <= old->sequence)
    {
        /*
            A manifest was received for a validator we're tracking, but
            its sequence number is no higher than the one already stored.
            This will happen normally when a peer without the latest gossip
            connects.
        */
        if (journal.debug)
            logMftAct(journal.debug, "Stale", pk, seq, old->sequence);
        return ManifestDisposition::stale;  // not a newer manifest, ignore
    }

    return ManifestDisposition::accepted;
}


ManifestDisposition
ManifestCache::applyManifest (
    Manifest m, UniqueNodeList& unl, beast::Journal const& journal)
{
    {
        std::lock_guard<std::mutex> lock (mutex_);

        /*
            before we spend time checking the signature, make sure we trust the
            master key and the sequence number is newer than any we have.
        */
        auto const chk = canApply(m.masterKey, m.sequence, journal);

        if (chk != ManifestDisposition::accepted)
        {
            return chk;
        }
    }

    if (! m.verify())
    {
        /*
          A manifest's signature is invalid.
          This shouldn't happen normally.
        */
        if (journal.warning)
            logMftAct(journal.warning, "Invalid", m.masterKey, m.sequence);
        return ManifestDisposition::invalid;
    }

    std::lock_guard<std::mutex> lock (mutex_);

    /*
        We released the lock above, so we have to check again, in case
        another thread accepted a newer manifest.
    */
    auto const chk = canApply(m.masterKey, m.sequence, journal);

    if (chk != ManifestDisposition::accepted)
    {
        return chk;
    }

    auto const iter = map_.find(m.masterKey);

    auto& old = iter->second.m;

    if (! old)
    {
        /*
            This is the first received manifest for a trusted master key
            (possibly our own).  This only happens once per validator per
            run (and possibly not at all, if there's an obsolete entry in
            [validator_keys] for a validator that no longer exists).
        */
        if (journal.info)
            logMftAct(journal.info, "AcceptedNew", m.masterKey, m.sequence);
    }
    else
    {
        if (m.revoked ())
        {
            /*
               The MASTER key for this validator was revoked.  This is
               expected, but should happen at most *very* rarely.
            */
            if (journal.info)
                logMftAct(journal.info, "Revoked",
                          m.masterKey, m.sequence, old->sequence);
        }
        else
        {
            /*
                An ephemeral key was revoked and superseded by a new key.
                This is expected, but should happen infrequently.
            */
            if (journal.info)
                logMftAct(journal.info, "AcceptedUpdate",
                          m.masterKey, m.sequence, old->sequence);
        }

        unl.deleteEphemeralKey (old->signingKey);
    }

    if (m.revoked ())
    {
        // The master key is revoked -- don't insert the signing key
        if (journal.warning)
            logMftAct(journal.warning, "Revoked", m.masterKey, m.sequence);

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
        unl.insertEphemeralKey (m.signingKey, iter->second.comment);
    }

    old = std::move(m);

    return ManifestDisposition::accepted;
}

void ManifestCache::load (
    DatabaseCon& dbCon, UniqueNodeList& unl, beast::Journal const& journal)
{
    static const char* const sql =
        "SELECT RawData FROM ValidatorManifests;";
    auto db = dbCon.checkoutDb ();
    soci::blob sociRawData (*db);
    soci::statement st =
        (db->prepare << sql,
             soci::into (sociRawData));
    st.execute ();
    while (st.fetch ())
    {
        std::string serialized;
        convert (sociRawData, serialized);
        if (auto mo = make_Manifest (std::move (serialized)))
        {
            if (!mo->verify())
            {
                throw std::runtime_error("Unverifiable manifest in db");
            }
            // add trusted key
            map_[mo->masterKey];

            // OK if not accepted (may have been loaded from the config file)
            applyManifest (std::move(*mo), unl, journal);
        }
        else
        {
            if (journal.warning)
                journal.warning << "Malformed manifest in database";
        }
    }
}

void ManifestCache::save (DatabaseCon& dbCon) const
{
    auto db = dbCon.checkoutDb ();

    soci::transaction tr(*db);
    *db << "DELETE FROM ValidatorManifests";
    static const char* const sql =
        "INSERT INTO ValidatorManifests (RawData) VALUES (:rawData);";
    // soci does not support bulk insertion of blob data
    soci::blob rawData(*db);
    for (auto const& v : map_)
    {
        if (!v.second.m)
            continue;

        convert (v.second.m->serialized, rawData);
        *db << sql,
            soci::use (rawData);
    }
    tr.commit ();
}
}
