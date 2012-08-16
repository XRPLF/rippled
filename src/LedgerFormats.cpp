
#include "LedgerFormats.h"

#define S_FIELD(x) sf##x, #x

LedgerEntryFormat LedgerFormats[]=
{
	{ "AccountRoot", ltACCOUNT_ROOT, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Sequence),			STI_UINT32,		SOE_REQUIRED, 0 },
		{ S_FIELD(Balance),				STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(LastReceive),			STI_UINT32,		SOE_REQUIRED, 0 },
		{ S_FIELD(LastTxnSeq),			STI_UINT32,		SOE_REQUIRED, 0 },
		{ S_FIELD(AuthorizedKey),		STI_ACCOUNT,	SOE_IFFLAG,   1 },
		{ S_FIELD(EmailHash),			STI_HASH128,	SOE_IFFLAG,   2 },
		{ S_FIELD(WalletLocator),		STI_HASH256,	SOE_IFFLAG,   4 },
		{ S_FIELD(MessageKey),			STI_VL,			SOE_IFFLAG,   8 },
		{ S_FIELD(TransferRate),		STI_UINT32,		SOE_IFFLAG,  16 },
		{ S_FIELD(Domain),				STI_VL,			SOE_IFFLAG,  32 },
		{ S_FIELD(PublishHash),			STI_HASH256,	SOE_IFFLAG,  64 },
		{ S_FIELD(PublishSize),			STI_UINT32,		SOE_IFFLAG, 128 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ "DirectoryNode", ltDIR_NODE, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Indexes),				STI_VECTOR256,	SOE_REQUIRED, 0 },
		{ S_FIELD(IndexNext),			STI_UINT64,		SOE_IFFLAG,   1 },
		{ S_FIELD(IndexPrevious),		STI_UINT64,		SOE_IFFLAG,   2 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ "GeneratorMap", ltGENERATOR_MAP, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Generator),			STI_VL,			SOE_REQUIRED, 0 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ "Nickname", ltNICKNAME, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Account),				STI_ACCOUNT,	SOE_REQUIRED, 0 },
		{ S_FIELD(MinimumOffer),		STI_AMOUNT,		SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ "Offer", ltOFFER, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Account),				STI_ACCOUNT,	SOE_REQUIRED, 0 },
		{ S_FIELD(Sequence),			STI_UINT32,		SOE_REQUIRED, 0 },
		{ S_FIELD(TakerPays),			STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(TakerGets),			STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(BookDirectory),		STI_HASH256,	SOE_REQUIRED, 0 },
		{ S_FIELD(BookNode),			STI_UINT64,		SOE_REQUIRED, 0 },
		{ S_FIELD(OwnerNode),			STI_UINT64,		SOE_REQUIRED, 0 },
		{ S_FIELD(PaysIssuer),			STI_ACCOUNT,	SOE_IFFLAG,   1 },
		{ S_FIELD(GetsIssuer),			STI_ACCOUNT,	SOE_IFFLAG,   2 },
		{ S_FIELD(Expiration),			STI_UINT32,		SOE_IFFLAG,   4 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ "RippleState", ltRIPPLE_STATE, {
		{ S_FIELD(Flags),				STI_UINT32,		SOE_FLAGS,	  0 },
		{ S_FIELD(Balance),				STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(LowID),				STI_ACCOUNT,	SOE_REQUIRED, 0 },
		{ S_FIELD(LowLimit),			STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(HighID),				STI_ACCOUNT,	SOE_REQUIRED, 0 },
		{ S_FIELD(HighLimit),			STI_AMOUNT,		SOE_REQUIRED, 0 },
		{ S_FIELD(LowQualityIn),		STI_UINT32,		SOE_IFFLAG,   1 },
		{ S_FIELD(LowQualityOut),		STI_UINT32,		SOE_IFFLAG,   2 },
		{ S_FIELD(HighQualityIn),		STI_UINT32,		SOE_IFFLAG,   4 },
		{ S_FIELD(HighQualityOut),		STI_UINT32,		SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),			STI_TL,			SOE_IFFLAG,   0x01000000 },
		{ sfInvalid, NULL,				STI_DONE,		SOE_NEVER,	  -1 } }
	},
	{ NULL, ltINVALID }
};

LedgerEntryFormat* getLgrFormat(LedgerEntryType t)
{
	LedgerEntryFormat* f = LedgerFormats;
	while (f->t_name != NULL)
	{
		if (f->t_type == t) return f;
		++f;
	}
	return NULL;
}
// vim:ts=4
