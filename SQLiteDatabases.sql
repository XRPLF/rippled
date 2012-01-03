
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
	Status		CHARACTER(1),		-- (N)ew, (A)ctive, (C)onflicted, (D)one, (H)eld
	Signature	BLOB
);

CREATE TABLE PubKeys ( -- holds pub keys for nodes and accounts
 	ID			CHARACTER(40) PRIMARY KEY,
	PubKey		BLOB
);




CREATE TABLE Ledgers ( -- closed/accepted ledgers
	LedgerHash		CHARACTER(64) PRIMARY KEY,
	LedgerSeq		BIGINT UNSIGNED,
	PrevHash		CHARACTER(64),
	FeeHeld			BIGINT UNSIGNED,
	ClosingTime		BIGINT UNSINGED,
	AccountSetHash	CHARACTER(64),
	TransSetHash	CHARACTER(64)
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

CREATE TABLE LocalAcctFamilies (		-- a family of accounts that share a payphrase
	FamilyName	CHARACTER(40) PRIMARY KEY,
	RootPubKey	CHARACTER(66),
	Seq			BIGINT UNSIGNED,		-- next one to issue
	Name		TEXT,
	Comment		TEXT
);

CREATE TABLE LocalAccounts (		-- an individual account
	ID			CHARACTER(40) PRIMARY KEY,
	KeyType		CHARACTER(1)		-- F=family
 	PrivateKey	TEXT,				-- For F, FamilyName:Seq
	Seq			BIGINT UNSIGNED,	-- last transaction seen/issued
	Balance		BIGINT UNSIGNED,
	LedgerSeq	BIGINT UNSIGNED,	-- ledger this balance is from
	Name		TEXT,
	Comment		TEXT
);
