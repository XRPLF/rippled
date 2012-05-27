#ifndef __LEDGERFORMATS__
#define __LEDGERFORMATS__

#include "SerializedObject.h"

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

// In the format 160 + namespace + other.
enum LedgerNameSpace
{
	lnsGenerator	= -1,
	lnsAccounts,
	lnsRipple,
	lnsBonds,
	lnsInvoices,
	lnsMultiSig
};

enum LedgerSpecificFlags
{
	lsfLowIndexed	= 0x00010000,
	lsfHighIndexed	= 0x00020000,
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
