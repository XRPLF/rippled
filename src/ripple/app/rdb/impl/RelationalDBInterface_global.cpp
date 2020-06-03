//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/json/to_string.h>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

/* Wallet DB */

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
    std::function<bool(PublicKey const&)> isTrusted,
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
    session << "INSERT INTO PeerReservations (PublicKey, Description) "
               "VALUES (:nodeId, :desc) "
               "ON CONFLICT (PublicKey) DO UPDATE SET "
               "Description=excluded.Description",
        soci::use(toBase58(TokenType::NodePublic, nodeId)),
        soci::use(description);
}

void
deletePeerReservation(soci::session& session, PublicKey const& nodeId)
{
    session << "DELETE FROM PeerReservations WHERE PublicKey = :nodeId",
        soci::use(toBase58(TokenType::NodePublic, nodeId));
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
        boost::optional<int> vote_to_veto)> const& callback)
{
    soci::transaction tr(session);
    std::string sql =
        "SELECT AmendmentHash, AmendmentName, Veto FROM FeatureVotes";
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
        callback(amendment_hash, amendment_name, vote_to_veto);
    }
}

void
voteAmendment(
    soci::session& session,
    uint256 const& amendment,
    std::string const& name,
    bool vote_to_veto)
{
    soci::transaction tr(session);
    std::string sql =
        "INSERT INTO FeatureVotes (AmendmentHash, AmendmentName, Veto) VALUES "
        "('";
    sql += to_string(amendment);
    sql += "', '" + name;
    sql += "', '" + std::to_string(int{vote_to_veto}) + "');";
    session << sql;
    tr.commit();
}

/* State DB */

void
initStateDB(
    soci::session& session,
    BasicConfig const& config,
    std::string const& dbName)
{
    open(session, config, dbName);

    session << "PRAGMA synchronous=FULL;";

    session << "CREATE TABLE IF NOT EXISTS DbState ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  WritableDb             TEXT,"
               "  ArchiveDb              TEXT,"
               "  LastRotatedLedger      INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS CanDelete ("
               "  Key                    INTEGER PRIMARY KEY,"
               "  CanDeleteSeq           INTEGER"
               ");";

    std::int64_t count = 0;
    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM DbState WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from DbState.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO DbState VALUES (1, '', '', 0);";
    }

    {
        // SOCI requires boost::optional (not std::optional) as the parameter.
        boost::optional<std::int64_t> countO;
        session << "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;",
            soci::into(countO);
        if (!countO)
            Throw<std::runtime_error>(
                "Failed to fetch Key Count from CanDelete.");
        count = *countO;
    }

    if (!count)
    {
        session << "INSERT INTO CanDelete VALUES (1, 0);";
    }
}

LedgerIndex
getCanDelete(soci::session& session)
{
    LedgerIndex seq;
    session << "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;",
        soci::into(seq);
    ;
    return seq;
}

LedgerIndex
setCanDelete(soci::session& session, LedgerIndex canDelete)
{
    session << "UPDATE CanDelete SET CanDeleteSeq = :canDelete WHERE Key = 1;",
        soci::use(canDelete);
    return canDelete;
}

SavedState
getSavedState(soci::session& session)
{
    SavedState state;
    session << "SELECT WritableDb, ArchiveDb, LastRotatedLedger"
               " FROM DbState WHERE Key = 1;",
        soci::into(state.writableDb), soci::into(state.archiveDb),
        soci::into(state.lastRotated);

    return state;
}

void
setSavedState(soci::session& session, SavedState const& state)
{
    session << "UPDATE DbState"
               " SET WritableDb = :writableDb,"
               " ArchiveDb = :archiveDb,"
               " LastRotatedLedger = :lastRotated"
               " WHERE Key = 1;",
        soci::use(state.writableDb), soci::use(state.archiveDb),
        soci::use(state.lastRotated);
}

void
setLastRotated(soci::session& session, LedgerIndex seq)
{
    session << "UPDATE DbState SET LastRotatedLedger = :seq"
               " WHERE Key = 1;",
        soci::use(seq);
}

/* DatabaseBody DB */

std::pair<std::unique_ptr<DatabaseCon>, std::optional<std::uint64_t>>
openDatabaseBodyDb(
    DatabaseCon::Setup const& setup,
    boost::filesystem::path path)
{
    // SOCI requires boost::optional (not std::optional) as the parameter.
    boost::optional<std::string> pathFromDb;
    boost::optional<std::uint64_t> size;

    auto conn = std::make_unique<DatabaseCon>(
        setup, "Download", DownloaderDBPragma, DatabaseBodyDBInit);

    auto& session = *conn->checkoutDb();

    session << "SELECT Path FROM Download WHERE Part=0;",
        soci::into(pathFromDb);

    // Try to reuse preexisting
    // database.
    if (pathFromDb)
    {
        // Can't resuse - database was
        // from a different file download.
        if (pathFromDb != path.string())
        {
            session << "DROP TABLE Download;";
        }

        // Continuing a file download.
        else
        {
            session << "SELECT SUM(LENGTH(Data)) FROM Download;",
                soci::into(size);
        }
    }

    return {std::move(conn), (size ? *size : std::optional<std::uint64_t>())};
}

