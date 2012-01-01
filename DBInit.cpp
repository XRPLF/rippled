#include <string>

// Transaction database holds transactions and public keys
const char *TxnDBInit[] = {
	"CREATE TABLE Transactions (				\
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
	);",
	"CREATE TABLE PubKeys (						\
		ID			CHARACTER(40) PRIMARY KEY,	\
		PubKey		BLOB						\
	);" };

int TxnDBCount=sizeof(TxnDBInit)/sizeof(const char *);

// Ledger database holds ledgers and ledger confirmations
const char *LedgerDBInit[] = {
	"CREATE TABLE Ledgers (							\
		LedgerHash		CHARACTER(64) PRIMARY KEY,	\
		LedgerSeq		BIGINT UNSIGNED,			\
		PrevHash		CHARACTER(64),				\
		FeeHeld			BIGINT UNSIGNED,			\
		ClosingTime		BIGINT UNSINGED,			\
		AccountSetHash	CHARACTER(64),				\
		TransSetHash	CHARACTER(64)				\
	);",
	"CREATE INDEX SeqLedger ON Ledgers(LedgerSeq);",
	"CREATE TABLE LedgerConfirmations	(			\
		LedgerSeq	BIGINT UNSIGNED,				\
		LedgerHash	CHARACTER(64),					\
		Hanko		CHARACTER(40),					\
		Signature	BLOB							\
	);",
	"CREATE INDEX LedgerConfByHash ON				\
		LedgerConfirmations(LedgerHash)l" };

int LedgerDBCount=sizeof(LedgerDBInit)/sizeof(const char *);

// Wallet database holds local accounts and trusted nodes
const char *WalletDBInit[] = {
	"CREATE TABLE LocalAcctFamilies (				\
		FamilyName	CHARACTER(40) PRIMARY KEY,		\
		RootPubKey	CHARACTER(66),					\
		Seq			BIGINT UNSIGNED,				\
		Name		TEXT,							\
		Comment		TEXT							\
	);",
	"CREATE TABLE LocalAccounts (					\
		ID			CHARACTER(40) PRIMARY KEY,		\
		DKID		CHARACTER(40),					\
		DKSeq		BIGINT UNSIGNED,				\
		Seq			BIGINT UNSIGNED,				\
		Balance		BIGINT UNSIGNED,				\
		LedgerSeq	BIGINT UNSIGNED,				\
		Comment		TEXT							\
	);",
	"CREATE TABLE TrustedNodes (				`	\
		Hanko		CHARACTER(40) PRIMARY KEY,		\
		TrustLevel	SMALLINT,						\
		Comment		TEXT							\
	);" };

int WalletDBCount=sizeof(WalletDBInit)/sizeof(const char *);


// Hash node database holds nodes indexed by hash
const char *HashNodeDBInit[] = {
	"CREATE TABLE CommittedObjects 					\
		Hash		CHARACTER(64) PRIMARY KEY,		\
		ObjType		CHAR(1)	NOT	NULL,				\
		LedgerIndex	BIGINT UNSIGNED,				\
		Object		BLOB							\
	);",
	"CREATE INDEX ObjectLocate ON					\
		CommittedObjects(LedgerIndex, ObjType);" };

int HashNodeDBCount=sizeof(HashNodeDBInit)/sizeof(const char *);

// Net node database holds nodes seen on the network
const char *NetNodeDBInit[] = {
	"CREATE TABLE KnownNodes	(						\
		Hanko			CHARACTER(40) PRIMARY KEY,	\
		LastSeen		TEXT,						\
		HaveContactInfo	CHARACTER(1),				\
		ContactObject	BLOB						\
		);" };


int NetNodeDBCount=sizeof(NetNodeDBInit)/sizeof(const char *);
