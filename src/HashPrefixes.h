#ifndef __HASHPREFIXES__
#define __HASHPREFIXES__

#include "types.h"

// TXN
const uint32 sHP_TransactionID =	0x54584E00;

// STX
const uint32 sHP_TransactionSign =	0x53545800;

// MLN
const uint32 sHP_LeafNode =			0x4D4C4E00;

// MIN
const uint32 sHP_InnerNode =		0x4D494E00;

// LGR
const uint32 sHP_Ledger =			0x4C575200;

// VAL
const uint32 sHP_Validation =		0x56414C00;

// PRP
const uint32 sHP_Proposal =			0x50525000;

#endif

// vim:ts=4
