
#include "TransactionFormats.h"

#define S_FIELD(x) sf##x, #x

TransactionFormat InnerTxnFormats[]=
{
	{ "Claim", ttCLAIM, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Generator),    STI_VL,      SOE_REQUIRED, 0 },
		{ S_FIELD(PubKey),		 STI_VL,      SOE_REQUIRED, 0 },
		{ S_FIELD(Signature),	 STI_VL,	  SOE_REQUIRED, 0 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "CreditSet", ttCREDIT_SET, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Destination),  STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(LimitAmount),  STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(AcceptRate),	 STI_UINT32,  SOE_IFFLAG,   1 },
		{ S_FIELD(AcceptStart),	 STI_UINT32,  SOE_IFFLAG,   2 },
		{ S_FIELD(AcceptExpire), STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "Invoice", ttINVOICE, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Target),       STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(Amount),       STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(Currency),     STI_HASH160, SOE_IFFLAG,   1 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   2 },
		{ S_FIELD(Destination),  STI_ACCOUNT, SOE_IFFLAG,   4 },
		{ S_FIELD(Identifier),   STI_VL,      SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "Offer", ttOFFER, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(AmountIn),     STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(CurrencyIn),   STI_HASH160, SOE_IFFLAG,   2 },
		{ S_FIELD(AmountOut),    STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(CurrencyOut),  STI_HASH160, SOE_IFFLAG,   4 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   8 },
		{ S_FIELD(Destination),  STI_ACCOUNT, SOE_IFFLAG,   16 },
		{ S_FIELD(ExpireLedger), STI_UINT32,  SOE_IFFLAG,   32 },
		{ S_FIELD(Identifier),   STI_VL,      SOE_IFFLAG,   64 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "Payment", ttPAYMENT, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(Destination),  STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ S_FIELD(Amount),       STI_AMOUNT,  SOE_REQUIRED, 0 },
		{ S_FIELD(SendMax),		 STI_AMOUNT,  SOE_IFFLAG,   1 },
		{ S_FIELD(Paths),		 STI_PATHSET, SOE_IFFLAG,   2 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(InvoiceID),    STI_HASH256, SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "TransitSet", ttTRANSIT_SET, {
		{ S_FIELD(Flags),        STI_UINT32,  SOE_FLAGS,    0 },
		{ S_FIELD(TransitRate),	 STI_UINT32,  SOE_IFFLAG,	1 },
		{ S_FIELD(TransitStart), STI_UINT32,  SOE_IFFLAG,   2 },
		{ S_FIELD(TransitExpire),STI_UINT32,  SOE_IFFLAG,   4 },
		{ S_FIELD(SourceTag),    STI_UINT32,  SOE_IFFLAG,   8 },
		{ S_FIELD(Extensions),   STI_TL,      SOE_IFFLAG,   0x02000000 },
		{ sfInvalid, NULL,       STI_DONE,    SOE_NEVER,    -1 } }
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
