

CREATE TABLE Transactions (			-- transactions in all states
	TransID		CHARACTER(64) PRIMARY KEY,	-- in hex
	FromAcct	CHARACTER(40),		-- 20 byte hash of pub key in hex
	FromSeq		BIGINT UNSIGNED,	-- account seq
	FromLedger	BIGINT UNSIGNED,
	Identifier	BIGINT UNSIGNED,
	ToAcct		CHARACTER(40),		-- 20 byte hash of pub key
	Amount		BIGINT UNSIGNED,
	Fee			BIGINT UNSIGNED,
	FirstSeen	TEXT,				-- time first seen
	CommitSeq	BIGINT UNSIGNED,	-- ledger commited to, 0 if none
	Status		CHARACTER(1)		-- (N)ew, (A)ctive, (C)onflicted, (D)one, (H)eld
);


CREATE TABLE PubKeys ( -- holds pub keys for nodes and accounts
 	ID			CHARACTER(40) PRIMARY KEY,
	PubKey		CHARCTER(66) NOT NULL
);


CREATE TABLE Ledgers ( -- closed ledgers
	LedgerHash		CHARACTER(64) PRIMARY KEY,
	LedgerSeq		BIGINT UNSIGNED,
	PrevHash		CHARACTER(64),
	FeeHeld			BIGINT UNSIGNED,
	AccountSetHash	CHARACTER(64),
	TransSetHash	CHARACTER(64),
	FullyStored		CHARACTER(1),		-- all data is in our db
	Status			CHARACTER(1)		-- (A)ccepted, (C)ompatible, (I)ncompatible
);

CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);



CREATE TABLE LedgerConfirmations (
	LedgerSeq	BIGINT UNSIGNED,
	LedgerHash	CHARACTER(64),
	Hanko		CHARACTER(40),
	Signature	BLOB
);

CREATE INDEX LedgerConfByHash ON LedgerConfirmations(LedgerHash);


CREATE TABLE TrustedNodes (
	Hanko		CHARACTER(40) PRIMARY KEY,
	TrustLevel	SMALLINT,
	Comment		TEXT
);

CREATE TABLE KnownNodes (
	Hanko			CHARACTER(40) PRIMARY KEY,
	LastSeen		TEXT,			-- YYYY-MM-DD HH:MM:SS.SSS
	HaveContactInfo	CHARACTER(1),
	ContactObject	BLOB
);


CREATE TABLE CommittedObjects (			-- used to synch nodes
	Hash		CHARACTER(64) PRIMARY KEY,
	ObjType		CHAR(1) NOT NULL,		-- (L)edger, (T)ransaction, (A)ccount node, transaction (N)ode
	LedgerIndex	BIGINT UNSIGNED,		-- 0 if none
	Object		BLOB
);

CREATE INDEX ObjectLocate ON CommittedObjects(LedgerIndex, ObjType);

CREATE TABLE LocalAccounts (			-- wallet
	ID			CHARACTER(40) PRIMARY KEY,
	Seq			BIGINT UNSIGNED,	-- last transaction seen/issued
	Balance		BIGINT UNSIGNED,
	LedgerSeq	BIGINT UNSIGNED,	-- ledger this balance is from
	KeyFormat	CHARACTER(1),			-- can be encrypted
	PrivateKey	BLOB,
	Comment		TEXT
);
