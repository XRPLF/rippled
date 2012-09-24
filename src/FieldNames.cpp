
#include "FieldNames.h"

#define S_FIELD(x) sf##x, #x

FieldName FieldNames[]=
{

	// 8-bit integers
	{ S_FIELD(CloseResolution),		STI_UINT8, 1 },

	// 32-bit integers (common)
	{ S_FIELD(Flags),				STI_UINT32, 1 },
	{ S_FIELD(SourceTag),			STI_UINT32, 2 },
	{ S_FIELD(Sequence),			STI_UINT32, 3 },
	{ S_FIELD(LastTxnSeq),			STI_UINT32, 4 },
	{ S_FIELD(LedgerSequence),		STI_UINT32, 5 },
	{ S_FIELD(CloseTime),			STI_UINT32, 6 },
	{ S_FIELD(ParentCloseTime),		STI_UINT32, 7 },
	{ S_FIELD(SigningTime),			STI_UINT32, 8 },
	{ S_FIELD(Expiration),			STI_UINT32, 9 },
	{ S_FIELD(TransferRate),		STI_UINT32, 10 },
	{ S_FIELD(PublishSize),			STI_UINT32, 11 },

	// 32-bit integers (rare)
	{ S_FIELD(HighQualityIn),		STI_UINT32, 16 },
	{ S_FIELD(HighQualityOut),		STI_UINT32, 17 },
	{ S_FIELD(LowQualityIn),		STI_UINT32, 18 },
	{ S_FIELD(LowQualityOut),		STI_UINT32, 19 },
	{ S_FIELD(QualityIn),			STI_UINT32, 20 },
	{ S_FIELD(QualityOut),			STI_UINT32, 21 },
	{ S_FIELD(StampEscrow),			STI_UINT32, 22 },
	{ S_FIELD(BondAmount),			STI_UINT32, 23 },
	{ S_FIELD(LoadFee),				STI_UINT32, 24 },

	// 64-bit integers
	{ S_FIELD(IndexNext),			STI_UINT64, 1 },
	{ S_FIELD(IndexPrevious),		STI_UINT64, 2 },
	{ S_FIELD(BookNode),			STI_UINT64, 3 },
	{ S_FIELD(OwnerNode),			STI_UINT64, 4 },
	{ S_FIELD(BaseFee),				STI_UINT64, 5 },

	// 128-bit
	{ S_FIELD(PublishSize),			STI_HASH128, 1 },
	{ S_FIELD(EmailHash),			STI_HASH128, 2 },

	// 256-bit
	{ S_FIELD(LedgerHash),			STI_HASH256, 1 },
	{ S_FIELD(ParentHash),			STI_HASH256, 2 },
	{ S_FIELD(TransactionHash),		STI_HASH256, 3 },
	{ S_FIELD(AccountHash),			STI_HASH256, 4 },
	{ S_FIELD(LastTxnID),			STI_HASH256, 5 },
	{ S_FIELD(WalletLocator),		STI_HASH256, 6 },
	{ S_FIELD(PublishHash),			STI_HASH256, 7 },
	{ S_FIELD(Nickname),			STI_HASH256, 8 },

	// currency amount
	{ S_FIELD(Amount),				STI_AMOUNT, 1 },
	{ S_FIELD(Balance),				STI_AMOUNT, 2 },
	{ S_FIELD(LimitAmount),			STI_AMOUNT, 3 },
	{ S_FIELD(TakerPays),			STI_AMOUNT, 4 },
	{ S_FIELD(TakerGets),			STI_AMOUNT, 5 },
	{ S_FIELD(LowLimit),			STI_AMOUNT, 6 },
	{ S_FIELD(HighLimit),			STI_AMOUNT, 7 },
	{ S_FIELD(MinimumOffer),		STI_AMOUNT, 8 },

	// variable length
	{ S_FIELD(PublicKey),			STI_VL, 1 },
	{ S_FIELD(MessageKey),			STI_VL, 2 },
	{ S_FIELD(SigningKey),			STI_VL, 3 },
	{ S_FIELD(Signature),			STI_VL, 4 },
	{ S_FIELD(Generator),			STI_VL, 5 },
	{ S_FIELD(Domain),				STI_VL, 6 },

	// account
	{ S_FIELD(Account),				STI_ACCOUNT, 1 },
	{ S_FIELD(Owner),				STI_ACCOUNT, 2 },
	{ S_FIELD(Destination),			STI_ACCOUNT, 3 },
	{ S_FIELD(Issuer),				STI_ACCOUNT, 4 },
	{ S_FIELD(HighID),				STI_ACCOUNT, 5 },
	{ S_FIELD(LowID),				STI_ACCOUNT, 6 },
	{ S_FIELD(Target),				STI_ACCOUNT, 7 },

	// path set
	{ S_FIELD(Paths),				STI_PATHSET, 1 },

	// vector of 256-bit
	{ S_FIELD(Indexes),				STI_VECTOR256, 1 },

	// inner object
	{ S_FIELD(MiddleTransaction),	STI_OBJECT, 1 },
	{ S_FIELD(InnerTransaction),	STI_OBJECT, 2 },
	// OBJECT/15 is reserved for end of object

	// array of objects
	{ S_FIELD(SigningAccounts),		STI_ARRAY, 1 },
	// ARRAY/15 is reserved for end of array
};
