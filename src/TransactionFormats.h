#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

enum TransactionType
{
	ttINVALID			= -1,
	ttPAYMENT			= 0,
	ttCLAIM				= 1,
	ttWALLET_ADD		= 2,
	ttACCOUNT_SET		= 3,
	ttPASSWORD_FUND		= 4,
	ttPASSWORD_SET		= 5,
	ttNICKNAME_SET		= 6,
	ttOFFER_CREATE		= 7,
	ttOFFER_CANCEL		= 8,
	ttCONTRACT			= 9,
	ttCONTRACT_REMOVE	= 10,  // can we use the same msg as offer cancel

	ttCREDIT_SET		= 20,
};

struct TransactionFormat
{
	const char *		t_name;
	TransactionType		t_type;
	SOElement			elements[24];
};

const int TransactionMinLen			= 32;
const int TransactionMaxLen			= 1048576;

//
// Transaction flags.
//

// OfferCreate flags:
const uint32 tfPassive				= 0x00010000;

// Payment flags:
const uint32 tfCreateAccount		= 0x00010000;
const uint32 tfPartialPayment		= 0x00020000;
const uint32 tfLimitQuality			= 0x00040000;
const uint32 tfNoRippleDirect		= 0x00080000;

extern TransactionFormat InnerTxnFormats[];
extern TransactionFormat* getTxnFormat(TransactionType t);
extern TransactionFormat* getTxnFormat(int t);
#endif
// vim:ts=4
