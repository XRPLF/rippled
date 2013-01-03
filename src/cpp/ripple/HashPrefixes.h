#ifndef __HASHPREFIXES__
#define __HASHPREFIXES__

#include "types.h"

// TXN - Hash of transaction plus signature to give transaction ID
const uint32 sHP_TransactionID =	0x54584E00;

// TND - Hash of transaction plus metadata
const uint32 sHP_TransactionNode =	0x534E4400;

// MLN - Hash of account state
const uint32 sHP_LeafNode =			0x4D4C4E00;

// MIN - Hash of inner node in tree
const uint32 sHP_InnerNode =		0x4D494E00;

// LGR - Hash of ledger master data for signing
const uint32 sHP_Ledger =			0x4C575200;

// STX - Hash of inner transaction to sign
const uint32 sHP_TransactionSign =	0x53545800;

// VAL - Hash of validation for signing
const uint32 sHP_Validation =		0x56414C00;

// PRP - Hash of proposal for signing
const uint32 sHP_Proposal =			0x50525000;

// stx - TESTNET Hash of inner transaction to sign
const uint32 sHP_TestNetTransactionSign =	0x73747800;

// val - TESTNET Hash of validation for signing
const uint32 sHP_TestNetValidation		=	0x76616C00;

// prp - TESTNET Hash of proposal for signing
const uint32 sHP_TestNetProposal		=	0x70727000;

#endif

// vim:ts=4
