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

#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/Sign.h>
#include <beast/core/detail/base64.hpp>
#include <stdexcept>

namespace ripple {

boost::optional<Manifest>
Manifest::make_Manifest (std::string s)
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
        auto const opt_msig = get (st, sfMasterSignature);
        if (!opt_pk || !opt_spk || !opt_seq || !opt_sig || !opt_msig)
        {
            return boost::none;
        }
        return Manifest (std::move (s), *opt_pk, *opt_spk, *opt_seq);
    }
    catch (std::exception const&)
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
    if (! ripple::verify (st, HashPrefix::manifest, signingKey))
        return false;

    return ripple::verify (
        st, HashPrefix::manifest, masterKey, sfMasterSignature);
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

Blob Manifest::getSignature () const
{
    STObject st (sfGeneric);
    SerialIter sit (serialized.data (), serialized.size ());
    st.set (sit);
    return st.getFieldVL (sfSignature);
}

Blob Manifest::getMasterSignature () const
{
    STObject st (sfGeneric);
    SerialIter sit (serialized.data (), serialized.size ());
    st.set (sit);
    return st.getFieldVL (sfMasterSignature);
}

PublicKey
ManifestCache::getSigningKey (PublicKey const& pk) const
{
    std::lock_guard<std::mutex> lock{read_mutex_};
    auto const iter = map_.find (pk);

    if (iter != map_.end () && !iter->second.revoked ())
        return iter->second.signingKey;

    return pk;
}

PublicKey
ManifestCache::getMasterKey (PublicKey const& pk) const
{
    std::lock_guard<std::mutex> lock{read_mutex_};
    auto const iter = signingToMasterKeys_.find (pk);

    if (iter != signingToMasterKeys_.end ())
        return iter->second;

    return pk;
}

bool
ManifestCache::revoked (PublicKey const& pk) const
{
    std::lock_guard<std::mutex> lock{read_mutex_};
    auto const iter = map_.find (pk);

    if (iter != map_.end ())
        return iter->second.revoked ();

    return false;
}

ManifestDisposition
ManifestCache::applyManifest (Manifest m)
{
    std::lock_guard<std::mutex> applyLock{apply_mutex_};

    /*
        before we spend time checking the signature, make sure the
        sequence number is newer than any we have.
    */
    auto const iter = map_.find(m.masterKey);

    if (iter != map_.end() &&
        m.sequence <= iter->second.sequence)
    {
        /*
            A manifest was received for a validator we're tracking, but
            its sequence number is not higher than the one already stored.
            This will happen normally when a peer without the latest gossip
            connects.
        */
        if (auto stream = j_.debug())
            logMftAct(stream, "Stale", m.masterKey, m.sequence, iter->second.sequence);
        return ManifestDisposition::stale;  // not a newer manifest, ignore
    }

    if (! m.verify())
    {
        /*
          A manifest's signature is invalid.
          This shouldn't happen normally.
        */
        if (auto stream = j_.warn())
            logMftAct(stream, "Invalid", m.masterKey, m.sequence);
        return ManifestDisposition::invalid;
    }

    std::lock_guard<std::mutex> readLock{read_mutex_};

    bool const revoked = m.revoked();

    if (iter == map_.end ())
    {
        /*
            This is the first received manifest for a trusted master key
            (possibly our own).  This only happens once per validator per
            run.
        */
        if (auto stream = j_.info())
            logMftAct(stream, "AcceptedNew", m.masterKey, m.sequence);

        if (! revoked)
            signingToMasterKeys_[m.signingKey] = m.masterKey;

        map_.emplace (std::make_pair(m.masterKey, std::move (m)));
    }
    else
    {
        /*
            An ephemeral key was revoked and superseded by a new key.
            This is expected, but should happen infrequently.
        */
        if (auto stream = j_.info())
            logMftAct(stream, "AcceptedUpdate",
                      m.masterKey, m.sequence, iter->second.sequence);

        signingToMasterKeys_.erase (iter->second.signingKey);

        if (! revoked)
            signingToMasterKeys_[m.signingKey] = m.masterKey;

        iter->second = std::move (m);
    }

    if (revoked)
    {
        /*
            A validator master key has been compromised, so its manifests
            are now untrustworthy.  In order to prevent us from accepting
            a forged manifest signed by the compromised master key, store
            this manifest, which has the highest possible sequence number
            and therefore can't be superseded by a forged one.
        */
        if (auto stream = j_.warn())
            logMftAct(stream, "Revoked", m.masterKey, m.sequence);
    }

    return ManifestDisposition::accepted;
}

void
ManifestCache::load (
    DatabaseCon& dbCon, std::string const& dbTable)
{
    // Load manifests stored in database
    std::string const sql =
        "SELECT RawData FROM " + dbTable + ";";
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
        if (auto mo = Manifest::make_Manifest (std::move (serialized)))
        {
            if (!mo->verify())
            {
                JLOG(j_.warn())
                    << "Unverifiable manifest in db";
                continue;
            }

            applyManifest (std::move(*mo));
        }
        else
        {
            JLOG(j_.warn())
                << "Malformed manifest in database";
        }
    }
}

bool
ManifestCache::load (
    DatabaseCon& dbCon, std::string const& dbTable,
    std::vector<std::string> const& configManifest)
{
    load (dbCon, dbTable);

    if (! configManifest.empty())
    {
        std::string s;
        s.reserve (Manifest::textLength);
        for (auto const& line : configManifest)
            s += beast::rfc2616::trim(line);

        auto mo = Manifest::make_Manifest (beast::detail::base64_decode(s));
        if (! mo)
        {
            JLOG (j_.error()) << "Malformed manifest in config";
            return false;
        }

        if (mo->revoked())
        {
            JLOG (j_.warn()) <<
                "Configured manifest revokes public key";
        }

        if (applyManifest (std::move(*mo)) ==
            ManifestDisposition::invalid)
        {
            JLOG (j_.error()) << "Manifest in config was rejected";
            return false;
        }
    }

    return true;
}

void ManifestCache::save (
    DatabaseCon& dbCon, std::string const& dbTable,
    std::function <bool (PublicKey const&)> isTrusted)
{
    std::lock_guard<std::mutex> lock{apply_mutex_};

    auto db = dbCon.checkoutDb ();

    soci::transaction tr(*db);
    *db << "DELETE FROM " << dbTable;
    std::string const sql =
        "INSERT INTO " + dbTable + " (RawData) VALUES (:rawData);";
    for (auto const& v : map_)
    {
        if (! isTrusted (v.second.masterKey))
        {

            JLOG(j_.info())
               << "Untrusted manifest in cache not saved to db";
            continue;
        }

        // soci does not support bulk insertion of blob data
        // Do not reuse blob because manifest ecdsa signatures vary in length
        // but blob write length is expected to be >= the last write
        soci::blob rawData(*db);
        convert (v.second.serialized, rawData);
        *db << sql,
            soci::use (rawData);
    }
    tr.commit ();
}
}