std::uint64_t
databaseBodyDoPut(
    soci::session& session,
    std::string const& data,
    std::string const& path,
    std::uint64_t fileSize,
    std::uint64_t part,
    std::uint16_t maxRowSizePad)
{
    std::uint64_t rowSize = 0;
    soci::indicator rti;

    std::uint64_t remainingInRow = 0;

    auto be =
        dynamic_cast<soci::sqlite3_session_backend*>(session.get_backend());
    BOOST_ASSERT(be);

    // This limits how large we can make the blob
    // in each row. Also subtract a pad value to
    // account for the other values in the row.
    auto const blobMaxSize =
        sqlite_api::sqlite3_limit(be->conn_, SQLITE_LIMIT_LENGTH, -1) -
        maxRowSizePad;

    std::string newpath;

    auto rowInit = [&] {
        session << "INSERT INTO Download VALUES (:path, zeroblob(0), 0, :part)",
            soci::use(newpath), soci::use(part);

        remainingInRow = blobMaxSize;
        rowSize = 0;
    };

    session << "SELECT Path,Size,Part FROM Download ORDER BY Part DESC "
               "LIMIT 1",
        soci::into(newpath), soci::into(rowSize), soci::into(part, rti);

    if (!session.got_data())
    {
        newpath = path;
        rowInit();
    }
    else
        remainingInRow = blobMaxSize - rowSize;

    auto insert = [&session, &rowSize, &part, &fs = fileSize](
                      auto const& data) {
        std::uint64_t updatedSize = rowSize + data.size();

        session << "UPDATE Download SET Data = CAST(Data || :data AS blob), "
                   "Size = :size WHERE Part = :part;",
            soci::use(data), soci::use(updatedSize), soci::use(part);

        fs += data.size();
    };

    size_t currentBase = 0;

    while (currentBase + remainingInRow < data.size())
    {
        if (remainingInRow)
        {
            insert(data.substr(currentBase, remainingInRow));
            currentBase += remainingInRow;
        }

        ++part;
        rowInit();
    }

    insert(data.substr(currentBase));

    return part;
}

void
databaseBodyFinish(soci::session& session, std::ofstream& fout)
{
    soci::rowset<std::string> rs =
        (session.prepare << "SELECT Data FROM Download ORDER BY PART ASC;");

    // iteration through the resultset:
    for (auto it = rs.begin(); it != rs.end(); ++it)
        fout.write(it->data(), it->size());
}

/* Vacuum DB */

bool
doVacuumDB(DatabaseCon::Setup const& setup)
{
    using namespace boost::filesystem;
    path dbPath = setup.dataDir / TxDBName;

    uintmax_t const dbSize = file_size(dbPath);
    assert(dbSize != static_cast<uintmax_t>(-1));

    if (auto available = space(dbPath.parent_path()).available;
        available < dbSize)
    {
        std::cerr << "The database filesystem must have at least as "
                     "much free space as the size of "
                  << dbPath.string() << ", which is " << dbSize
                  << " bytes. Only " << available << " bytes are available.\n";
        return false;
    }

    auto txnDB =
        std::make_unique<DatabaseCon>(setup, TxDBName, TxDBPragma, TxDBInit);
    auto& session = txnDB->getSession();
    std::uint32_t pageSize;

    // Only the most trivial databases will fit in memory on typical
    // (recommended) software. Force temp files to be written to disk
    // regardless of the config settings.
    session << boost::format(CommonDBPragmaTemp) % "file";
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM beginning. page_size: " << pageSize << std::endl;

    session << "VACUUM;";
    assert(setup.globalPragma);
    for (auto const& p : *setup.globalPragma)
        session << p;
    session << "PRAGMA page_size;", soci::into(pageSize);

    std::cout << "VACUUM finished. page_size: " << pageSize << std::endl;

    return true;
}

/* PeerFinder DB */

void
initPeerFinderDB(
    soci::session& session,
    BasicConfig const& config,
    beast::Journal j)
{
    DBConfig m_sociConfig(config, "peerfinder");
    m_sociConfig.open(session);

    JLOG(j.info()) << "Opening database at '" << m_sociConfig.connectionString()
                   << "'";

    soci::transaction tr(session);
    session << "PRAGMA encoding=\"UTF-8\";";

    session << "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
               "  name             TEXT PRIMARY KEY, "
               "  version          INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
               "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
               "  address  TEXT UNIQUE NOT NULL, "
               "  valence  INTEGER"
               ");";

    session << "CREATE INDEX IF NOT EXISTS "
               "  PeerFinder_BootstrapCache_Index ON "
               "PeerFinder_BootstrapCache "
               "  (  "
               "    address "
               "  ); ";

    tr.commit();
}

