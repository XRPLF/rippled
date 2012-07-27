
#include "TransactionFormats.h"

#define S_FIELD(x) sf##x, #x

TransactionFormat InnerTxnFormats[]=
{
	{ "AccountSet", ttACCOUNT_SET, {
		{ S_FIELD(Flags),			STI_UINT32,	 SOE_FLAGS,    0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(EmailHash),		STI_HASH128, SOE_IFFLAG,   2 },
		{ S_FIELD(WalletLocator),	STI_HASH256, SOE_IFFLAG,   4 },
		{ S_FIELD(MessageKey),		STI_VL,      SOE_IFFLAG,   8 },
		{ S_FIELD(Domain),			STI_VL,      SOE_IFFLAG,   16 },
		{ S_FIELD(TransferRate),	STI_UINT32,	 SOE_IFFLAG,   32 },
		{ S_FIELD(PublishHash),		STI_HASH256, SOE_IFFLAG,   64 },
		{ S_FIELD(PublishSize),		STI_UINT32,	 SOE_IFFLAG,   128 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "Claim", ttCLAIM, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Generator),		STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(PubKey),			STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(Signature),		STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "CreditSet", ttCREDIT_SET, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Destination),		STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(LimitAmount),		STI_AMOUNT,  SOE_IFFLAG,   2 },
		{ S_FIELD(QualityIn),		STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(QualityOut),		STI_UINT32,  SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "Invoice", ttINVOICE, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Target),			STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(Amount),			STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Destination),		STI_ACCOUNT, SOE_IFFLAG,   2 },
		{ S_FIELD(Identifier),		STI_VL,		 SOE_IFFLAG,   4 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "NicknameSet", ttNICKNAME_SET, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Nickname),		STI_HASH256, SOE_REQUIRED, 0 },
		{ S_FIELD(MinimumOffer),	STI_AMOUNT,  SOE_IFFLAG,   1 },
		{ S_FIELD(Signature),		STI_VL,		 SOE_IFFLAG,   2 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "OfferCreate", ttOFFER_CREATE, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(TakerPays),		STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(TakerGets),		STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(PaysIssuer),		STI_ACCOUNT, SOE_IFFLAG,   2 },
		{ S_FIELD(GetsIssuer),		STI_ACCOUNT, SOE_IFFLAG,   4 },
		{ S_FIELD(Expiration),		STI_UINT32,  SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "OfferCancel", ttOFFER_CANCEL, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(OfferSequence),	STI_UINT32,  SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "PasswordFund", ttPASSWORD_FUND, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Destination),		STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "PasswordSet", ttPASSWORD_SET, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(AuthorizedKey),	STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(Generator),		STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(PubKey),			STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(Signature),		STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "Payment", ttPAYMENT, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Destination),		STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(Amount),			STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(SendMax),			STI_AMOUNT,  SOE_IFFLAG,   1 },
		{ S_FIELD(Paths),			STI_PATHSET, SOE_IFFLAG,   2 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(InvoiceID),		STI_HASH256, SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ "WalletAdd", ttWALLET_ADD, {
		{ S_FIELD(Flags),			STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Amount),			STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(AuthorizedKey),	STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(PubKey),			STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(Signature),		STI_VL,		 SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),		STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),		STI_TL,		 SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,			STI_DONE,	 SOE_NEVER,    -1 } }
	},
	{ NULL, ttINVALID }
};

TransactionFormat* getTxnFormat(TransactionType t)
{
	TransactionFormat* f = InnerTxnFormats;
	while (f->t_name != NULL)
	{
		if (f->t_type == t) return f;
		++f;
	}
	return NULL;
}
// vim:ts=4
