#include "TransactionErr.h"
#include "utils.h"

bool transResultInfo(TER terCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		TER				terCode;
		const char*		cpToken;
		const char*		cpHuman;
	} transResultInfoA[] = {
		{	tefALREADY,				"tefALREADY",				"The exact transaction was already in this ledger."		},
		{	tefBAD_ADD_AUTH,		"tefBAD_ADD_AUTH",			"Not authorized to add account."						},
		{	tefBAD_AUTH,			"tefBAD_AUTH",				"Transaction's public key is not authorized."			},
		{	tefBAD_CLAIM_ID,		"tefBAD_CLAIM_ID",			"Malformed."											},
		{	tefBAD_GEN_AUTH,		"tefBAD_GEN_AUTH",			"Not authorized to claim generator."					},
		{	tefBAD_LEDGER,			"tefBAD_LEDGER",			"Ledger in unexpected state."							},
		{	tefCLAIMED,				"tefCLAIMED",				"Can not claim a previously claimed account."			},
		{	tefEXCEPTION,			"tefEXCEPTION",				"Unexpected program state."								},
		{	tefCREATED,				"tefCREATED",				"Can't add an already created account."					},
		{	tefGEN_IN_USE,			"tefGEN_IN_USE",			"Generator already in use."								},
		{	tefPAST_SEQ,			"tefPAST_SEQ",				"This sequence number has already past"					},

		{	telBAD_PATH_COUNT,		"telBAD_PATH_COUNT",		"Malformed: too many paths."							},
		{	telINSUF_FEE_P,			"telINSUF_FEE_P",			"Fee insufficient."										},

		{	temBAD_AMOUNT,			"temBAD_AMOUNT",			"Can only send positive amounts."						},
		{	temBAD_AUTH_MASTER,		"temBAD_AUTH_MASTER",		"Auth for unclaimed account needs correct master key."	},
		{	temBAD_EXPIRATION,		"temBAD_EXPIRATION",		"Malformed."											},
		{	temBAD_ISSUER,			"temBAD_ISSUER",			"Malformed."											},
		{	temBAD_OFFER,			"temBAD_OFFER",				"Malformed."											},
		{	temBAD_PATH,			"temBAD_PATH",				"Malformed."											},
		{	temBAD_PATH_LOOP,		"temBAD_PATH_LOOP",			"Malformed."											},
		{	temBAD_PUBLISH,			"temBAD_PUBLISH",			"Malformed: Bad publish."								},
		{	temBAD_TRANSFER_RATE,	"temBAD_TRANSFER_RATE",		"Malformed: Transfer rate must be >= 1.0"				},
		{	temBAD_SET_ID,			"temBAD_SET_ID",			"Malformed."											},
		{	temBAD_SEQUENCE,		"temBAD_SEQUENCE",			"Malformed: Sequence in not in the past."				},
		{	temDST_IS_SRC,			"temDST_IS_SRC",			"Destination may not be source."						},
		{	temDST_NEEDED,			"temDST_NEEDED",			"Destination not specified."							},
		{	temINSUF_FEE_P,			"temINSUF_FEE_P",			"Fee not allowed."										},
		{	temINVALID,				"temINVALID",				"The transaction is ill-formed."						},
		{	temINVALID_FLAG,		"temINVALID_FLAG",			"The transaction has an invalid flag."					},
		{	temREDUNDANT,			"temREDUNDANT",				"Sends same currency to self."							},
		{	temRIPPLE_EMPTY,		"temRIPPLE_EMPTY",			"PathSet with no paths."								},
		{	temUNCERTAIN,			"temUNCERTAIN",				"In process of determining result. Never returned."		},
		{	temUNKNOWN,				"temUNKNOWN",				"The transactions requires logic not implemented yet."	},

		{	tepPATH_DRY,			"tepPATH_DRY",				"Path could not send partial amount."					},
		{	tepPATH_PARTIAL,		"tepPATH_PARTIAL",			"Path could not send full amount."						},

		{	terDIR_FULL,			"terDIR_FULL",				"Can not add entry to full dir."						},
		{	terFUNDS_SPENT,			"terFUNDS_SPENT",			"Can't set password, password set funds already spent."	},
		{	terINSUF_FEE_B,			"terINSUF_FEE_B",			"Account balance can't pay fee."						},
		{	terNO_ACCOUNT,			"terNO_ACCOUNT",			"The source account does not exist."					},
		{	terNO_DST,				"terNO_DST",				"Destination does not exist. Send XRP to create it."	},
		{	terNO_DST_INSUF_XRP,	"terNO_DST_INSUF_XRP",		"Destination does not exist. Too little XRP sent to create it."		},
		{	terNO_LINE,				"terNO_LINE",				"No such line."											},
		{	terNO_LINE_REDUNDANT,	"terNO_LINE_REDUNDANT",		"Can't set non-existant line to default."				},
		{	terPRE_SEQ,				"terPRE_SEQ",				"Missing/inapplicable prior transaction."				},
		{	terSET_MISSING_DST,		"terSET_MISSING_DST",		"Can't set password, destination missing."				},
		{	terUNFUNDED,			"terUNFUNDED",				"Source account had insufficient balance for transaction."	},

		{	tesSUCCESS,				"tesSUCCESS",				"The transaction was applied."							},
	};

	int	iIndex	= NUMBER(transResultInfoA);

	while (iIndex-- && transResultInfoA[iIndex].terCode != terCode)
		;

	if (iIndex >= 0)
	{
		strToken	= transResultInfoA[iIndex].cpToken;
		strHuman	= transResultInfoA[iIndex].cpHuman;
	}

	return iIndex >= 0;
}

std::string transToken(TER terCode)
{
	std::string	strToken;
	std::string	strHuman;

	return transResultInfo(terCode, strToken, strHuman) ? strToken : "-";
}

std::string transHuman(TER terCode)
{
	std::string	strToken;
	std::string	strHuman;

	return transResultInfo(terCode, strToken, strHuman) ? strHuman : "-";
}

// vim:ts=4
