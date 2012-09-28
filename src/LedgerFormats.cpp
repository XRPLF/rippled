
#include "LedgerFormats.h"

#define LEF_BASE							\
	{ sfLedgerEntryType,	SOE_REQUIRED },	\
	{ sfFlags,				SOE_REQUIRED },	\
	{ sfLedgerIndex,		SOE_OPTIONAL },

LedgerEntryFormat LedgerFormats[]=
{
	{ "AccountRoot", ltACCOUNT_ROOT, { LEF_BASE
		{ sfAccount,		SOE_REQUIRED },
		{ sfSequence,		SOE_REQUIRED },
		{ sfBalance,		SOE_REQUIRED },
		{ sfLastTxnID,		SOE_REQUIRED },
		{ sfLastTxnSeq,		SOE_REQUIRED },
		{ sfAuthorizedKey,	SOE_OPTIONAL },
		{ sfEmailHash,		SOE_OPTIONAL },
		{ sfWalletLocator,	SOE_OPTIONAL },
		{ sfMessageKey,		SOE_OPTIONAL },
		{ sfTransferRate,	SOE_OPTIONAL },
		{ sfDomain,			SOE_OPTIONAL },
		{ sfPublishHash,	SOE_OPTIONAL },
		{ sfPublishSize,	SOE_OPTIONAL },
		{ sfInvalid,		SOE_END } }
	},
	{ "Contract", ltCONTRACT, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfAccount,		SOE_REQUIRED },
		{ sfBalance,		SOE_REQUIRED },
		{ sfLastTxnID,		SOE_REQUIRED },
		{ sfLastTxnSeq,		SOE_REQUIRED },
		{ sfIssuer,			SOE_REQUIRED },
		{ sfOwner,			SOE_REQUIRED },
		{ sfExpiration,		SOE_REQUIRED },
		{ sfBondAmount,		SOE_REQUIRED },
		{ sfCreateCode,		SOE_REQUIRED },
		{ sfFundCode,		SOE_REQUIRED },
		{ sfRemoveCode,		SOE_REQUIRED },
		{ sfExpireCode,		SOE_REQUIRED },
		{ sfInvalid,		SOE_END } }
	},
	{ "DirectoryNode", ltDIR_NODE, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfIndexes,		SOE_REQUIRED },
		{ sfIndexNext,		SOE_OPTIONAL },
		{ sfIndexPrevious,	SOE_OPTIONAL },
		{ sfInvalid,		SOE_END } }
	},
	{ "GeneratorMap", ltGENERATOR_MAP, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfGenerator,		SOE_REQUIRED },
		{ sfInvalid,		SOE_END } }
	},
	{ "Nickname", ltNICKNAME, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfAccount,		SOE_REQUIRED },
		{ sfMinimumOffer,	SOE_OPTIONAL },
		{ sfInvalid,		SOE_END } }
	},
	{ "Offer", ltOFFER, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfAccount,		SOE_REQUIRED },
		{ sfSequence,		SOE_REQUIRED },
		{ sfTakerPays,		SOE_REQUIRED },
		{ sfTakerGets,		SOE_REQUIRED },
		{ sfBookDirectory,	SOE_REQUIRED },
		{ sfBookNode,		SOE_REQUIRED },
		{ sfOwnerNode,		SOE_REQUIRED },
		{ sfLastTxnID,		SOE_REQUIRED },
		{ sfLastTxnSeq,		SOE_REQUIRED },
		{ sfExpiration,		SOE_OPTIONAL },
		{ sfInvalid,		SOE_END } }
	},
	{ "RippleState", ltRIPPLE_STATE, { LEF_BASE
		{ sfLedgerEntryType,SOE_REQUIRED },
		{ sfFlags,			SOE_REQUIRED },
		{ sfBalance,		SOE_REQUIRED },
		{ sfLowID,			SOE_REQUIRED },
		{ sfLowLimit,		SOE_REQUIRED },
		{ sfHighID,			SOE_REQUIRED },
		{ sfHighLimit,		SOE_REQUIRED },
		{ sfLastTxnID,		SOE_REQUIRED },
		{ sfLastTxnSeq,		SOE_REQUIRED },
		{ sfLowQualityIn,	SOE_OPTIONAL },
		{ sfLowQualityOut,	SOE_OPTIONAL },
		{ sfHighQualityIn,	SOE_OPTIONAL },
		{ sfHighQualityOut,	SOE_OPTIONAL },
		{ sfInvalid,		SOE_END } }
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
