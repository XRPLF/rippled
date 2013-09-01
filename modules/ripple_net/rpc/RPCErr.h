//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_RPC_RPCERR_H_INCLUDED
#define RIPPLE_NET_RPC_RPCERR_H_INCLUDED

enum
{
    rpcSUCCESS = 0,
    rpcBAD_SYNTAX,  // Must be 1 to print usage to command line.
    rpcJSON_RPC,
    rpcFORBIDDEN,

    // Error numbers beyond this line are not stable between versions.
    // Programs should use error tokens.

    // Misc failure
    rpcLOAD_FAILED,
    rpcNO_PERMISSION,
    rpcNO_EVENTS,
    rpcNOT_STANDALONE,
    rpcTOO_BUSY,
    rpcSLOW_DOWN,

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
    rpcNO_PF_REQUEST,

    // Bad parameter
    rpcACT_BITCOIN,
    rpcACT_MALFORMED,
    rpcQUALITY_MALFORMED,
    rpcBAD_BLOB,
    rpcBAD_FEATURE,
    rpcBAD_ISSUER,
    rpcBAD_MARKET,
    rpcBAD_SECRET,
    rpcBAD_SEED,
    rpcCOMMAND_MISSING,
    rpcDST_ACT_MALFORMED,
    rpcDST_ACT_MISSING,
    rpcDST_AMT_MALFORMED,
    rpcDST_ISR_MALFORMED,
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
    rpcSRC_ACT_NOT_FOUND,
    rpcSRC_AMT_MALFORMED,
    rpcSRC_CUR_MALFORMED,
    rpcSRC_ISR_MALFORMED,

    // Internal error (should never happen)
    rpcINTERNAL,        // Generic internal error.
    rpcFAIL_GEN_DECRPYT,
    rpcNOT_IMPL,
    rpcNOT_SUPPORTED,
    rpcNO_GEN_DECRPYT,
};

bool isRpcError (Json::Value jvResult);
Json::Value rpcError (int iError, Json::Value jvResult = Json::Value (Json::objectValue));

#endif
// vim:ts=4
