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

#include <ripple/app/main/DBInit.h>
#include <type_traits>

namespace ripple {

// Transaction database holds transactions and public keys
const char* TxnDBName = "transaction.db";
const char* TxnDBInit[] =
{
    "PRAGMA page_size=4096;",
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA journal_mode=WAL;",
    "PRAGMA journal_size_limit=1582080;",
    "PRAGMA max_page_count=2147483646;",

#if (ULONG_MAX > UINT_MAX) && !defined (NO_SQLITE_MMAP)
    "PRAGMA mmap_size=17179869184;",
#endif

    "BEGIN TRANSACTION;",

    "CREATE TABLE IF NOT EXISTS Transactions (                \
        TransID     CHARACTER(64) PRIMARY KEY,  \
        TransType   CHARACTER(24),              \
        FromAcct    CHARACTER(35),              \
        FromSeq     BIGINT UNSIGNED,            \
        LedgerSeq   BIGINT UNSIGNED,            \
        Status      CHARACTER(1),               \
        RawTxn      BLOB,                       \
        TxnMeta     BLOB                        \
    );",
    "CREATE INDEX IF NOT EXISTS TxLgrIndex ON                 \
        Transactions(LedgerSeq);",

    "CREATE TABLE IF NOT EXISTS AccountTransactions (         \
        TransID     CHARACTER(64),              \
        Account     CHARACTER(64),              \
        LedgerSeq   BIGINT UNSIGNED,            \
        TxnSeq      INTEGER                     \
    );",
    "CREATE INDEX IF NOT EXISTS AcctTxIDIndex ON              \
        AccountTransactions(TransID);",
    "CREATE INDEX IF NOT EXISTS AcctTxIndex ON                \
        AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);",
    "CREATE INDEX IF NOT EXISTS AcctLgrIndex ON               \
        AccountTransactions(LedgerSeq, Account, TransID);",

    "END TRANSACTION;"
};

int TxnDBCount = std::extent<decltype(TxnDBInit)>::value;

// Ledger database holds ledgers and ledger confirmations
const char* LedgerDBInit[] =
{
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA journal_mode=WAL;",
    "PRAGMA journal_size_limit=1582080;",

    "BEGIN TRANSACTION;",

    "CREATE TABLE IF NOT EXISTS Ledgers (                         \
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

    // InitialSeq field is the current ledger seq when the row
    // is inserted. Only relevant during online delete
    "CREATE TABLE IF NOT EXISTS Validations   (                   \
        LedgerSeq   BIGINT UNSIGNED,                \
        InitialSeq  BIGINT UNSIGNED,                \
        LedgerHash  CHARACTER(64),                  \
        NodePubKey  CHARACTER(56),                  \
        SignTime    BIGINT UNSIGNED,                \
        RawData     BLOB                            \
    );",
    "CREATE INDEX IF NOT EXISTS ValidationsByHash ON              \
        Validations(LedgerHash);",
    "CREATE INDEX IF NOT EXISTS ValidationsBySeq ON              \
        Validations(LedgerSeq);",
    "CREATE INDEX IF NOT EXISTS ValidationsByInitialSeq ON              \
        Validations(InitialSeq, LedgerSeq);",
    "CREATE INDEX IF NOT EXISTS ValidationsByTime ON              \
        Validations(SignTime);",

    "END TRANSACTION;"
};

int LedgerDBCount = std::extent<decltype(LedgerDBInit)>::value;

const char* WalletDBInit[] =
{
    "BEGIN TRANSACTION;",

    // A node's identity must be persisted, including
    // for clustering purposes. This table holds one
    // entry: the server's unique identity, but the
    // value can be overriden by specifying a node
    // identity in the config file using a [node_seed]
    // entry.
    "CREATE TABLE IF NOT EXISTS NodeIdentity (      \
        PublicKey       CHARACTER(53),              \
        PrivateKey      CHARACTER(52)               \
    );",

    // Peer reservations.
    // REVIEWER: How do we handle table migrations if we need to add a column?
    "CREATE TABLE IF NOT EXISTS PeerReservations (   \
        PublicKey       CHARACTER(53) NOT NULL,      \
        Name            CHARACTER(32) NULL,          \
    );",

    // Validator Manifests
    "CREATE TABLE IF NOT EXISTS ValidatorManifests ( \
        RawData          BLOB NOT NULL               \
    );",

    "CREATE TABLE IF NOT EXISTS PublisherManifests ( \
        RawData          BLOB NOT NULL               \
    );",

    // Old tables that were present in wallet.db and we
    // no longer need or use.
    "DROP INDEX IF EXISTS SeedNodeNext;",
    "DROP INDEX IF EXISTS SeedDomainNext;",
    "DROP TABLE IF EXISTS Features;",
    "DROP TABLE IF EXISTS TrustedNodes;",
    "DROP TABLE IF EXISTS ValidatorReferrals;",
    "DROP TABLE IF EXISTS IpReferrals;",
    "DROP TABLE IF EXISTS SeedNodes;",
    "DROP TABLE IF EXISTS SeedDomains;",
    "DROP TABLE IF EXISTS Misc;",

    "END TRANSACTION;"
};

int WalletDBCount = std::extent<decltype(WalletDBInit)>::value;

} // ripple
