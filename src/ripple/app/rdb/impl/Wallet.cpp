//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/rdb/Wallet.h>
#include <boost/format.hpp>

namespace ripple {

std::unique_ptr<DatabaseCon>
makeWalletDB(DatabaseCon::Setup const& setup)
{
    // wallet database
    return std::make_unique<DatabaseCon>(
        setup, WalletDBName, std::array<char const*, 0>(), WalletDBInit);
}

std::unique_ptr<DatabaseCon>
makeTestWalletDB(DatabaseCon::Setup const& setup, std::string const& dbname)
{
    // wallet database
    return std::make_unique<DatabaseCon>(
        setup, dbname.data(), std::array<char const*, 0>(), WalletDBInit);
}

void
getManifests(
    soci::session& session,
    std::string const& dbTable,
    ManifestCache& mCache,
    beast::Journal j)
{
    // Load manifests stored in database
    std::string const sql = "SELECT RawData FROM " + dbTable + ";";
    soci::blob sociRawData(session);
    soci::statement st = (session.prepare << sql, soci::into(sociRawData));
    st.execute();
    while (st.fetch())
    {
        std::string serialized;
        convert(sociRawData, serialized);
        if (auto mo = deserializeManifest(serialized))
        {
            if (!mo->verify())
            {
                JLOG(j.warn()) << "Unverifiable manifest in db";
                continue;
            }

            mCache.applyManifest(std::move(*mo));
        }
        else
        {
            JLOG(j.warn()) << "Malformed manifest in database";
        }
    }
}

static void
saveManifest(
    soci::session& session,
    std::string const& dbTable,
    std::string const& serialized)
{
    // soci does not support bulk insertion of blob data
    // Do not reuse blob because manifest ecdsa signatures vary in length
    // but blob write length is expected to be >= the last write
    soci::blob rawData(session);
    convert(serialized, rawData);
    session << "INSERT INTO " << dbTable << " (RawData) VALUES (:rawData);",
        soci::use(rawData);
}

void
saveManifests(
    soci::session& session,
    std::string const& dbTable,
    std::function<bool(PublicKey const&)> const& isTrusted,
    hash_map<PublicKey, Manifest> const& map,
    beast::Journal j)
{
    soci::transaction tr(session);
    session << "DELETE FROM " << dbTable;
    for (auto const& v : map)
    {
        // Save all revocation manifests,
        // but only save trusted non-revocation manifests.
        if (!v.second.revoked() && !isTrusted(v.second.masterKey))
        {
            JLOG(j.info()) << "Untrusted manifest in cache not saved to db";
            continue;
        }

        saveManifest(session, dbTable, v.second.serialized);
    }
    tr.commit();
}

void
addValidatorManifest(soci::session& session, std::string const& serialized)
{
    soci::transaction tr(session);
    saveManifest(session, "ValidatorManifests", serialized);
    tr.commit();
}

void
clearNodeIdentity(soci::session& session)
{
    session << "DELETE FROM NodeIdentity;";
}

std::pair<PublicKey, SecretKey>
getNodeIdentity(soci::session& session)
{
    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::string> pubKO, priKO;
        soci::statement st =
            (session.prepare
                 << "SELECT PublicKey, PrivateKey FROM NodeIdentity;",
             soci::into(pubKO),
             soci::into(priKO));
        st.execute();
        while (st.fetch())
        {
            auto const sk = parseBase58<SecretKey>(
                TokenType::NodePrivate, priKO.value_or(""));
            auto const pk = parseBase58<PublicKey>(
                TokenType::NodePublic, pubKO.value_or(""));

            // Only use if the public and secret keys are a pair
            if (sk && pk && (*pk == derivePublicKey(KeyType::secp256k1, *sk)))
                return {*pk, *sk};
        }
    }

    // If a valid identity wasn't found, we randomly generate a new one:
    auto [newpublicKey, newsecretKey] = randomKeyPair(KeyType::secp256k1);

    session << str(
        boost::format("INSERT INTO NodeIdentity (PublicKey,PrivateKey) "
                      "VALUES ('%s','%s');") %
        toBase58(TokenType::NodePublic, newpublicKey) %
        toBase58(TokenType::NodePrivate, newsecretKey));

    return {newpublicKey, newsecretKey};
}

