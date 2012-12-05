
#include "Log.h"

#include "RPCErr.h"
#include "utils.h"

#include "../json/writer.h"

SETUP_LOG();

Json::Value rpcError(int iError, Json::Value jvResult)
{
	static struct {
		int			iError;
		const char*	pToken;
		const char*	pMessage;
	} errorInfoA[] = {
		{ rpcACT_EXISTS,			"actExists",		"Account already exists."								},
		{ rpcACT_MALFORMED,			"actMalformed",		"Account malformed."									},
		{ rpcACT_NOT_FOUND,			"actNotFound",		"Account not found."									},
		{ rpcBAD_SEED,				"badSeed",			"Disallowed seed."										},
		{ rpcBAD_SYNTAX,			"badSyntax",		"Syntax error."											},
		{ rpcDST_ACT_MALFORMED,		"dstActMalformed",	"Destination account is malformed."						},
		{ rpcDST_ACT_MISSING,		"dstActMissing",	"Destination account does not exists."					},
		{ rpcDST_AMT_MALFORMED,		"dstAmtMalformed",	"Destination amount/currency/issuer is malformed."		},
		{ rpcFAIL_GEN_DECRPYT,		"failGenDecrypt",	"Failed to decrypt generator."							},
		{ rpcGETS_ACT_MALFORMED,	"getsActMalformed",	"Gets account malformed."								},
		{ rpcGETS_AMT_MALFORMED,	"getsAmtMalformed",	"Gets amount malformed."								},
		{ rpcHOST_IP_MALFORMED,		"hostIpMalformed",	"Host IP is malformed."									},
		{ rpcINSUF_FUNDS,			"insufFunds",		"Insufficient funds."									},
		{ rpcINTERNAL,				"internal",			"Internal error."										},
		{ rpcINVALID_PARAMS,		"invalidParams",	"Invalid parameters."									},
		{ rpcJSON_RPC,				"json_rpc",			"JSON-RPC transport error."								},
		{ rpcLGR_IDXS_INVALID,		"lgrIdxsInvalid",	"Ledger indexes invalid."								},
		{ rpcLGR_IDX_MALFORMED,		"lgrIdxMalformed",	"Ledger index malformed."								},
		{ rpcLGR_NOT_FOUND,			"lgrNotFound",		"Ledger not found."										},
		{ rpcNICKNAME_MALFORMED,	"nicknameMalformed","Nickname is malformed."								},
		{ rpcNICKNAME_MISSING,		"nicknameMissing",	"Nickname does not exist."								},
		{ rpcNICKNAME_PERM,			"nicknamePerm",		"Account does not control nickname."					},
		{ rpcNOT_IMPL,				"notImpl",			"Not implemented."										},
		{ rpcNO_ACCOUNT,			"noAccount",		"No such account."										},
		{ rpcNO_CLOSED,				"noClosed",			"Closed ledger is unavailable."							},
		{ rpcNO_CURRENT,			"noCurrent",		"Current ledger is unavailable."						},
		{ rpcNO_EVENTS,				"noEvents",			"Current transport does not support events."			},
		{ rpcNO_GEN_DECRPYT,		"noGenDectypt",		"Password failed to decrypt master public generator."	},
		{ rpcNO_NETWORK,			"noNetwork",		"Network not available."								},
		{ rpcNO_PATH,				"noPath",			"Unable to find a ripple path."							},
		{ rpcNO_PERMISSION,			"noPermission",		"You don't have permission for this command."			},
		{ rpcNOT_STANDALONE,		"notStandAlone",	"Operation valid in debug mode only."					},
		{ rpcNOT_SUPPORTED,			"notSupported",		"Operation not supported."								},
		{ rpcPASSWD_CHANGED,		"passwdChanged",	"Wrong key, password changed."							},
		{ rpcPAYS_ACT_MALFORMED,	"paysActMalformed",	"Pays account malformed."								},
		{ rpcPAYS_AMT_MALFORMED,	"paysAmtMalformed",	"Pays amount malformed."								},
		{ rpcPORT_MALFORMED,		"portMalformed",	"Port is malformed."									},
		{ rpcPUBLIC_MALFORMED,		"publicMalformed",	"Public key is malformed."								},
		{ rpcQUALITY_MALFORMED,		"qualityMalformed",	"Quality malformed."									},
		{ rpcSRC_ACT_MALFORMED,		"srcActMalformed",	"Source account is malformed."							},
		{ rpcSRC_ACT_MISSING,		"srcActMissing",	"Source account does not exist."						},
		{ rpcSRC_AMT_MALFORMED,		"srcAmtMalformed",	"Source amount/currency/issuer is malformed."			},
		{ rpcSRC_UNCLAIMED,			"srcUnclaimed",		"Source account is not claimed."						},
		{ rpcSUCCESS,				"success",			"Success."												},
		{ rpcTXN_NOT_FOUND,			"txnNotFound",		"Transaction not found."								},
		{ rpcUNKNOWN_COMMAND,		"unknownCmd",		"Unknown command."										},
		{ rpcWRONG_SEED,			"wrongSeed",		"The regular key does not point as the master key."		},
	};

	int		i;

	for (i=NUMBER(errorInfoA); i-- && errorInfoA[i].iError != iError;)
		;

	jvResult["error"]			= i >= 0 ? errorInfoA[i].pToken : lexical_cast_i(iError);
	jvResult["error_message"]	= i >= 0 ? errorInfoA[i].pMessage : lexical_cast_i(iError);
	jvResult["error_code"]		= iError;

	if (i >= 0)
	{
		cLog(lsDEBUG) << "rpcError: "
			<< errorInfoA[i].pToken << ": " << errorInfoA[i].pMessage << std::endl;
	}

	return jvResult;
}

// vim:ts=4
