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

#endif