std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>
getPeerReservationTable(soci::session& session, beast::Journal j)
{
    std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual> table;
    // These values must be boost::optionals (not std) because SOCI expects
    // boost::optionals.
    boost::optional<std::string> valPubKey, valDesc;
    // We should really abstract the table and column names into constants,
    // but no one else does. Because it is too tedious? It would be easy if we
    // had a jOOQ for C++.
    soci::statement st =
        (session.prepare
             << "SELECT PublicKey, Description FROM PeerReservations;",
         soci::into(valPubKey),
         soci::into(valDesc));
    st.execute();
    while (st.fetch())
    {
        if (!valPubKey || !valDesc)
        {
            // This represents a `NULL` in a `NOT NULL` column. It should be
            // unreachable.
            continue;
        }
        auto const optNodeId =
            parseBase58<PublicKey>(TokenType::NodePublic, *valPubKey);
        if (!optNodeId)
        {
            JLOG(j.warn()) << "load: not a public key: " << valPubKey;
            continue;
        }
        table.insert(PeerReservation{*optNodeId, *valDesc});
    }

    return table;
}

void
insertPeerReservation(
    soci::session& session,
    PublicKey const& nodeId,
    std::string const& description)
{
    auto const sNodeId = toBase58(TokenType::NodePublic, nodeId);
    session << "INSERT INTO PeerReservations (PublicKey, Description) "
               "VALUES (:nodeId, :desc) "
               "ON CONFLICT (PublicKey) DO UPDATE SET "
               "Description=excluded.Description",
        soci::use(sNodeId), soci::use(description);
}

void
deletePeerReservation(soci::session& session, PublicKey const& nodeId)
{
    auto const sNodeId = toBase58(TokenType::NodePublic, nodeId);
    session << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId",
        soci::use(sNodeId);
}

bool
createFeatureVotes(soci::session& session)
{
    soci::transaction tr(session);
    std::string sql =
        "SELECT count(*) FROM sqlite_master "
        "WHERE type='table' AND name='FeatureVotes'";
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<int> featureVotesCount;
    session << sql, soci::into(featureVotesCount);
    bool exists = static_cast<bool>(*featureVotesCount);

    // Create FeatureVotes table in WalletDB if it doesn't exist
    if (!exists)
    {
        session << "CREATE TABLE  FeatureVotes ( "
                   "AmendmentHash      CHARACTER(64) NOT NULL, "
                   "AmendmentName      TEXT, "
                   "Veto               INTEGER NOT NULL );";
        tr.commit();
    }
    return exists;
}

void
readAmendments(
    soci::session& session,
    std::function<void(
        boost::optional<std::string> amendment_hash,
        boost::optional<std::string> amendment_name,
        boost::optional<AmendmentVote> vote)> const& callback)
{
    // lambda that converts the internally stored int to an AmendmentVote.
    auto intToVote = [](boost::optional<int> const& dbVote)
        -> boost::optional<AmendmentVote> {
        return safe_cast<AmendmentVote>(dbVote.value_or(1));
    };

    soci::transaction tr(session);
    std::string sql =
        "SELECT AmendmentHash, AmendmentName, Veto FROM "
        "( SELECT AmendmentHash, AmendmentName, Veto, RANK() OVER "
        "(  PARTITION BY AmendmentHash ORDER BY ROWID DESC ) "
        "as rnk FROM FeatureVotes ) WHERE rnk = 1";
    // SOCI requires boost::optional (not std::optional) as parameters.
    boost::optional<std::string> amendment_hash;
    boost::optional<std::string> amendment_name;
    boost::optional<int> vote_to_veto;
    soci::statement st =
        (session.prepare << sql,
         soci::into(amendment_hash),
         soci::into(amendment_name),
         soci::into(vote_to_veto));
    st.execute();
    while (st.fetch())
    {
        callback(amendment_hash, amendment_name, intToVote(vote_to_veto));
    }
}

void
voteAmendment(
    soci::session& session,
    uint256 const& amendment,
    std::string const& name,
    AmendmentVote vote)
{
    soci::transaction tr(session);
    std::string sql =
        "INSERT INTO FeatureVotes (AmendmentHash, AmendmentName, Veto) VALUES "
        "('";
    sql += to_string(amendment);
    sql += "', '" + name;
    sql += "', '" + std::to_string(safe_cast<int>(vote)) + "');";
    session << sql;
    tr.commit();
}

}  // namespace ripple
