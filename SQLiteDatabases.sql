

CREATE TABLE Transactions (  -- trans in all state
	Hanko		BLOB PRIMARY KEY,
	NodeHash	BLOB,
	Source		BLOB,
	FromSeq		BIGINT UNSIGNED,
	Dest		BLOB,
	Ident		BIGINT,
	SourceLedger	BIGINT UNSIGNED,
	Signature	BLOB,
	LedgerCommited	BIGINT UNSIGNED,	-- 0 if none
	Status		VARCHAR(12) NOT NULL
);

CREATE INDEX TransactionHashSet -- needed to fetch hash groups
ON Transactions(LedgerCommited, NodeHash);


CREATE TABLE PubKeys ( -- holds pub keys for nodes and accounts
	Hash		BLOB PRIMARY KEY,
	PubKey		BLOB NOT NULL
);


CREATE TABLE AccountStatus ( -- holds balances and sequence numbers
	Row		INTEGER PRIMARY KEY,
	Hanko		BLOB,
	Balance		BIGINT UNSIGNED,
	Sequence	BIGINT UNSIGNED,
	FirstLedger	BIGINT UNSIGNED,
	LastLedger	BIGINT UNSIGNED		-- 2^60 if still valid
);

CREATE TABLE Ledgers ( -- closed ledgers
	Hash		BLOB PRIMARY KEY,
	LedgerSeq	BIGINT UNSIGNED,
	FeeHeld		BIGINT UNSIGNED,
	PrevHash	BLOB,
	AccountHash	BLOB,
	TrasactionHash	BLOB,
	FullyStored	VARCHAR(1),
	Status		VARCHAR(1)
);


CREATE TABLE LedgerHashNodes (
	NodeID		BLOB,
	LedgerSeq	BIGINT UNSIGNED,
	Hashes		BLOB
);

CREATE TABLE TransactionHashNodes (
	NodeID		BLOB,
	LedgerSeq	BIGINT UNSIGNED,
	Hashes		BLOB
);


CREATE TABLE LedgerConfirmations (
	LedgerSeq	BIGINT UNSIGNED,
	LedgerHash	BLOB,
	Hanko		BLOB,
	Signature	BLOB
);

CREATE TABLE TrustedNodes (
	Hanko		BLOB PRIMARY KEY,
	Trust		SMALLINT.
	Comment		TEXT
);

CREATE TABLE KnownNodes (
	Hanko		BLOB PRIMARY KEY,
	LastSeen	TEXT,			-- YYYY-MM-DD HH:MM:SS.SSS
	LastSigned	TEXT,
	LastIP		BLOB,
	LastPort	BIGINT UNSIGNED,
	ContactObject	BLOB
);

CREATE TABLE ByHash (				-- used to synch nodes
	Hash		BLOB PRIMARY KEY,
	Type		VARCHAR(12) NOT NULL,
	LedgerIndex	BIGINT UNSIGNED,	-- 2^60 if valid now, 0 if none
	Object		BLOB
);

CREATE TABLE LocalAccounts (
	Hash		BLOB PRIMARY KEY,
	CurrentBalance	BIGINT UNSIGNED,
	KeyFormat	TEXT,
	PrivateKey	BLOB
);

