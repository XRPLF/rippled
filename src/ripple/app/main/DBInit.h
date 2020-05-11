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

#ifndef RIPPLE_APP_DATA_DBINIT_H_INCLUDED
#define RIPPLE_APP_DATA_DBINIT_H_INCLUDED

#include <array>

namespace ripple {

////////////////////////////////////////////////////////////////////////////////

// These pragmas are built at startup and applied to all database
// connections, unless otherwise noted.
inline constexpr char const* CommonDBPragmaJournal{"PRAGMA journal_mode=%s;"};
inline constexpr char const* CommonDBPragmaSync{"PRAGMA synchronous=%s;"};
inline constexpr char const* CommonDBPragmaTemp{"PRAGMA temp_store=%s;"};
// Default values will always be used for the common pragmas if
// at least this much ledger history is configured. This includes
// full history nodes. This is because such a large amount of data will
// be more difficult to recover if a rare failure occurs, which are
// more likely with some of the other available tuning settings.
inline constexpr std::uint32_t SQLITE_TUNING_CUTOFF = 100'000'000;

// Ledger database holds ledgers and ledger confirmations
inline constexpr auto LgrDBName{"ledger.db"};

inline constexpr std::array<char const*, 1> LgrDBPragma{
    {"PRAGMA journal_size_limit=1582080;"}};

inline constexpr std::array<char const*, 5> LgrDBInit{
    {"BEGIN TRANSACTION;",

     "CREATE TABLE IF NOT EXISTS Ledgers (           \
        LedgerHash      CHARACTER(64) PRIMARY KEY,  \
        LedgerSeq       BIGINT UNSIGNED,            \
        PrevHash        CHARACTER(64),              \
        TotalCoins      BIGINT UNSIGNED,            \
        ClosingTime     BIGINT UNSIGNED,            \
        PrevClosingTime BIGINT UNSIGNED,            \
        CloseTimeRes    BIGINT UNSIGNED,            \
        CloseFlags      BIGINT UNSIGNED,            \
        AccountSetHash  CHARACTER(64),              \
        TransSetHash    CHARACTER(64)               \
    );",
     "CREATE INDEX IF NOT EXISTS SeqLedger ON Ledgers(LedgerSeq);",

     // Old table and indexes no longer needed
     "DROP TABLE IF EXISTS Validations;",

     "END TRANSACTION;"}};

////////////////////////////////////////////////////////////////////////////////

// Transaction database holds transactions and public keys
inline constexpr auto TxDBName{"transaction.db"};

inline constexpr
#if (ULONG_MAX > UINT_MAX) && !defined(NO_SQLITE_MMAP)
    std::array<char const*, 4>
        TxDBPragma
{
    {
#else
    std::array<char const*, 3> TxDBPragma {{
#endif
        "PRAGMA page_size=4096;", "PRAGMA journal_size_limit=1582080;",
            "PRAGMA max_page_count=2147483646;",
#if (ULONG_MAX > UINT_MAX) && !defined(NO_SQLITE_MMAP)
            "PRAGMA mmap_size=17179869184;"
#endif
    }
};

inline constexpr std::array<char const*, 8> TxDBInit{
    {"BEGIN TRANSACTION;",

     "CREATE TABLE IF NOT EXISTS Transactions (          \
        TransID     CHARACTER(64) PRIMARY KEY,          \
        TransType   CHARACTER(24),                      \
        FromAcct    CHARACTER(35),                      \
        FromSeq     BIGINT UNSIGNED,                    \
        LedgerSeq   BIGINT UNSIGNED,                    \
        Status      CHARACTER(1),                       \
        RawTxn      BLOB,                               \
        TxnMeta     BLOB                                \
    );",
     "CREATE INDEX IF NOT EXISTS TxLgrIndex ON           \
        Transactions(LedgerSeq);",

     "CREATE TABLE IF NOT EXISTS AccountTransactions (   \
        TransID     CHARACTER(64),                      \
        Account     CHARACTER(64),                      \
        LedgerSeq   BIGINT UNSIGNED,                    \
        TxnSeq      INTEGER                             \
    );",
     "CREATE INDEX IF NOT EXISTS AcctTxIDIndex ON        \
        AccountTransactions(TransID);",
     "CREATE INDEX IF NOT EXISTS AcctTxIndex ON          \
        AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);",
     "CREATE INDEX IF NOT EXISTS AcctLgrIndex ON         \
        AccountTransactions(LedgerSeq, Account, TransID);",

     "END TRANSACTION;"}};

////////////////////////////////////////////////////////////////////////////////

// Temporary database used with an incomplete shard that is being acquired
inline constexpr auto AcquireShardDBName{"acquire.db"};

inline constexpr std::array<char const*, 1> AcquireShardDBPragma{
    {"PRAGMA journal_size_limit=1582080;"}};

inline constexpr std::array<char const*, 1> AcquireShardDBInit{
    {"CREATE TABLE IF NOT EXISTS Shard (             \
        ShardIndex          INTEGER PRIMARY KEY,    \
        LastLedgerHash      CHARACTER(64),          \
        StoredLedgerSeqs    BLOB                    \
    );"}};

////////////////////////////////////////////////////////////////////////////////

// Pragma for Ledger and Transaction databases with complete shards
// These override the CommonDBPragma values defined above.
inline constexpr std::array<char const*, 2> CompleteShardDBPragma{
    {"PRAGMA synchronous=OFF;", "PRAGMA journal_mode=OFF;"}};

////////////////////////////////////////////////////////////////////////////////

inline constexpr auto WalletDBName{"wallet.db"};

inline constexpr std::array<char const*, 6> WalletDBInit{
    {"BEGIN TRANSACTION;",

     // A node's identity must be persisted, including
     // for clustering purposes. This table holds one
     // entry: the server's unique identity, but the
     // value can be overriden by specifying a node
     // identity in the config file using a [node_seed]
     // entry.
     "CREATE TABLE IF NOT EXISTS NodeIdentity (			\
        PublicKey       CHARACTER(53),					\
        PrivateKey      CHARACTER(52)					\
    );",

     // Peer reservations
     "CREATE TABLE IF NOT EXISTS PeerReservations (		\
        PublicKey       CHARACTER(53) UNIQUE NOT NULL,	\
        Description     CHARACTER(64) NOT NULL			\
    );",

     // Validator Manifests
     "CREATE TABLE IF NOT EXISTS ValidatorManifests (	\
        RawData          BLOB NOT NULL					\
    );",

     "CREATE TABLE IF NOT EXISTS PublisherManifests (	\
        RawData          BLOB NOT NULL					\
    );",

     "END TRANSACTION;"}};

////////////////////////////////////////////////////////////////////////////////

static constexpr auto stateDBName{"state.db"};

// These override the CommonDBPragma values defined above.
static constexpr std::array<char const*, 2> DownloaderDBPragma{
    {"PRAGMA synchronous=FULL;", "PRAGMA journal_mode=DELETE;"}};

static constexpr std::array<char const*, 3> ShardArchiveHandlerDBInit{
    {"BEGIN TRANSACTION;",

     "CREATE TABLE IF NOT EXISTS State (     \
         ShardIndex  INTEGER PRIMARY KEY,   \
         URL         TEXT                   \
     );",

     "END TRANSACTION;"}};

static constexpr std::array<char const*, 3> DatabaseBodyDBInit{
    {"BEGIN TRANSACTION;",

     "CREATE TABLE IF NOT EXISTS download (      \
        Path        TEXT,                       \
        Data        BLOB,                       \
        Size        BIGINT UNSIGNED,            \
        Part        BIGINT UNSIGNED PRIMARY KEY \
    );",

     "END TRANSACTION;"}};

}  // namespace ripple

#endif