void
updatePeerFinderDB(
    soci::session& session,
    int currentSchemaVersion,
    beast::Journal j)
{
    soci::transaction tr(session);
    // get version
    int version(0);
    {
        // SOCI requires a boost::optional (not std::optional) parameter.
        boost::optional<int> vO;
        session << "SELECT "
                   "  version "
                   "FROM SchemaVersion WHERE "
                   "  name = 'PeerFinder';",
            soci::into(vO);

        version = vO.value_or(0);

        JLOG(j.info()) << "Opened version " << version << " database";
    }

    {
        if (version < currentSchemaVersion)
        {
            JLOG(j.info()) << "Updating database to version "
                           << currentSchemaVersion;
        }
        else if (version > currentSchemaVersion)
        {
            Throw<std::runtime_error>(
                "The PeerFinder database version is higher than expected");
        }
    }

    if (version < 4)
    {
        //
        // Remove the "uptime" column from the bootstrap table
        //

        session << "CREATE TABLE IF NOT EXISTS "
                   "PeerFinder_BootstrapCache_Next ( "
                   "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "  address  TEXT UNIQUE NOT NULL, "
                   "  valence  INTEGER"
                   ");";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Next_Index ON "
                   "    PeerFinder_BootstrapCache_Next "
                   "  ( address ); ";

        std::size_t count;
        session << "SELECT COUNT(*) FROM PeerFinder_BootstrapCache;",
            soci::into(count);

        std::vector<PeerFinder::Store::Entry> list;

        {
            list.reserve(count);
            std::string s;
            int valence;
            soci::statement st =
                (session.prepare << "SELECT "
                                    " address, "
                                    " valence "
                                    "FROM PeerFinder_BootstrapCache;",
                 soci::into(s),
                 soci::into(valence));

            st.execute();
            while (st.fetch())
            {
                PeerFinder::Store::Entry entry;
                entry.endpoint = beast::IP::Endpoint::from_string(s);
                if (!is_unspecified(entry.endpoint))
                {
                    entry.valence = valence;
                    list.push_back(entry);
                }
                else
                {
                    JLOG(j.error()) << "Bad address string '" << s
                                    << "' in Bootcache table";
                }
            }
        }

        if (!list.empty())
        {
            std::vector<std::string> s;
            std::vector<int> valence;
            s.reserve(list.size());
            valence.reserve(list.size());

            for (auto iter(list.cbegin()); iter != list.cend(); ++iter)
            {
                s.emplace_back(to_string(iter->endpoint));
                valence.emplace_back(iter->valence);
            }

            session << "INSERT INTO PeerFinder_BootstrapCache_Next ( "
                       "  address, "
                       "  valence "
                       ") VALUES ( "
                       "  :s, :valence"
                       ");",
                soci::use(s), soci::use(valence);
        }

        session << "DROP TABLE IF EXISTS PeerFinder_BootstrapCache;";

        session << "DROP INDEX IF EXISTS PeerFinder_BootstrapCache_Index;";

        session << "ALTER TABLE PeerFinder_BootstrapCache_Next "
                   "  RENAME TO PeerFinder_BootstrapCache;";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Index ON "
                   "PeerFinder_BootstrapCache "
                   "  (  "
                   "    address "
                   "  ); ";
    }

    if (version < 3)
    {
        //
        // Remove legacy endpoints from the schema
        //

        session << "DROP TABLE IF EXISTS LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index;";
    }

    {
        int const v(currentSchemaVersion);
        session << "INSERT OR REPLACE INTO SchemaVersion ("
                   "   name "
                   "  ,version "
                   ") VALUES ( "
                   "  'PeerFinder', :version "
                   ");",
            soci::use(v);
    }

    tr.commit();
}

void
readPeerFinderDB(
    soci::session& session,
    std::function<void(std::string const&, int)> const& func)
{
    std::string s;
    int valence;
    soci::statement st =
        (session.prepare << "SELECT "
                            " address, "
                            " valence "
                            "FROM PeerFinder_BootstrapCache;",
         soci::into(s),
         soci::into(valence));

    st.execute();
    while (st.fetch())
    {
        func(s, valence);
    }
}

void
savePeerFinderDB(
    soci::session& session,
    std::vector<PeerFinder::Store::Entry> const& v)
{
    soci::transaction tr(session);
    session << "DELETE FROM PeerFinder_BootstrapCache;";

    if (!v.empty())
    {
        std::vector<std::string> s;
        std::vector<int> valence;
        s.reserve(v.size());
        valence.reserve(v.size());

        for (auto const& e : v)
        {
            s.emplace_back(to_string(e.endpoint));
            valence.emplace_back(e.valence);
        }

        session << "INSERT INTO PeerFinder_BootstrapCache ( "
                   "  address, "
                   "  valence "
                   ") VALUES ( "
                   "  :s, :valence "
                   ");",
            soci::use(s), soci::use(valence);
    }

    tr.commit();
}

}  // namespace ripple
