// This is not really a header file, but it can be used as one with
// appropriate #define statements.

	// types (common)
	TYPE("Int16",			UINT16,		1)
	TYPE("Int32",			UINT32,		2)
	TYPE("Int64",			UINT64,		3)
	TYPE("Hash128",			HASH128,	4)
	TYPE("Hash256",			HASH256,	5)
	TYPE("Amount",			AMOUNT,		6)
	TYPE("VariableLength",	VL,			7)
	TYPE("Account",			ACCOUNT,	8)
	// 9-13 are reserved
	TYPE("Object",			OBJECT,		14)
	TYPE("Array",			ARRAY,		15)

	// types (uncommon)
	TYPE("Int8",			UINT8,		16)
	TYPE("Hash160",			HASH160,	17)
	TYPE("PathSet",			PATHSET,	18)
	TYPE("Vector256",		VECTOR256,	19)



	// 8-bit integers
	FIELD(CloseResolution,		UINT8, 1)

	// 16-bit integers
	FIELD(LedgerEntryType,		UINT16, 1)
	FIELD(TransactionType,		UINT16, 1)

	// 32-bit integers (common)
	FIELD(ObjectType,			UINT32, 1)
	FIELD(Flags,				UINT32, 2)
	FIELD(SourceTag,			UINT32, 3)
	FIELD(Sequence,				UINT32, 4)
	FIELD(LastTxnSeq,			UINT32, 5)
	FIELD(LedgerSequence,		UINT32, 6)
	FIELD(CloseTime,			UINT32, 7)
	FIELD(ParentCloseTime,		UINT32, 8)
	FIELD(SigningTime,			UINT32, 9)
	FIELD(Expiration,			UINT32, 10)
	FIELD(TransferRate,			UINT32, 11)
	FIELD(PublishSize,			UINT32, 12)

	// 32-bit integers (uncommon)
	FIELD(HighQualityIn,		UINT32, 16)
	FIELD(HighQualityOut,		UINT32, 17)
	FIELD(LowQualityIn,			UINT32, 18)
	FIELD(LowQualityOut,		UINT32, 19)
	FIELD(QualityIn,			UINT32, 20)
	FIELD(QualityOut,			UINT32, 21)
	FIELD(StampEscrow,			UINT32, 22)
	FIELD(BondAmount,			UINT32, 23)
	FIELD(LoadFee,				UINT32, 24)
	FIELD(OfferSequence,		UINT32, 25)

	// 64-bit integers
	FIELD(Fee,					UINT64, 1)
	FIELD(IndexNext,			UINT64, 2)
	FIELD(IndexPrevious,		UINT64, 3)
	FIELD(BookNode,				UINT64, 4)
	FIELD(OwnerNode,			UINT64, 5)
	FIELD(BaseFee,				UINT64, 6)

	// 128-bit
	FIELD(EmailHash,			HASH128, 2)

	// 256-bit (common)
	FIELD(LedgerHash,			HASH256, 1)
	FIELD(ParentHash,			HASH256, 2)
	FIELD(TransactionHash,		HASH256, 3)
	FIELD(AccountHash,			HASH256, 4)
	FIELD(LastTxnID,			HASH256, 5)
	FIELD(LedgerIndex,			HASH256, 6)
	FIELD(WalletLocator,		HASH256, 7)
	FIELD(PublishHash,			HASH256, 8)

	// 256-bit (uncommon)
	FIELD(BookDirectory,		HASH256, 16)
	FIELD(InvoiceID,			HASH256, 17)
	FIELD(Nickname,				HASH256, 18)

	// currency amount (common)
	FIELD(Amount,				AMOUNT, 1)
	FIELD(Balance,				AMOUNT, 2)
	FIELD(LimitAmount,			AMOUNT, 3)
	FIELD(TakerPays,			AMOUNT, 4)
	FIELD(TakerGets,			AMOUNT, 5)
	FIELD(LowLimit,				AMOUNT, 6)
	FIELD(HighLimit,			AMOUNT, 7)
	FIELD(SendMax,				AMOUNT, 9)

	// current amount (uncommon)
	FIELD(MinimumOffer,			AMOUNT, 16)
	FIELD(RippleEscrow,			AMOUNT, 17)

	// variable length
	FIELD(PublicKey,			VL, 1)
	FIELD(MessageKey,			VL, 2)
	FIELD(SigningKey,			VL, 3)
	FIELD(Signature,			VL, 4)
	FIELD(Generator,			VL, 5)
	FIELD(Domain,				VL, 6)
	FIELD(FundCode,				VL, 7)
	FIELD(RemoveCode,			VL, 8)
	FIELD(ExpireCode,			VL, 9)
	FIELD(CreateCode,			VL, 10)

	// account
	FIELD(Account,				ACCOUNT, 1)
	FIELD(Owner,				ACCOUNT, 2)
	FIELD(Destination,			ACCOUNT, 3)
	FIELD(Issuer,				ACCOUNT, 4)
	FIELD(HighID,				ACCOUNT, 5)
	FIELD(LowID,				ACCOUNT, 6)
	FIELD(Target,				ACCOUNT, 7)
	FIELD(AuthorizedKey,		ACCOUNT, 8)

	// path set
	FIELD(Paths,				PATHSET, 1)

	// vector of 256-bit
	FIELD(Indexes,				VECTOR256, 1)

	// inner object
	// OBJECT/1 is reserved for end of object
	FIELD(MiddleTransaction,	OBJECT, 2)
	FIELD(InnerTransaction,		OBJECT, 3)

	// array of objects
	// ARRAY/1 is reserved for end of array
	FIELD(SigningAccounts,		ARRAY, 2)
