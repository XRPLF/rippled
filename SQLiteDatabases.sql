

CREATE TABLE Transactions (  -- trans in all state
	TransID		BLOB PRIMARY KEY,
	NodeHash	BLOB,
	FromName	BLOB,			-- 20 byte hash of pub key
	FromPubKey	BLOB,
	FromSeq		BIGINT UNSIGNED,	-- account seq
	DestName	BLOB,			-- 20 byte hash of pub key
	Ident		BIGINT,
	SourceLedger	BIGINT UNSIGNED,	-- ledger source expected
	Signature	BLOB,
	LedgerCommited	BIGINT UNSIGNED,	-- 0 if none
	Status		VARCHAR(12) NOT NULL
);

CREATE INDEX TransHashSet ON Transactions(LedgerCommited, NodeHash);



CREATE TABLE PubKeys ( -- holds pub keys for nodes and accounts
	Hash		BLOB PRIMARY KEY,
	PubKey		BLOB NOT NULL
);


CREATE TABLE AccountStatus ( -- holds balances and sequence numbers
	AccountName	BLOB,			-- 20 byte hash
	Balance		BIGINT UNSIGNED,
	Seq		BIGINT UNSIGNED,
	FirstLedger	BIGINT UNSIGNED,
	LastLedger	BIGINT UNSIGNED		-- 2^60 if still valid
);

CREATE UNIQUE INDEX CurrentStatus ON AccountStatus(AccountName, LastLedger);


CREATE TABLE Ledgers ( -- closed ledgers
	LedgerHash	BLOB PRIMARY KEY,
	LedgerSeq	BIGINT UNSIGNED,
	PrevHash	BLOB,
	FeeHeld		BIGINT UNSIGNED,
	AccountSetHash	BLOB,
	TransSetHash	BLOB,
	FullyStored	VARCHAR(1),		-- all data in our db
	Status		VARCHAR(1)
);

CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);


CREATE TABLE AccountSetHashNodes (	
	LedgerSeq	BIGINT UNSIGNED,
	NodeID		BLOB,
	Hashes		BLOB			-- 32 hashes, each 20 bytes
);

CREATE UNIQUE INDEX FindAccountHashNodes  ON AccountSetHashNodes(LedgerSeq, NodeID);


CREATE TABLE TransSetHashNodes (
	LedgerSeq	BIGINT UNSIGNED,
	NodeID		BLOB,
	Hashes		BLOB			-- 32 hashes, each 20 bytes
);

CREATE UNIQUE INDEX FindTransHashNodes ON TransSetHashNodes(LedgerSeq, NodeID);


CREATE TABLE LedgerConfirmations (
	LedgerSeq	BIGINT UNSIGNED,
	LedgerHash	BLOB,
	Hanko		BLOB,
	Signature	BLOB
);

CREATE INDEX SeqLedgerConf ON LedgerConfirmations(LedgerSeq);


CREATE TABLE TrustedNodes (
	Hanko		BLOB PRIMARY KEY,
	TrustLevel	SMALLINT,
	Comment		TEXT
);

CREATE TABLE KnownNodes (
	Hanko		BLOB PRIMARY KEY,
	LastSeen	TEXT,			-- YYYY-MM-DD HH:MM:SS.SSS
	LastIP		BLOB,			-- IPv4 or IPv6
	LastPort	BIGINT UNSIGNED,
	ContactObject	BLOB
);

CREATE TABLE ByHash (				-- used to synch nodes
	Hash		BLOB PRIMARY KEY,
	ObjType		CHAR(1) NOT NULL,
	LedgerIndex	BIGINT UNSIGNED,	-- 2^60 if valid now, 0 if none
	Object		BLOB
);

CREATE TABLE LocalAccounts (			-- wallet
	Hash		BLOB PRIMARY KEY,
	CurrentBalance	BIGINT UNSIGNED,
	KeyFormat	TEXT,			-- can be encrypted
	PrivateKey	BLOB
	Comment		TEXT
);

