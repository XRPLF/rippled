

CREATE TABLE Transactions (			-- transactions in all states
	TransID		BLOB PRIMARY KEY,
	FromID		BLOB,				-- 20 byte hash of pub key
	FromSeq		BIGINT UNSIGNED,	-- account seq
	FromLedger	BIGINT UNSIGNED,
	ToID		BLOB,				-- 20 byte hash of pub key
	FirstSeen	TEXT,				-- time first seen
	CommitSeq	BIGINT UNSIGNED,	-- ledger commited to, 0 if none
	Status		VARCHAR(1)			-- (N)ew, (A)ctive, (C)onflicted, (D)one, (H)eld
);


CREATE TABLE PubKeys ( -- holds pub keys for nodes and accounts
	ID			BLOB PRIMARY KEY,
	PubKey		BLOB NOT NULL
);


CREATE TABLE Ledgers ( -- closed ledgers
	LedgerHash		BLOB PRIMARY KEY,
	LedgerSeq		BIGINT UNSIGNED,
	PrevHash		BLOB,
	FeeHeld			BIGINT UNSIGNED,
	AccountSetHash	BLOB,
	TransSetHash	BLOB,
	FullyStored		VARCHAR(1),		-- all data is in our db
	Status			VARCHAR(1)		-- (A)ccepted, (C)ompatible, (I)ncompatible
);

CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);



CREATE TABLE LedgerConfirmations (
	LedgerSeq	BIGINT UNSIGNED,
	LedgerHash	BLOB,
	Hanko		BLOB,
	Signature	BLOB
);

CREATE INDEX LedgerConfByHash ON LedgerConfirmations(LedgerHash);


CREATE TABLE TrustedNodes (
	Hanko		BLOB PRIMARY KEY,
	TrustLevel	SMALLINT,
	Comment		TEXT
);

CREATE TABLE KnownNodes (
	Hanko			BLOB PRIMARY KEY,
	LastSeen		TEXT,			-- YYYY-MM-DD HH:MM:SS.SSS
	HaveContactInfo	VARCHAR(1),
	ContactObject	BLOB
);


CREATE TABLE CommittedObjects (			-- used to synch nodes
	Hash		BLOB PRIMARY KEY,
	ObjType		CHAR(1) NOT NULL,		-- (L)edger, (T)ransaction, (A)ccount node, transaction (N)ode
	LedgerIndex	BIGINT UNSIGNED,		-- 0 if none
	Object		BLOB
);

CREATE INDEX ObjectLocate ON CommittedObjects(LedgerIndex, ObjType);

CREATE TABLE LocalAccounts (			-- wallet
	ID			BLOB PRIMARY KEY,
	Hash		BLOB,
	Seq			BIGINT UNSIGNED,	-- last transaction seen/issued
	Balance		BIGINT UNSIGNED,
	LedgerSeq	BIGINT UNSIGNED,	-- ledger this balance is from
	KeyFormat	TEXT,				-- can be encrypted
	PrivateKey	BLOB,
	Comment		TEXT
);
