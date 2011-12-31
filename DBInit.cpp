#include <string>

// Transaction database holds transactions and public keys
std::string TxnDBInit("							\
	CREATE TABLE Transactions (					\
		TransID		CHARACTER(64) PRIMARY KEY,	\
		FromAcct	CHARACTER(40),				\
		FromSeq		BIGINT UNSIGNED,			\
		FromLedger	BIGINT UNSIGNED,			\
		Identifier	BIGINT UNSIGNED,			\
		ToAcct		CHARACTER(40),				\
		Amount		BIGINT UNSIGNED,			\
		Fee			BIGINT UNSIGNED,			\
		FirstSeen	TEXT,						\
		CommitSeq	BIGINT UNSIGNED,			\
		Status		CHARACTER(1),				\
		Signature	BLOB						\
	);											\
												\
	CREATE TABLE PubKeys (						\
		ID			CHARACTER(40) PRIMARY KEY,	\
		PubKey		BLOB						\
	);											\
");

// Ledger database holds ledgers and ledger confirmations
std::string LedgerDBInit("							\
	CREATE TABLE Ledgers (							\
		LedgerHash		CHARACTER(64) PRIMARY KEY,	\
		LedgerSeq		BIGINT UNSIGNED,			\
		PrevHash		CHARACTER(64),				\
		FeeHeld			BIGINT UNSIGNED,			\
		ClosingTime		BIGINT UNSINGED,			\
		AccountSetHash	CHARACTER(64),				\
		TransSetHash	CHARACTER(64)				\
	);												\
	CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);	\
													\
	CREATE TABLE LedgerConfirmations	(			\
		LedgerSeq	BIGINT UNSIGNED,				\
		LedgerHash	CHARACTER(64),					\
		Hanko		CHARACTER(40),					\
		Signature	BLOB							\
	);												\
	CREATE INDEX LedgerConfByHash ON				\
		LedgerConfirmations(LedgerHash);			\
");


// Wallet database holds local accounts and trusted nodes
std::string WalletDBInit("							\
	CREATE TABLE LocalAcctFamilies (				\
		FamilyName	CHARACTER(40) PRIMARY KEY,		\
		RootPubKey	CHARACTER(66),					\
		Seq			BIGINT UNSIGNED,				\
		Name		TEXT,							\
		Comment		TEXT							\
	);												\
													\
	CREATE TABLE LocalAccounts (					\
		ID			CHARACTER(40) PRIMARY KEY,		\
		DKID		CHARACTER(40),					\
		DKSeq		BIGINT UNSIGNED,				\
		Seq			BIGINT UNSIGNED,				\
		Balance		BIGINT UNSIGNED,				\
		LedgerSeq	BIGINT UNSIGNED,				\
		Comment		TEXT							\
	);												\
													\
	CREATE TABLE TrustedNodes (					`	\
		Hanko		CHARACTER(40) PRIMARY KEY,		\
		TrustLevel	SMALLINT,						\
		Comment		TEXT							\
	);												\
");

// Hash node database holds nodes indexed by hash
std::string HashNodeDBInit("						\
	CREATE TABLE CommittedObjects (					\
		Hash		CHARACTER(64) PRIMARY KEY,		\
		ObjType		CHAR(1)	NOT	NULL,				\
		LedgerIndex	BIGINT UNSIGNED,				\
		Object		BLOB							\
	);												\
	CREATE INDEX ObjectLocate ON					\
		CommittedObjects(LedgerIndex, ObjType);		\
");

// Net node database holds nodes seen on the network
std::string NetNodeDBInit("							\
	CREATE TABLE KnownNodes	(						\
		Hanko			CHARACTER(40) PRIMARY KEY,	\
		LastSeen		TEXT,						\
		HaveContactInfo	CHARACTER(1),				\
		ContactObject	BLOB						\
		);											\
");
