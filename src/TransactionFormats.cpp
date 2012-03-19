
#include "TransactionFormats.h"

TransactionFormat InnerTxnFormats[]=
{
	{ "MakePayment", 0, {
		{ "Flags",        STI_UINT16,  SOE_FLAGS,    0 },
		{ "Sequence", 	  STI_UINT32,  SOE_REQUIRED, 0 },
		{ "Destination",  STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ "Amount",       STI_UINT64,  SOE_REQUIRED, 0 },
		{ "Currency",     STI_HASH160, SOE_IFFLAG,   1 },
		{ "SourceTag",    STI_UINT32,  SOE_IFFLAG,   2 },
		{ "TargetLedger", STI_UINT32,  SOE_IFFLAG,   4 },
		{ "InvoiceID",    STI_HASH256, SOE_IFFLAG,   8 },
		{ "Extensions",   STI_TL,      SOE_IFFLAG,   32768 },
		{ NULL,           STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "Invoice", 1, {
		{ "Flags",        STI_UINT16,  SOE_FLAGS,    0 },
		{ "Sequence",     STI_UINT32,  SOE_REQUIRED, 0 },
		{ "Target",       STI_ACCOUNT, SOE_REQUIRED, 0 },
		{ "Amount",       STI_UINT64,  SOE_REQUIRED, 0 },
		{ "Currency",     STI_HASH160, SOE_IFFLAG,   1 },
		{ "SourceTag",    STI_UINT32,  SOE_IFFLAG,   2 },
		{ "Destination",  STI_ACCOUNT, SOE_IFFLAG,   4 },
		{ "TargetLedger", STI_UINT32,  SOE_IFFLAG,   8 },
		{ "Identifier",   STI_VL,      SOE_IFFLAG,   16 },
		{ "Extensions",   STI_TL,      SOE_IFFLAG,   32768 },
		{ NULL,           STI_DONE,    SOE_NEVER,    -1 } }
	},
	{ "Offer", 2, {
		{ "Flags",        STI_UINT16,  SOE_FLAGS,    0 },
		{ "Sequence",     STI_UINT32,  SOE_REQUIRED, 0 },
		{ "AmountIn",     STI_UINT64,  SOE_REQUIRED, 0 },
		{ "CurrencyIn",   STI_HASH160, SOE_IFFLAG,   2 },
		{ "AmountOut",    STI_UINT64,  SOE_REQUIRED, 0 },
		{ "CurrencyOut",  STI_HASH160, SOE_IFFLAG,   4 },
		{ "SourceTag",    STI_UINT32,  SOE_IFFLAG,   8 },
		{ "Destination",  STI_ACCOUNT, SOE_IFFLAG,   16 },
		{ "TargetLedger", STI_UINT32,  SOE_IFFLAG,   32 },
		{ "ExpireLedger", STI_UINT32,  SOE_IFFLAG,   64 },
		{ "Identifier",   STI_VL,      SOE_IFFLAG,   128 },
		{ "Extensions",   STI_TL,      SOE_IFFLAG,   32768 },
		{ NULL,           STI_DONE,    SOE_NEVER,    -1 } }
	}
};
