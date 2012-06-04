#ifndef __LEDGERFORMATS__
#define __LEDGERFORMATS__

#include "SerializedObject.h"

// Used as the type of a transaction or the type of a ledger entry.
enum LedgerEntryType
{
	ltINVALID		= -1,
	ltACCOUNT_ROOT,
	ltDIR_ROOT,
	ltDIR_NODE,
	ltGENERATOR_MAP,
	ltRIPPLE_STATE,
	ltNICKNAME
};

// Used as a prefix for computing ledger indexes (keys).
enum LedgerNameSpace
{
	spaceAccount,
	spaceGenerator,
	spaceNickname,
	spaceRipple,
	spaceRippleDir,
	spaceOffer,
	spaceOfferDir,
	spaceBond,
	spaceInvoice,
	spaceMultiSig,
};

enum LedgerSpecificFlags
{
	// ltRIPPLE_STATE
	lsfLowIndexed		= 0x00010000,
	lsfHighIndexed		= 0x00020000,

	// ltACCOUNT_ROOT
	lsfPasswordSpent	= 0x00010000,	// True if password set fee is spent.
};

struct LedgerEntryFormat
{
	const char *t_name;
	LedgerEntryType t_type;
	SOElement elements[20];
};

extern LedgerEntryFormat LedgerFormats[];
extern LedgerEntryFormat* getLgrFormat(LedgerEntryType t);
#endif
// vim:ts=4
