#include <string>

// Transaction database holds transactions and public keys
const char *TxnDBInit[] = {
	"CREATE TABLE Transactions (				\
		TransID		CHARACTER(64) PRIMARY KEY,	\
		TransType	CHARACTER(24)				\
		FromAcct	CHARACTER(35),				\
		FromSeq		BIGINT UNSIGNED,			\
		OtherAcct	CHARACTER(40),				\
		Amount		BIGINT UNSIGNED,			\
		FirstSeen	TEXT,						\
		CommitSeq	BIGINT UNSIGNED,			\
		Status		CHARACTER(1),				\
		RawTxn		BLOB						\
	);",
	"CREATE TABLE PubKeys (						\
		ID			CHARACTER(35) PRIMARY KEY,	\
		PubKey		BLOB						\
	);"
};

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
#if 0
	"CREATE TABLE LedgerConfirmations	(			\
		LedgerSeq	BIGINT UNSIGNED,				\
		LedgerHash	CHARACTER(64),					\
		Hanko		CHARACTER(35),					\
		Signature	BLOB							\
	);",
	"CREATE INDEX LedgerConfByHash ON				\
		LedgerConfirmations(LedgerHash)"
#endif
};

int LedgerDBCount=sizeof(LedgerDBInit)/sizeof(const char *);

// Wallet database holds local accounts and trusted nodes
const char *WalletDBInit[] = {
	"CREATE TABLE LocalAcctFamilies (				\
		FamilyGenerator	CHARACTER(53) PRIMARY KEY,	\
		Seq				BIGINT UNSIGNED,			\
		Comment			TEXT						\
	);",

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

	// Domain:
	//  Domain source for https.
	// PublicKey:
	//  Set if ever succeeded.
	// XXX Use NULL in place of ""
	// Source:
	//  'M' = Manually added.	: 1500
	//  'V' = validators.txt	: 1000
	//  'W' = Web browsing.		:  200
	//  'R' = Referral			:    0
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
	// Fetches are made to the CAS.
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
		Next			DATETIME,					\
		Scan			DATETIME,					\
		Fetch			DATETIME,					\
		Sha256			CHARACTER[64],				\
		Comment			TEXT						\
	);",

	// Allow us to easily find the next SeedNode to fetch.
	"CREATE INDEX SeedNodeNext ON SeedNodes (Next);",

	// Table of trusted nodes.
	// Score:
	//  Computed trust score.  Higher is better.
	// Seen:
	//  Last validation received.
	"CREATE TABLE TrustedNodes (							\
		PublicKey		CHARACTER(53) PRIMARY KEY NOT NULL,	\
		Score			INTEGER DEFAULT 0 NOT NULL,			\
		Seen			DATETIME							\
	);",

	// List of referrals.
	// - There may be multiple sources for a Validator.  The last source is used.
	// Validator:
	//  Public key of referrer.
	// Entry:
	//  Entry index in [validators] table.
	// Referral:
	//  This is the form provided by the newcoin.txt:
	//  - Public key for CAS based referral.
	//  - Domain for domain based referral.
	// XXX Do garbage collection when validators have no references.
	"CREATE TABLE ValidatorReferrals (				\
		Validator		CHARACTER(53) NOT NULL,		\
		Entry			INTEGER NOT NULL,			\
		Referral		TEXT NOT NULL,				\
		PRIMARY KEY (Validator,Entry)				\
	);",

	// List of referrals from newcoin.txt files.
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

	// Table of IPs to contact the nextwork.
	// IP:
	//  IP address to contact.
	// Port:
	//  Port to contact.
	//  -1 = Default
	// Score:
	//  Computed trust score.  Higher is better.
	// Source:
	//  'V' = Validation file
	//  'M' = Manually added.
	//  'I' = Inbound connection.
	//  'O' = Other.
	// Contact:
	//  Time of last contact.
	//  XXX Update on connect and hourly.
	"CREATE TABLE PeerIps (								\
		IP				TEXT NOT NULL,					\
		Port			INTEGER NOT NULL DEFAULT -1,	\
		Score			INTEGER NOT NULL,				\
		Source			CHARACTER(1) NOT NULL,			\
		Contact			DATETIME,						\
		PRIMARY KEY (IP,PORT)							\
	);",
};

#if 0
	"CREATE TABLE LocalAccounts (					\
		ID			CHARACTER(35) PRIMARY KEY,		\
		PrivateKey	TEXT							\
		Seq			BIGINT UNSIGNED,				\
		Balance		BIGINT UNSIGNED,				\
		LedgerSeq	BIGINT UNSIGNED,				\
		Comment		TEXT							\
	);",
#endif

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
		CommittedObjects(LedgerIndex, ObjType);"
};

int HashNodeDBCount=sizeof(HashNodeDBInit)/sizeof(const char *);

// Net node database holds nodes seen on the network
// XXX Not really used needs replacement.
const char *NetNodeDBInit[] = {
	"CREATE TABLE KnownNodes	(					\
		Hanko			CHARACTER(35) PRIMARY KEY,	\
		LastSeen		TEXT,						\
		HaveContactInfo	CHARACTER(1),				\
		ContactObject	BLOB						\
	);"
};


int NetNodeDBCount=sizeof(NetNodeDBInit)/sizeof(const char *);

// vim:ts=4
