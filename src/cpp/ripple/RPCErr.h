#ifndef __RPCERR__
#define __RPCERR__

#include "../json/value.h"

enum {
	rpcSUCCESS,

	rpcBAD_SYNTAX,	// Must be 1 to print usage to command line.
	rpcJSON_RPC,

	// Error numbers beyond this line are not stable between versions.
	// Programs should use error tokens.

	// Misc failure
	rpcLOAD_FAILED,
	rpcNO_PERMISSION,
	rpcNO_EVENTS,
	rpcNOT_STANDALONE,

	// Networking
	rpcNO_CLOSED,
	rpcNO_CURRENT,
	rpcNO_NETWORK,

	// Ledger state
	rpcACT_EXISTS,
	rpcACT_NOT_FOUND,
	rpcINSUF_FUNDS,
	rpcLGR_NOT_FOUND,
	rpcNICKNAME_MISSING,
	rpcNO_ACCOUNT,
	rpcNO_PATH,
	rpcPASSWD_CHANGED,
	rpcSRC_MISSING,
	rpcSRC_UNCLAIMED,
	rpcTXN_NOT_FOUND,
	rpcWRONG_SEED,

	// Malformed command
	rpcINVALID_PARAMS,
	rpcUNKNOWN_COMMAND,

	// Bad parameter
	rpcACT_MALFORMED,
	rpcQUALITY_MALFORMED,
	rpcBAD_SEED,
	rpcDST_ACT_MALFORMED,
	rpcDST_ACT_MISSING,
	rpcDST_AMT_MALFORMED,
	rpcGETS_ACT_MALFORMED,
	rpcGETS_AMT_MALFORMED,
	rpcHOST_IP_MALFORMED,
	rpcLGR_IDXS_INVALID,
	rpcLGR_IDX_MALFORMED,
	rpcNICKNAME_MALFORMED,
	rpcNICKNAME_PERM,
	rpcPAYS_ACT_MALFORMED,
	rpcPAYS_AMT_MALFORMED,
	rpcPORT_MALFORMED,
	rpcPUBLIC_MALFORMED,
	rpcSRC_ACT_MALFORMED,
	rpcSRC_ACT_MISSING,
	rpcSRC_AMT_MALFORMED,

	// Internal error (should never happen)
	rpcINTERNAL,		// Generic internal error.
	rpcFAIL_GEN_DECRPYT,
	rpcNOT_IMPL,
	rpcNO_GEN_DECRPYT,
};

Json::Value rpcError(int iError, Json::Value jvResult=Json::Value(Json::objectValue));
#endif
// vim:ts=4
