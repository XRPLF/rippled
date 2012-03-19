#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

#define STI_ACCOUNT STI_HASH160

struct TransactionFormat
{
	const char *t_name;
	int t_id;
	SOElement elements[16];
};

extern TransactionFormat InnerTxnFormats[];

enum TransactionType
{
	ttMAKE_PAYMENT=0,
	ttNTX_INVOICE=1,
	ttEXCHANGE_OFFER=2
};

#endif
