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

namespace ripple {

// Transaction database holds transactions and public keys
const char* TxnDBInit[] =
{
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA journal_mode=WAL;",
    "PRAGMA journal_size_limit=1582080;",

#if (ULONG_MAX > UINT_MAX) && !defined (NO_SQLITE_MMAP)
	"PRAGMA mmap_size=17179869184;",
#endif

    "BEGIN TRANSACTION;",

    "CREATE TABLE Transactions (				\
		TransID		CHARACTER(64) PRIMARY KEY,	\
		TransType	CHARACTER(24),				\
		FromAcct	CHARACTER(35),				\
		FromSeq		BIGINT UNSIGNED,			\
		LedgerSeq	BIGINT UNSIGNED,			\
		Status		CHARACTER(1),				\
		RawTxn		BLOB,						\
		TxnMeta		BLOB						\
	);",
    "CREATE INDEX TxLgrIndex ON                 \
		Transactions(LedgerSeq);",

    "CREATE TABLE AccountTransactions (			\
		TransID		CHARACTER(64),				\
		Account		CHARACTER(64),				\
		LedgerSeq	BIGINT UNSIGNED,			\
		TxnSeq		INTEGER						\
	);",
    "CREATE INDEX AcctTxIDIndex ON				\
		AccountTransactions(TransID);",
    "CREATE INDEX AcctTxIndex ON				\
		AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);",
    "CREATE INDEX AcctLgrIndex ON				\
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

    "CREATE TABLE Ledgers (							\
		LedgerHash		CHARACTER(64) PRIMARY KEY,	\
		LedgerSeq		BIGINT UNSIGNED,			\
		PrevHash		CHARACTER(64),				\
		TotalCoins		BIGINT UNSIGNED,			\
		ClosingTime		BIGINT UNSIGNED,			\
		PrevClosingTime	BIGINT UNSIGNED,			\
		CloseTimeRes	BIGINT UNSIGNED,			\
		CloseFlags		BIGINT UNSIGNED,			\
		AccountSetHash	CHARACTER(64),				\
		TransSetHash	CHARACTER(64)				\
	);",
    "CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);",

    "CREATE TABLE Validations	(					\
		LedgerHash	CHARACTER(64),					\
		NodePubKey	CHARACTER(56),					\
		SignTime	BIGINT UNSIGNED,				\
		RawData		BLOB							\
	);",
    "CREATE INDEX ValidationsByHash ON				\
		Validations(LedgerHash);",
    "CREATE INDEX ValidationsByTime ON				\
		Validations(SignTime);",

    "END TRANSACTION;"
};

int LedgerDBCount = std::extent<decltype(LedgerDBInit)>::value;

// RPC database holds persistent data for RPC clients.
const char* RpcDBInit[] =
{

    // Local persistence of the RPC client
    "CREATE TABLE RPCData (							\
		Key			TEXT PRIMARY Key,				\
		Value		TEXT							\
	);",
};

int RpcDBCount = std::extent<decltype(RpcDBInit)>::value;

// NodeIdentity database holds local accounts and trusted nodes
// VFALCO NOTE but its a table not a database, so...?
//
const char* WalletDBInit[] =
{
    // Node identity must be persisted for CAS routing and responsibilities.
    "BEGIN TRANSACTION;",

    "CREATE TABLE NodeIdentity (					\
		PublicKey		CHARACTER(53),				\
		PrivateKey		CHARACTER(52),				\
		Dh512			TEXT,						\
		Dh1024			TEXT						\
	);",

    // Miscellaneous persistent information
    // Integer: 1 : Used to simplify SQL.
    // ScoreUpdated: when scores was last updated.
    // FetchUpdated: when last fetch succeeded.
    "CREATE TABLE Misc (							\
		Magic			INTEGER UNIQUE NOT NULL,	\
		ScoreUpdated	DATETIME,					\
		FetchUpdated	DATETIME					\
	);",

    // Scoring and other information for domains.
    //
    // Domain:
    //  Domain source for https.
    // PublicKey:
    //  Set if ever succeeded.
    // XXX Use NULL in place of ""
    // Source:
    //  'M' = Manually added.   : 1500
    //  'V' = validators.txt    : 1000
    //  'W' = Web browsing.     :  200
    //  'R' = Referral          :    0
    // Next:
    //  Time of next fetch attempt.
    // Scan:
    //  Time of last fetch attempt.
    // Fetch:
    //  Time of last successful fetch.
    // Sha256:
    //  Checksum of last fetch.
    // Comment:
    //  User supplied comment.
    // Table of Domains user has asked to trust.
    "CREATE TABLE SeedDomains (						\
		Domain			TEXT PRIMARY KEY NOT NULL,	\
		PublicKey		CHARACTER(53),				\
		Source			CHARACTER(1) NOT NULL,		\
		Next			DATETIME,					\
		Scan			DATETIME,					\
		Fetch			DATETIME,					\
		Sha256			CHARACTER[64],				\
		Comment			TEXT						\
	);",

    // Allow us to easily find the next SeedDomain to fetch.
    "CREATE INDEX SeedDomainNext ON SeedDomains (Next);",

    // Table of PublicKeys user has asked to trust.
    // Fetches are made to the CAS.  This gets the ripple.txt so even validators
    // without a web server can publish a ripple.txt.
    // Source:
    //  'M' = Manually added.   : 1500
    //  'V' = validators.txt    : 1000
    //  'W' = Web browsing.     :  200
    //  'R' = Referral          :    0
    // Next:
    //  Time of next fetch attempt.
    // Scan:
    //  Time of last fetch attempt.
    // Fetch:
    //  Time of last successful fetch.
    // Sha256:
    //  Checksum of last fetch.
    // Comment:
    //  User supplied comment.
    "CREATE TABLE SeedNodes (						\
		PublicKey		CHARACTER(53) PRIMARY KEY NOT NULL,		\
		Source			CHARACTER(1) NOT NULL,		\
		Next			DATETIME,					\
		Scan			DATETIME,					\
		Fetch			DATETIME,					\
		Sha256			CHARACTER[64],				\
		Comment			TEXT						\
	);",

    // Allow us to easily find the next SeedNode to fetch.
    "CREATE INDEX SeedNodeNext ON SeedNodes (Next);",

    // Nodes we trust to not grossly collude against us.  Derived from
    // SeedDomains, SeedNodes, and ValidatorReferrals.
    //
    // Score:
    //  Computed trust score.  Higher is better.
    // Seen:
    //  Last validation received.
    "CREATE TABLE TrustedNodes (							\
		PublicKey		CHARACTER(53) PRIMARY KEY NOT NULL,	\
		Score			INTEGER DEFAULT 0 NOT NULL,			\
		Seen			DATETIME,							\
		Comment			TEXT								\
	);",

    // List of referrals.
    // - There may be multiple sources for a Validator. The last source is used.
    // Validator:
    //  Public key of referrer.
    // Entry:
    //  Entry index in [validators] table.
    // Referral:
    //  This is the form provided by the ripple.txt:
    //  - Public key for CAS based referral.
    //  - Domain for domain based referral.
    // XXX Do garbage collection when validators have no references.
    "CREATE TABLE ValidatorReferrals (				\
		Validator		CHARACTER(53) NOT NULL,		\
		Entry			INTEGER NOT NULL,			\
		Referral		TEXT NOT NULL,				\
		PRIMARY KEY (Validator,Entry)				\
	);",

    // List of referrals from ripple.txt files.
    // Validator:
    //  Public key of referree.
    // Entry:
    //  Entry index in [validators] table.
    // IP:
    //  IP of referred.
    // Port:
    //  -1 = Default
    // XXX Do garbage collection when ips have no references.
    "CREATE TABLE IpReferrals (							\
		Validator		CHARACTER(53) NOT NULL,			\
		Entry			INTEGER NOT NULL,				\
		IP				TEXT NOT NULL,					\
		Port			INTEGER NOT NULL DEFAULT -1,	\
		PRIMARY KEY (Validator,Entry)					\
	);",

    "CREATE TABLE Features (							\
		Hash			CHARACTER(64) PRIMARY KEY,		\
		FirstMajority	BIGINT UNSIGNED,				\
		LastMajority	BIGINT UNSIGNED					\
	);",

    // This removes an old table and its index which are now redundant. This
    // code will eventually go away. It's only here to clean up the wallet.db
    "DROP TABLE IF EXISTS PeerIps;",
    "DROP INDEX IF EXISTS;",


    "END TRANSACTION;"
};

int WalletDBCount = std::extent<decltype(WalletDBInit)>::value;

// Hash node database holds nodes indexed by hash
// VFALCO TODO Remove this since it looks unused
/*

int HashNodeDBCount = std::extent<decltype(HashNodeDBInit)>::value;
*/

// Net node database holds nodes seen on the network
// XXX Not really used needs replacement.
/*
const char* NetNodeDBInit[] =
{
    "CREATE TABLE KnownNodes	(					\
		Hanko			CHARACTER(35) PRIMARY KEY,	\
		LastSeen		TEXT,						\
		HaveContactInfo	CHARACTER(1),				\
		ContactObject	BLOB						\
	);"
};

int NetNodeDBCount = std::extent<decltype(NetNodeDBInit)>::value;
*/

// This appears to be unused
/*
const char* PathFindDBInit[] =
{
    "PRAGMA synchronous = OFF;            ",

    "DROP TABLE TrustLines;               ",

    "CREATE TABLE TrustLines {            "
    "To				CHARACTER(40),    "  // Hex of account trusted
    "By				CHARACTER(40),    " // Hex of account trusting
    "Currency		CHARACTER(80),    " // Hex currency, hex issuer
    "Use			INTEGER,          " // Use count
    "Seq			BIGINT UNSIGNED   " // Sequence when use count was updated
    "};                                   ",

    "CREATE INDEX TLBy ON TrustLines(By, Currency, Use);",
    "CREATE INDEX TLTo ON TrustLines(To, Currency, Use);",

    "DROP TABLE Exchanges;",

    "CREATE TABLE Exchanges {             "
    "From			CHARACTER(80),    "
    "To				CHARACTER(80),    "
    "Currency       CHARACTER(80),    "
    "Use            INTEGER,          "
    "Seq            BIGINT UNSIGNED   "
    "};                                   ",

    "CREATE INDEX ExBy ON Exchanges(By, Currency, Use);",
    "CREATE INDEX ExTo ON Exchanges(To, Currency, Use);",
};

int PathFindDBCount = std::extent<decltype(PathFindDBInit)>::value;
*/

} // ripple
