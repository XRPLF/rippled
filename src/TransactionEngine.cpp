//
// XXX Should make sure all fields and are recognized on a transactions.
// XXX Make sure fee is claimed for failed transactions.
// XXX Might uses an unordered set for vector.
//

#include "TransactionEngine.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <queue>

#include "../json/writer.h"

#include "Config.h"
#include "Log.h"
#include "TransactionFormats.h"
#include "utils.h"

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX		2
#define RIPPLE_PATHS_MAX	3

#define QUALITY_ONE			100000000	// 10e9
#define CURRENCY_XNS		uint160(0)
#define CURRENCY_ONE		uint160(1)	// Used as a place holder
#define ACCOUNT_XNS			uint160(0)
#define ACCOUNT_ONE			uint160(1)	// Used as a place holder

static STAmount saZero(CURRENCY_ONE, 0, 0);
static STAmount saOne(CURRENCY_ONE, 1, 0);

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		TransactionEngineResult	terCode;
		const char*				cpToken;
		const char*				cpHuman;
	} transResultInfoA[] = {
		{	tenBAD_ADD_AUTH,		"tenBAD_ADD_AUTH",			"Not authorized to add account."					},
		{	tenBAD_AMOUNT,			"tenBAD_AMOUNT",			"Can only send positive amounts."					},
		{	tenBAD_CLAIM_ID,		"tenBAD_CLAIM_ID",			"Malformed."										},
		{	tenBAD_EXPIRATION,		"tenBAD_EXPIRATION",		"Malformed."										},
		{	tenBAD_GEN_AUTH,		"tenBAD_GEN_AUTH",			"Not authorized to claim generator."				},
		{	tenBAD_ISSUER,			"tenBAD_ISSUER",			"Malformed."										},
		{	tenBAD_OFFER,			"tenBAD_OFFER",				"Malformed."										},
		{	tenBAD_PATH,			"tenBAD_PATH",				"Malformed: path."									},
		{	tenBAD_PATH_COUNT,		"tenBAD_PATH_COUNT",		"Malformed: too many paths."						},
		{	tenBAD_PUBLISH,			"tenBAD_PUBLISH",			"Malformed: bad publish."							},
		{	tenBAD_RIPPLE,			"tenBAD_RIPPLE",			"Ledger prevents ripple from succeeding."			},
		{	tenBAD_SET_ID,			"tenBAD_SET_ID",			"Malformed."										},
		{	tenCLAIMED,				"tenCLAIMED",				"Can not claim a previously claimed account."		},
		{	tenCREATED,				"tenCREATED",				"Can't add an already created account."				},
		{	tenCREATEXNS,			"tenCREATEXNS",				"Can not specify non XNS for Create."				},
		{	tenDST_IS_SRC,			"tenDST_IS_SRC",			"Destination may not be source."					},
		{	tenDST_NEEDED,			"tenDST_NEEDED",			"Destination not specified."						},
		{	tenEXPIRED,				"tenEXPIRED",				"Won't add an expired offer."						},
		{	tenEXPLICITXNS,			"tenEXPLICITXNS",			"XNS is used by default, don't specify it."			},
		{	tenFAILED,				"tenFAILED",				"Something broke horribly"							},
		{	tenGEN_IN_USE,			"tenGEN_IN_USE",			"Generator already in use."							},
		{	tenINSUF_FEE_P,			"tenINSUF_FEE_P",			"fee totally insufficient"							},
		{	tenINVALID,				"tenINVALID",				"The transaction is ill-formed"						},
		{	tenMSG_SET,				"tenMSG_SET",				"Can't change a message key."						},
		{	tenREDUNDANT,			"tenREDUNDANT",				"Sends same currency to self."						},
		{	tenRIPPLE_EMPTY,		"tenRIPPLE_EMPTY",			"PathSet with no paths."							},
		{	tenUNKNOWN,				"tenUNKNOWN",				"The transactions requires logic not implemented yet"	},
		{	terALREADY,				"terALREADY",				"The exact transaction was already in this ledger"	},
		{	terBAD_AUTH,			"terBAD_AUTH",				"Transaction's public key is not authorized."		},
		{	terBAD_AUTH_MASTER,		"terBAD_AUTH_MASTER",		"Auth for unclaimed account needs correct master key."	},
		{	terBAD_LEDGER,			"terBAD_LEDGER",			"Ledger in unexpected state."						},
		{	terBAD_RIPPLE,			"terBAD_RIPPLE",			"No ripple path can be satisfied."					},
		{	terBAD_SEQ,				"terBAD_SEQ",				"This sequence number should be zero for prepaid transactions."	},
		{	terCREATED,				"terCREATED",				"Can not create a previously created account."		},
		{	terDIR_FULL,			"terDIR_FULL",				"Can not add entry to full dir."					},
		{	terFUNDS_SPENT,			"terFUNDS_SPENT",			"Can't set password, password set funds already spent."	},
		{	terINSUF_FEE_B,			"terINSUF_FEE_B",			"Account balance can't pay fee"						},
		{	terINSUF_FEE_T,			"terINSUF_FEE_T",			"fee insufficient now (account doesn't exist, network load)"	},
		{	terNODE_NOT_FOUND,		"terNODE_NOT_FOUND",		"Can not delete a directory node."					},
		{	terNODE_NOT_MENTIONED,  "terNODE_NOT_MENTIONED",	"Could not remove node from a directory."			},
		{	terNODE_NO_ROOT,        "terNODE_NO_ROOT",			"Directory doesn't exist."							},
		{	terNO_ACCOUNT,			"terNO_ACCOUNT",			"The source account does not exist"					},
		{	terNO_DST,				"terNO_DST",				"The destination does not exist"					},
		{	terNO_LINE_NO_ZERO,		"terNO_LINE_NO_ZERO",		"Can't zero non-existant line, destination might make it."	},
		{	terNO_PATH,				"terNO_PATH",				"No path existed or met transaction/balance requirements"	},
		{	terOFFER_NOT_FOUND,		"terOFFER_NOT_FOUND",		"Can not cancel offer."								},
		{	terOVER_LIMIT,			"terOVER_LIMIT",			"Over limit."										},
		{	terPAST_LEDGER,			"terPAST_LEDGER",			"The transaction expired and can't be applied"		},
		{	terPAST_SEQ,			"terPAST_SEQ",				"This sequence number has already past"				},
		{	terPATH_EMPTY,			"terPATH_EMPTY",			"Path could not send partial amount."				},
		{	terPATH_PARTIAL,		"terPATH_PARTIAL",			"Path could not send full amount."					},
		{	terPRE_SEQ,				"terPRE_SEQ",				"Missing/inapplicable prior transaction"			},
		{	terSET_MISSING_DST,		"terSET_MISSING_DST",		"Can't set password, destination missing."			},
		{	terSUCCESS,				"terSUCCESS",				"The transaction was applied"						},
		{	terUNCLAIMED,			"terUNCLAIMED",				"Can not use an unclaimed account."					},
		{	terUNFUNDED,			"terUNFUNDED",				"Source account had insufficient balance for transaction."	},
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

STAmount TransactionEngine::rippleBalance(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saBalance;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	assert(sleRippleState);
	if (sleRippleState)
	{
		saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
		if (uToAccountID < uFromAccountID)
			saBalance.negate();
	}

	return saBalance;

}

STAmount TransactionEngine::rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saLimit;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	assert(sleRippleState);
	if (sleRippleState)
	{
		saLimit	= sleRippleState->getIValueFieldAmount(uToAccountID < uFromAccountID ? sfLowLimit : sfHighLimit);
	}

	return saLimit;

}

uint32 TransactionEngine::rippleTransfer(const uint160& uIssuerID)
{
	SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uIssuerID));

	return sleAccount->getIFieldPresent(sfTransferRate)
		? sleAccount->getIFieldU32(sfTransferRate)
		: QUALITY_ONE;
}

// XXX Might not need this, might store in nodes on calc reverse.
uint32 TransactionEngine::rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	uint32		uQualityIn	= QUALITY_ONE;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			uQualityIn	= sleRippleState->getIFieldU32(uToAccountID < uFromAccountID ? sfLowQualityIn : sfHighQualityIn);
		}
		else
		{
			assert(false);
		}
	}

	return uQualityIn;
}

uint32 TransactionEngine::rippleQualityOut(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	uint32		uQualityOut	= QUALITY_ONE;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			uQualityOut	= sleRippleState->getIFieldU32(uToAccountID < uFromAccountID ? sfLowQualityOut : sfHighQualityOut);
		}
		else
		{
			assert(false);
		}
	}

	return uQualityOut;
}

// Return how much of uIssuerID's uCurrencyID IOUs that uAccountID holds.  May be negative.
// <-- IOU's uAccountID has of uIssuerID
STAmount TransactionEngine::rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount			saBalance;
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uAccountID, uIssuerID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);

		if (uAccountID < uIssuerID)
			saBalance.negate();		// Put balance in low terms.
	}

	return saBalance;
}

// <-- saAmount: amount of uCurrencyID held by uAccountID. May be negative.
STAmount TransactionEngine::accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount	saAmount;

	if (uCurrencyID.isZero())
	{
		SLE::pointer		sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uAccountID));

		saAmount	= sleAccount->getIValueFieldAmount(sfBalance);

		Log(lsINFO) << "accountHolds: stamps: " << saAmount.getText();
	}
	else
	{
		saAmount	= rippleHolds(uAccountID, uCurrencyID, uIssuerID);

		Log(lsINFO) << "accountHolds: "
			<< saAmount.getFullText()
			<< " : "
			<< STAmount::createHumanCurrency(uCurrencyID)
			<< "/"
			<< NewcoinAddress::createHumanAccountID(uIssuerID);
	}

	return saAmount;
}

// Returns the funds available for uAccountID for a currency/issuer.
// Use when you need a default for rippling uAccountID's currency.
// --> saDefault/currency/issuer
// <-- saFunds: Funds available. May be negative.
//              If the issuer is the same as uAccountID, result is Default.
STAmount TransactionEngine::accountFunds(const uint160& uAccountID, const STAmount& saDefault)
{
	STAmount	saFunds;

	Log(lsINFO) << "accountFunds: uAccountID="
			<< NewcoinAddress::createHumanAccountID(uAccountID);
	Log(lsINFO) << "accountFunds: saDefault.isNative()=" << saDefault.isNative();
	Log(lsINFO) << "accountFunds: saDefault.getIssuer()="
			<< NewcoinAddress::createHumanAccountID(saDefault.getIssuer());

	if (!saDefault.isNative() && saDefault.getIssuer() == uAccountID)
	{
		saFunds	= saDefault;

		Log(lsINFO) << "accountFunds: offer funds: ripple self-funded: " << saFunds.getText();
	}
	else
	{
		saFunds	= accountHolds(uAccountID, saDefault.getCurrency(), saDefault.getIssuer());

		Log(lsINFO) << "accountFunds: offer funds: uAccountID ="
			<< NewcoinAddress::createHumanAccountID(uAccountID)
			<< " : "
			<< saFunds.getText()
			<< "/"
			<< saDefault.getHumanCurrency()
			<< "/"
			<< NewcoinAddress::createHumanAccountID(saDefault.getIssuer());
	}

	return saFunds;
}

// Calculate transit fee.
STAmount TransactionEngine::rippleTransfer(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount)
{
	STAmount	saTransitFee;

	if (uSenderID != uIssuerID && uReceiverID != uIssuerID)
	{
		uint32		uTransitRate	= rippleTransfer(uIssuerID);

		if (QUALITY_ONE != uTransitRate)
		{
			STAmount		saTransitRate(CURRENCY_ONE, uTransitRate, -9);

			saTransitFee	= STAmount::multiply(saAmount, saTransitRate, saAmount.getCurrency());
		}
	}

	return saTransitFee;
}

// Direct send w/o fees: redeeming IOUs and/or sending own IOUs.
void TransactionEngine::rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	uint160				uIssuerID		= saAmount.getIssuer();

	assert(uSenderID == uIssuerID || uReceiverID == uIssuerID);

	bool				bFlipped		= uSenderID > uReceiverID;
	uint256				uIndex			= Ledger::getRippleStateIndex(uSenderID, uReceiverID, saAmount.getCurrency());
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, uIndex);

	if (!sleRippleState)
	{
		Log(lsINFO) << "rippleCredit: Creating ripple line: " << uIndex.ToString();

		STAmount	saBalance	= saAmount;

		sleRippleState	= entryCreate(ltRIPPLE_STATE, uIndex);

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, uSenderID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uReceiverID);
	}
	else
	{
		STAmount	saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);

		if (!bFlipped)
			saBalance.negate();		// Put balance in low terms.

		saBalance	+= saAmount;

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);

		entryModify(sleRippleState);
	}
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer for receiver to get.
// <-- saActual: Amount actually sent.  Sender pay's fees.
STAmount TransactionEngine::rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	STAmount	saActual;
	uint160		uIssuerID	= saAmount.getIssuer();

	if (uSenderID == uIssuerID || uReceiverID == uIssuerID)
	{
		// Direct send: redeeming IOUs and/or sending own IOUs.
		rippleCredit(uSenderID, uReceiverID, saAmount);

		saActual	= saAmount;
	}
	else
	{
		// Sending 3rd party IOUs: transit.

		STAmount		saTransitFee	= rippleTransfer(uSenderID, uReceiverID, uIssuerID, saAmount);

		saActual	= saTransitFee.isZero() ? saAmount : saAmount+saTransitFee;

		saActual.setIssuer(uIssuerID);	// XXX Make sure this done in + above.

		rippleCredit(uIssuerID, uReceiverID, saAmount);
		rippleCredit(uSenderID, uIssuerID, saActual);
	}

	return saActual;
}

STAmount TransactionEngine::accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	STAmount	saActualCost;

	if (saAmount.isNative())
	{
		SLE::pointer		sleSender	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uSenderID));
		SLE::pointer		sleReceiver	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uReceiverID));

		Log(lsINFO) << str(boost::format("accountSend> %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender->getIValueFieldAmount(sfBalance)).getFullText()
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText()
			% saAmount.getFullText());

		sleSender->setIFieldAmount(sfBalance, sleSender->getIValueFieldAmount(sfBalance) - saAmount);
		sleReceiver->setIFieldAmount(sfBalance, sleReceiver->getIValueFieldAmount(sfBalance) + saAmount);

		Log(lsINFO) << str(boost::format("accountSend< %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender->getIValueFieldAmount(sfBalance)).getFullText()
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText()
			% saAmount.getFullText());

		entryModify(sleSender);
		entryModify(sleReceiver);

		saActualCost	= saAmount;
	}
	else
	{
		saActualCost	= rippleSend(uSenderID, uReceiverID, saAmount);
	}

	return saActualCost;
}

TransactionEngineResult TransactionEngine::offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID)
{
	uint64					uOwnerNode	= sleOffer->getIFieldU64(sfOwnerNode);
	TransactionEngineResult	terResult	= dirDelete(false, uOwnerNode, Ledger::getOwnerDirIndex(uOwnerID), uOfferIndex);

	if (terSUCCESS == terResult)
	{
		uint256		uDirectory	= sleOffer->getIFieldH256(sfBookDirectory);
		uint64		uBookNode	= sleOffer->getIFieldU64(sfBookNode);

		terResult	= dirDelete(false, uBookNode, uDirectory, uOfferIndex);
	}

	entryDelete(sleOffer);

	return terResult;
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TransactionEngineResult TransactionEngine::dirAdd(
	uint64&							uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	SLE::pointer		sleNode;
	STVector256			svIndexes;
	SLE::pointer		sleRoot		= entryCache(ltDIR_NODE, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		sleRoot		= entryCreate(ltDIR_NODE, uRootIndex);

		sleNode		= sleRoot;
		uNodeDir	= 0;
	}
	else
	{
		uNodeDir	= sleRoot->getIFieldU64(sfIndexPrevious);		// Get index to last directory node.

		if (uNodeDir)
		{
			// Try adding to last node.
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

			assert(sleNode);
		}
		else
		{
			// Try adding to root.  Didn't have a previous set to the last node.
			sleNode		= sleRoot;
		}

		svIndexes	= sleNode->getIFieldV256(sfIndexes);

		if (DIR_NODE_MAX != svIndexes.peekValue().size())
		{
			// Add to current node.
			entryModify(sleNode);
		}
		// Add to new node.
		else if (!++uNodeDir)
		{
			return terDIR_FULL;
		}
		else
		{
			// Have old last point to new node, if it was not root.
			if (uNodeDir == 1)
			{
				// Previous node is root node.

				sleRoot->setIFieldU64(sfIndexNext, uNodeDir);
			}
			else
			{
				// Previous node is not root node.

				SLE::pointer	slePrevious	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir-1));

				slePrevious->setIFieldU64(sfIndexNext, uNodeDir);
				entryModify(slePrevious);

				sleNode->setIFieldU64(sfIndexPrevious, uNodeDir-1);
			}

			// Have root point to new node.
			sleRoot->setIFieldU64(sfIndexPrevious, uNodeDir);
			entryModify(sleRoot);

			// Create the new node.
			sleNode		= entryCreate(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));
			svIndexes	= STVector256();
		}
	}

	svIndexes.peekValue().push_back(uLedgerIndex);	// Append entry.
	sleNode->setIFieldV256(sfIndexes, svIndexes);	// Save entry.

	Log(lsINFO) << "dirAdd:   creating: root: " << uRootIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Entry: " << uLedgerIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Node: " << strHex(uNodeDir);
	// Log(lsINFO) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();

	return terSUCCESS;
}

// -->    bKeepRoot: True, if we never completely clean up, after we overflow the root node.
// -->     uNodeDir: Node containing entry.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Ledger must be in a state for this to work.
TransactionEngineResult TransactionEngine::dirDelete(
	bool							bKeepRoot,
	const uint64&					uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	uint64				uNodeCur	= uNodeDir;
	SLE::pointer		sleNode		= entryCache(ltDIR_NODE, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

	assert(sleNode);

	if (!sleNode)
	{
		Log(lsWARNING) << "dirDelete: no such node";

		return terBAD_LEDGER;
	}

	STVector256						svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&			vuiIndexes	= svIndexes.peekValue();
	std::vector<uint256>::iterator	it;

	it = std::find(vuiIndexes.begin(), vuiIndexes.end(), uLedgerIndex);

	assert(vuiIndexes.end() != it);
	if (vuiIndexes.end() == it)
	{
		assert(false);

		Log(lsWARNING) << "dirDelete: no such entry";

		return terBAD_LEDGER;
	}

	// Remove the element.
	if (vuiIndexes.size() > 1)
		*it = vuiIndexes[vuiIndexes.size()-1];

	vuiIndexes.resize(vuiIndexes.size()-1);

	sleNode->setIFieldV256(sfIndexes, svIndexes);
	entryModify(sleNode);

	if (vuiIndexes.empty())
	{
		// May be able to delete nodes.
		uint64				uNodePrevious	= sleNode->getIFieldU64(sfIndexPrevious);
		uint64				uNodeNext		= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeCur)
		{
			// Just emptied root node.

			if (!uNodePrevious)
			{
				// Never overflowed the root node.  Delete it.
				entryDelete(sleNode);
			}
			// Root overflowed.
			else if (bKeepRoot)
			{
				// If root overflowed and not allowed to delete overflowed root node.

				nothing();
			}
			else if (uNodePrevious != uNodeNext)
			{
				// Have more than 2 nodes.  Can't delete root node.

				nothing();
			}
			else
			{
				// Have only a root node and a last node.
				SLE::pointer		sleLast	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));

				assert(sleLast);

				if (sleLast->getIFieldV256(sfIndexes).peekValue().empty())
				{
					// Both nodes are empty.

					entryDelete(sleNode);	// Delete root.
					entryDelete(sleLast);	// Delete last.
				}
				else
				{
					// Have an entry, can't delete root node.

					nothing();
				}
			}
		}
		// Just emptied a non-root node.
		else if (uNodeNext)
		{
			// Not root and not last node. Can delete node.

			SLE::pointer		slePrevious	= entryCache(ltDIR_NODE, uNodePrevious ? Ledger::getDirNodeIndex(uRootIndex, uNodePrevious) : uRootIndex);

			assert(slePrevious);

			SLE::pointer		sleNext		= entryCache(ltDIR_NODE, uNodeNext ? Ledger::getDirNodeIndex(uRootIndex, uNodeNext) : uRootIndex);

			assert(slePrevious);
			assert(sleNext);

			if (!slePrevious)
			{
				Log(lsWARNING) << "dirDelete: previous node is missing";

				return terBAD_LEDGER;
			}

			if (!sleNext)
			{
				Log(lsWARNING) << "dirDelete: next node is missing";

				return terBAD_LEDGER;
			}

			// Fix previous to point to its new next.
			slePrevious->setIFieldU64(sfIndexNext, uNodeNext);
			entryModify(slePrevious);

			// Fix next to point to its new previous.
			sleNext->setIFieldU64(sfIndexPrevious, uNodePrevious);
			entryModify(sleNext);
		}
		// Last node.
		else if (bKeepRoot || uNodePrevious)
		{
			// Not allowed to delete last node as root was overflowed.
			// Or, have pervious entries preventing complete delete.

			nothing();
		}
		else
		{
			// Last and only node besides the root.
			SLE::pointer			sleRoot	= entryCache(ltDIR_NODE, uRootIndex);

			assert(sleRoot);

			if (sleRoot->getIFieldV256(sfIndexes).peekValue().empty())
			{
				// Both nodes are empty.

				entryDelete(sleRoot);	// Delete root.
				entryDelete(sleNode);	// Delete last.
			}
			else
			{
				// Root has an entry, can't delete.

				nothing();
			}
		}
	}

	return terSUCCESS;
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool TransactionEngine::dirFirst(
	const uint256& uRootIndex,	// --> Root of directory.
	SLE::pointer& sleNode,		// <-- current node
	unsigned int& uDirEntry,	// <-- next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	sleNode		= entryCache(ltDIR_NODE, uRootIndex);
	uDirEntry	= 0;

	assert(sleNode);			// We never probe for directories.

	return TransactionEngine::dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool TransactionEngine::dirNext(
	const uint256& uRootIndex,	// --> Root of directory
	SLE::pointer& sleNode,		// <-> current node
	unsigned int& uDirEntry,	// <-> next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	STVector256				svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

	if (uDirEntry == vuiIndexes.size())
	{
		uint64				uNodeNext	= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeNext)
		{
			uEntryIndex.zero();

			return false;
		}
		else
		{
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));
			uDirEntry	= 0;

			return dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
		}
	}

	uEntryIndex	= vuiIndexes[uDirEntry++];

	return true;
}

// Set the authorized public key for an account.  May also set the generator map.
TransactionEngineResult	TransactionEngine::setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator)
{
	//
	// Verify that submitter knows the private key for the generator.
	// Otherwise, people could deny access to generators.
	//

	std::vector<unsigned char>	vucCipher		= txn.getITFieldVL(sfGenerator);
	std::vector<unsigned char>	vucPubKey		= txn.getITFieldVL(sfPubKey);
	std::vector<unsigned char>	vucSignature	= txn.getITFieldVL(sfSignature);
	NewcoinAddress				naAccountPublic	= NewcoinAddress::createAccountPublic(vucPubKey);

	if (!naAccountPublic.accountPublicVerify(Serializer::getSHA512Half(vucCipher), vucSignature))
	{
		Log(lsWARNING) << "createGenerator: bad signature unauthorized generator claim";

		return tenBAD_GEN_AUTH;
	}

	// Create generator.
	uint160				hGeneratorID	= naAccountPublic.getAccountID();

	SLE::pointer		sleGen			= entryCache(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));
	if (!sleGen)
	{
		// Create the generator.
		Log(lsTRACE) << "createGenerator: creating generator";

		sleGen			= entryCreate(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));

		sleGen->setIFieldVL(sfGenerator, vucCipher);
	}
	else if (bMustSetGenerator)
	{
		// Doing a claim.  Must set generator.
		// Generator is already in use.  Regular passphrases limited to one wallet.
		Log(lsWARNING) << "createGenerator: generator already in use";

		return tenGEN_IN_USE;
	}

	// Set the public key needed to use the account.
	uint160				uAuthKeyID		= bMustSetGenerator
											? hGeneratorID								// Claim
											: txn.getITFieldAccount(sfAuthorizedKey);	// PasswordSet

	mTxnAccount->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return terSUCCESS;
}

SLE::pointer TransactionEngine::entryCache(LedgerEntryType letType, const uint256& uIndex)
{
	SLE::pointer				sleEntry;

	if (!uIndex.isZero())
	{
		LedgerEntryAction action;
		sleEntry = mNodes.getEntry(uIndex, action);
		if (!sleEntry)
		{
			sleEntry = mLedger->getSLE(uIndex);
			if (sleEntry)
			{
				mNodes.entryCache(sleEntry);
				mOrigNodes.entryCache(sleEntry); // So the metadata code can compare to the original
			}
		}
		else if(action == taaDELETE)
			assert(false);
	}

	return sleEntry;
}

SLE::pointer TransactionEngine::entryCreate(LedgerEntryType letType, const uint256& uIndex)
{
	assert(!uIndex.isZero());

	SLE::pointer	sleNew	= boost::make_shared<SerializedLedgerEntry>(letType);
	sleNew->setIndex(uIndex);
	mNodes.entryCreate(sleNew);

	return sleNew;
}


void TransactionEngine::entryDelete(SLE::pointer sleEntry, bool unfunded)
{
	mNodes.entryDelete(sleEntry, unfunded);
}

void TransactionEngine::entryModify(SLE::pointer sleEntry)
{
	mNodes.entryModify(sleEntry);
}

void TransactionEngine::txnWrite()
{
	// Write back the account states and add the transaction to the ledger
	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
	{
		const SLE::pointer&	sleEntry	= it->second.mEntry;

		switch (it->second.mAction)
		{
			case taaNONE:
				assert(false);
				break;

			case taaCACHED:
				break;

			case taaCREATE:
				{
					Log(lsINFO) << "applyTransaction: taaCREATE: " << sleEntry->getText();

					if (mLedger->writeBack(lepCREATE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaMODIFY:
				{
					Log(lsINFO) << "applyTransaction: taaMODIFY: " << sleEntry->getText();

					if (mLedger->writeBack(lepNONE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaDELETE:
				{
					Log(lsINFO) << "applyTransaction: taaDELETE: " << sleEntry->getText();

					if (!mLedger->peekAccountStateMap()->delItem(it->first))
						assert(false);
				}
				break;
		}
	}
}

// This is for when a transaction fails from the issuer's point of view and the current changes need to be cleared so other
// actions can be applied to the ledger.
void TransactionEngine::entryReset()
{
	mNodes.setTo(mOrigNodes);
}

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params)
{
	Log(lsTRACE) << "applyTransaction>";
	assert(mLedger);
	mLedgerParentCloseTime	= mLedger->getParentCloseTimeNC();
	mNodes.init(txn.getTransactionID(), mLedger->getLedgerSeq());

#ifdef DEBUG
	if (1)
	{
		Serializer ser;
		txn.add(ser);
		SerializerIterator sit(ser);
		SerializedTransaction s2(sit);
		if (!s2.isEquivalent(txn))
		{
			Log(lsFATAL) << "Transaction serdes mismatch";
			Json::StyledStreamWriter ssw;
			ssw.write(Log(lsINFO).ref(), txn.getJson(0));
			ssw.write(Log(lsFATAL).ref(), s2.getJson(0));
			assert(false);
		}
	}
#endif

	TransactionEngineResult terResult = terSUCCESS;

	uint256 txID = txn.getTransactionID();
	if (!txID)
	{
		Log(lsWARNING) << "applyTransaction: invalid transaction id";

		terResult	= tenINVALID;
	}

	//
	// Verify transaction is signed properly.
	//

	// Extract signing key
	// Transactions contain a signing key.  This allows us to trivially verify a transaction has at least been properly signed
	// without going to disk.  Each transaction also notes a source account id.  This is used to verify that the signing key is
	// associated with the account.
	// XXX This could be a lot cleaner to prevent unnecessary copying.
	NewcoinAddress	naSigningPubKey;

	if (terSUCCESS == terResult)
		naSigningPubKey	= NewcoinAddress::createAccountPublic(txn.peekSigningPubKey());

	// Consistency: really signed.
	if ((terSUCCESS == terResult) && ((params & tepNO_CHECK_SIGN) == 0) && !txn.checkSign(naSigningPubKey))
	{
		Log(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

		terResult	= tenINVALID;
	}

	STAmount	saCost		= theConfig.FEE_DEFAULT;

	// Customize behavoir based on transaction type.
	if (terSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
			case ttPASSWORD_SET:
				saCost	= 0;
				break;

			case ttPAYMENT:
				if (txn.getFlags() & tfCreateAccount)
				{
					saCost	= theConfig.FEE_ACCOUNT_CREATE;
				}
				break;

			case ttNICKNAME_SET:
				{
					SLE::pointer		sleNickname		= entryCache(ltNICKNAME, txn.getITFieldH256(sfNickname));

					if (!sleNickname)
						saCost	= theConfig.FEE_NICKNAME_CREATE;
				}
				break;

			case ttACCOUNT_SET:
			case ttCREDIT_SET:
			case ttINVOICE:
			case ttOFFER_CREATE:
			case ttOFFER_CANCEL:
			case ttPASSWORD_FUND:
			case ttWALLET_ADD:
				nothing();
				break;

			case ttINVALID:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: ttINVALID transaction type";
				terResult = tenINVALID;
				break;

			default:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
				terResult = tenUNKNOWN;
				break;
		}
	}

	STAmount saPaid = txn.getTransactionFee();

	if (terSUCCESS == terResult && (params & tepNO_CHECK_FEE) == tepNONE)
	{
		if (!saCost.isZero())
		{
			if (saPaid < saCost)
			{
				Log(lsINFO) << "applyTransaction: insufficient fee";

				terResult	= tenINSUF_FEE_P;
			}
		}
		else
		{
			if (!saPaid.isZero())
			{
				// Transaction is malformed.
				Log(lsWARNING) << "applyTransaction: fee not allowed";

				terResult	= tenINSUF_FEE_P;
			}
		}
	}

	// Get source account ID.
	mTxnAccountID	= txn.getSourceAccount().getAccountID();
	if (terSUCCESS == terResult && !mTxnAccountID)
	{
		Log(lsWARNING) << "applyTransaction: bad source id";

		terResult	= tenINVALID;
	}

	if (terSUCCESS != terResult)
		return terResult;

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	mTxnAccount			= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(mTxnAccountID));

	// Find source account
	// If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probablistic.

	STAmount			saSrcBalance;
	uint32				t_seq			= txn.getSequence();
	bool				bHaveAuthKey	= false;

	if (!mTxnAccount)
	{
		Log(lsTRACE) << str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		terResult			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);
		bHaveAuthKey	= mTxnAccount->getIFieldPresent(sfAuthorizedKey);
	}

	// Check if account claimed.
	if (terSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				if (bHaveAuthKey)
				{
					Log(lsWARNING) << "applyTransaction: Account already claimed.";

					terResult	= tenCLAIMED;
				}
				break;

			default:
				nothing();
				break;
		}
	}

	// Consistency: Check signature
	if (terSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != mTxnAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= tenBAD_CLAIM_ID;
				}
				break;

			case ttPASSWORD_SET:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != mTxnAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= tenBAD_SET_ID;
				}
				break;

			default:
				// Verify the transaction's signing public key is the key authorized for signing.
				if (bHaveAuthKey && naSigningPubKey.getAccountID() == mTxnAccount->getIValueFieldAccount(sfAuthorizedKey).getAccountID())
				{
					// Authorized to continue.
					nothing();
				}
				else if (naSigningPubKey.getAccountID() == mTxnAccountID)
				{
					// Authorized to continue.
					nothing();
				}
				else if (bHaveAuthKey)
				{
					Log(lsINFO) << "applyTransaction: Delay: Not authorized to use account.";

					terResult	= terBAD_AUTH;
				}
				else
				{
					Log(lsINFO) << "applyTransaction: Invalid: Not authorized to use account.";

					terResult	= terBAD_AUTH_MASTER;
				}
				break;
		}
	}

	// Deduct the fee, so it's not available during the transaction.
	// Will only write the account back, if the transaction succeeds.
	if (terSUCCESS != terResult || saCost.isZero())
	{
		nothing();
	}
	else if (saSrcBalance < saPaid)
	{
		Log(lsINFO)
			<< str(boost::format("applyTransaction: Delay: insufficent balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText());

		terResult	= terINSUF_FEE_B;
	}
	else
	{
		mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (terSUCCESS != terResult)
	{
		nothing();
	}
	else if (!saCost.isZero())
	{
		uint32 a_seq = mTxnAccount->getIFieldU32(sfSequence);

		Log(lsTRACE) << "Aseq=" << a_seq << ", Tseq=" << t_seq;

		if (t_seq != a_seq)
		{
			if (a_seq < t_seq)
			{
				Log(lsINFO) << "applyTransaction: future sequence number";

				terResult	= terPRE_SEQ;
			}
			else if (mLedger->hasTransaction(txID))
			{
				Log(lsWARNING) << "applyTransaction: duplicate sequence number";

				terResult	= terALREADY;
			}
			else
			{
				Log(lsWARNING) << "applyTransaction: past sequence number";

				terResult	= terPAST_SEQ;
			}
		}
		else
		{
			mTxnAccount->setIFieldU32(sfSequence, t_seq + 1);
		}
	}
	else
	{
		Log(lsINFO) << "applyTransaction: Zero cost transaction";

		if (t_seq)
		{
			Log(lsINFO) << "applyTransaction: bad sequence for pre-paid transaction";

			terResult	= terPAST_SEQ;
		}
	}

	if (terSUCCESS == terResult)
	{
		entryModify(mTxnAccount);
		mOrigNodes = mNodes.duplicate();

		switch (txn.getTxnType())
		{
			case ttACCOUNT_SET:
				terResult = doAccountSet(txn);
				break;

			case ttCLAIM:
				terResult = doClaim(txn);
				break;

			case ttCREDIT_SET:
				terResult = doCreditSet(txn);
				break;

			case ttINVALID:
				Log(lsINFO) << "applyTransaction: invalid type";
				terResult = tenINVALID;
				break;

			case ttINVOICE:
				terResult = doInvoice(txn);
				break;

			case ttOFFER_CREATE:
				terResult = doOfferCreate(txn);
				break;

			case ttOFFER_CANCEL:
				terResult = doOfferCancel(txn);
				break;

			case ttNICKNAME_SET:
				terResult = doNicknameSet(txn);
				break;

			case ttPASSWORD_FUND:
				terResult = doPasswordFund(txn);
				break;

			case ttPASSWORD_SET:
				terResult = doPasswordSet(txn);
				break;

			case ttPAYMENT:
				terResult = doPayment(txn);
				break;

			case ttWALLET_ADD:
				terResult = doWalletAdd(txn);
				break;

			default:
				terResult = tenUNKNOWN;
				break;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	transResultInfo(terResult, strToken, strHuman);

	Log(lsINFO) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

	if (terSUCCESS == terResult)
	{
		txnWrite();

		Serializer s;

		txn.add(s);

		// XXX add failed status too
		// XXX do fees as need.
		if (!mLedger->addTransaction(txID, s))
			assert(false);

		if ((params & tepUPDATE_TOTAL) != tepNONE)
			mLedger->destroyCoins(saPaid.getNValue());
	}

	mTxnAccount	= SLE::pointer();
	mNodes.clear();
	mOrigNodes.clear();
	mUnfunded.clear();

	return terResult;
}

TransactionEngineResult TransactionEngine::doAccountSet(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doAccountSet>";

	//
	// EmailHash
	//

	if (txn.getITFieldPresent(sfEmailHash))
	{
		uint128		uHash	= txn.getITFieldH128(sfEmailHash);

		if (uHash.isZero())
		{
			Log(lsINFO) << "doAccountSet: unset email hash";

			mTxnAccount->makeIFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set email hash";

			mTxnAccount->setIFieldH128(sfEmailHash, uHash);
		}
	}

	//
	// WalletLocator
	//

	if (txn.getITFieldPresent(sfWalletLocator))
	{
		uint256		uHash	= txn.getITFieldH256(sfWalletLocator);

		if (uHash.isZero())
		{
			Log(lsINFO) << "doAccountSet: unset wallet locator";

			mTxnAccount->makeIFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set wallet locator";

			mTxnAccount->setIFieldH256(sfWalletLocator, uHash);
		}
	}

	//
	// MessageKey
	//

	if (!txn.getITFieldPresent(sfMessageKey))
	{
		nothing();
	}
	else if (mTxnAccount->getIFieldPresent(sfMessageKey))
	{
		Log(lsINFO) << "doAccountSet: can not change message key";

		return tenMSG_SET;
	}
	else
	{
		Log(lsINFO) << "doAccountSet: set message key";

		mTxnAccount->setIFieldVL(sfMessageKey, txn.getITFieldVL(sfMessageKey));
	}

	//
	// Domain
	//

	if (txn.getITFieldPresent(sfDomain))
	{
		std::vector<unsigned char>	vucDomain	= txn.getITFieldVL(sfDomain);

		if (vucDomain.empty())
		{
			Log(lsINFO) << "doAccountSet: unset domain";

			mTxnAccount->makeIFieldAbsent(sfDomain);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set domain";

			mTxnAccount->setIFieldVL(sfDomain, vucDomain);
		}
	}

	//
	// TransferRate
	//

	if (txn.getITFieldPresent(sfTransferRate))
	{
		uint32		uRate	= txn.getITFieldU32(sfTransferRate);

		if (!uRate)
		{
			Log(lsINFO) << "doAccountSet: unset transfer rate";

			mTxnAccount->makeIFieldAbsent(sfTransferRate);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set transfer rate";

			mTxnAccount->setIFieldU32(sfTransferRate, uRate);
		}
	}

	//
	// PublishHash && PublishSize
	//

	bool	bPublishHash	= txn.getITFieldPresent(sfPublishHash);
	bool	bPublishSize	= txn.getITFieldPresent(sfPublishSize);

	if (bPublishHash ^ bPublishSize)
	{
		Log(lsINFO) << "doAccountSet: bad publish";

		return tenBAD_PUBLISH;
	}
	else if (bPublishHash && bPublishSize)
	{
		uint256		uHash	= txn.getITFieldH256(sfPublishHash);
		uint32		uSize	= txn.getITFieldU32(sfPublishSize);

		if (uHash.isZero())
		{
			Log(lsINFO) << "doAccountSet: unset publish";

			mTxnAccount->makeIFieldAbsent(sfPublishHash);
			mTxnAccount->makeIFieldAbsent(sfPublishSize);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set publish";

			mTxnAccount->setIFieldH256(sfPublishHash, uHash);
			mTxnAccount->setIFieldU32(sfPublishSize, uSize);
		}
	}

	Log(lsINFO) << "doAccountSet<";

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doClaim(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doClaim>";

	TransactionEngineResult	terResult	= setAuthorized(txn, true);

	Log(lsINFO) << "doClaim<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doCreditSet(const SerializedTransaction& txn)
{
	TransactionEngineResult	terResult	= terSUCCESS;
	Log(lsINFO) << "doCreditSet>";

	// Check if destination makes sense.
	uint160				uDstAccountID	= txn.getITFieldAccount(sfDestination);

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Destination account not specifed.";

		return tenDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Can not extend credit to self.";

		return tenDST_IS_SRC;
	}

	SLE::pointer		sleDst		= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		Log(lsINFO) << "doCreditSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	bool				bFlipped		= mTxnAccountID > uDstAccountID;
	uint32				uFlags			= bFlipped ? lsfLowIndexed : lsfHighIndexed;
	bool				bLimitAmount	= txn.getITFieldPresent(sfLimitAmount);
	STAmount			saLimitAmount	= bLimitAmount ? txn.getITFieldAmount(sfLimitAmount) : STAmount();
	bool				bQualityIn		= txn.getITFieldPresent(sfQualityIn);
	uint32				uQualityIn		= bQualityIn ? txn.getITFieldU32(sfQualityIn) : 0;
	bool				bQualityOut		= txn.getITFieldPresent(sfQualityOut);
	uint32				uQualityOut		= bQualityIn ? txn.getITFieldU32(sfQualityOut) : 0;
	uint160				uCurrencyID		= saLimitAmount.getCurrency();
	STAmount			saBalance(uCurrencyID);
	bool				bAddIndex		= false;
	bool				bDelIndex		= false;

	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		// A line exists in one or more directions.
#if 0
		if (saLimitAmount.isZero())
		{
			// Zeroing line.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bLow			= uLowID == uSrcAccountID;
			bool		bHigh			= uLowID == uDstAccountID;
			bool		bBalanceZero	= sleRippleState->getIValueFieldAmount(sfBalance).isZero();
			STAmount	saDstLimit		= sleRippleState->getIValueFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);
			bool		bDstLimitZero	= saDstLimit.isZero();

			assert(bLow || bHigh);

			if (bBalanceZero && bDstLimitZero)
			{
				// Zero balance and eliminating last limit.

				bDelIndex	= true;
				terResult	= dirDelete(false, uSrcRef, Ledger::getRippleDirIndex(mTxnAccountID), sleRippleState->getIndex());
			}
		}
#endif

		if (!bDelIndex)
		{
			if (bLimitAmount)
				sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit: sfLowLimit , saLimitAmount);

			if (!bQualityIn)
			{
				nothing();
			}
			else if (uQualityIn)
			{
				sleRippleState->setIFieldU32(bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);
			}
			else
			{
				sleRippleState->makeIFieldAbsent(bFlipped ? sfLowQualityIn : sfHighQualityIn);
			}

			if (!bQualityOut)
			{
				nothing();
			}
			else if (uQualityOut)
			{
				sleRippleState->setIFieldU32(bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);
			}
			else
			{
				sleRippleState->makeIFieldAbsent(bFlipped ? sfLowQualityOut : sfHighQualityOut);
			}

			bAddIndex		= !(sleRippleState->getFlags() & uFlags);

			if (bAddIndex)
				sleRippleState->setFlag(uFlags);

			entryModify(sleRippleState);
		}

		Log(lsINFO) << "doCreditSet: Modifying ripple line: bAddIndex=" << bAddIndex << " bDelIndex=" << bDelIndex;
	}
	// Line does not exist.
	else if (saLimitAmount.isZero())
	{
		Log(lsINFO) << "doCreditSet: Redundant: Setting non-existant ripple line to 0.";

		return terNO_LINE_NO_ZERO;
	}
	else
	{
		// Create a new ripple line.
		STAmount		saZero(uCurrencyID);

		bAddIndex		= true;
		sleRippleState	= entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doCreditSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFlag(uFlags);
		sleRippleState->setIFieldAmount(sfBalance, saZero);	// Zero balance in currency.
		sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAmount);
		sleRippleState->setIFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, saZero);
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, mTxnAccountID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uDstAccountID);
		if (uQualityIn)
			sleRippleState->setIFieldU32(bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);
		if (uQualityOut)
			sleRippleState->setIFieldU32(bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);
	}

	if (bAddIndex)
	{
		uint64			uSrcRef;	// Ignored, ripple_state dirs never delete.

		// XXX Make dirAdd more flexiable to take vector.
		terResult	= dirAdd(uSrcRef, Ledger::getRippleDirIndex(mTxnAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doNicknameSet(const SerializedTransaction& txn)
{
	std::cerr << "doNicknameSet>" << std::endl;

	uint256				uNickname		= txn.getITFieldH256(sfNickname);
	bool				bMinOffer		= txn.getITFieldPresent(sfMinimumOffer);
	STAmount			saMinOffer		= bMinOffer ? txn.getITFieldAmount(sfAmount) : STAmount();

	SLE::pointer		sleNickname		= entryCache(ltNICKNAME, uNickname);

	if (sleNickname)
	{
		// Edit old entry.
		sleNickname->setIFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && !saMinOffer.isZero())
		{
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
		}
		else
		{
			sleNickname->makeIFieldAbsent(sfMinimumOffer);
		}

		entryModify(sleNickname);
	}
	else
	{
		// Make a new entry.
		// XXX Need to include authorization limiting for first year.

		sleNickname	= entryCreate(ltNICKNAME, Ledger::getNicknameIndex(uNickname));

		std::cerr << "doNicknameSet: Creating nickname node: " << sleNickname->getIndex().ToString() << std::endl;

		sleNickname->setIFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && !saMinOffer.isZero())
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordFund(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordFund>" << std::endl;

	uint160				uDstAccountID	= txn.getITFieldAccount(sfDestination);
	SLE::pointer		sleDst			= mTxnAccountID == uDstAccountID
											? mTxnAccount
											: entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		std::cerr << "doPasswordFund: Delay transaction: Destination account does not exist." << std::endl;

		return terSET_MISSING_DST;
	}

	if (sleDst->getFlags() & lsfPasswordSpent)
	{
		sleDst->clearFlag(lsfPasswordSpent);

		std::cerr << "doPasswordFund: Clearing spent." << sleDst->getFlags() << std::endl;

		if (mTxnAccountID != uDstAccountID) {
			std::cerr << "doPasswordFund: Destination modified." << std::endl;

			entryModify(sleDst);
		}
	}

	std::cerr << "doPasswordFund<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordSet(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordSet>" << std::endl;

	if (mTxnAccount->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	mTxnAccount->setFlag(lsfPasswordSpent);

	TransactionEngineResult	terResult	= setAuthorized(txn, false);

	std::cerr << "doPasswordSet<" << std::endl;

	return terResult;
}

#if 0
// XXX Need to adjust for fees.
// Find offers to satisfy pnDst.
// - Does not adjust any balances as there is at least a forward pass to come.
// --> pnDst.saWanted: currency and amount wanted
// --> pnSrc.saIOURedeem.mCurrency: use this before saIOUIssue, limit to use.
// --> pnSrc.saIOUIssue.mCurrency: use this after saIOURedeem, limit to use.
// <-- pnDst.saReceive
// <-- pnDst.saIOUForgive
// <-- pnDst.saIOUAccept
// <-- terResult : terSUCCESS = no error and if !bAllowPartial complelely satisfied wanted.
// <-> usOffersDeleteAlways:
// <-> usOffersDeleteOnSuccess:
TransactionEngineResult calcOfferFill(paymentNode& pnSrc, paymentNode& pnDst, bool bAllowPartial)
{
	TransactionEngineResult	terResult;

	if (pnDst.saWanted.isNative())
	{
		// Transfer stamps.

		STAmount	saSrcFunds	= pnSrc.saAccount->accountHolds(pnSrc.saAccount, uint160(0), uint160(0));

		if (saSrcFunds && (bAllowPartial || saSrcFunds > pnDst.saWanted))
		{
			pnSrc.saSend	= min(saSrcFunds, pnDst.saWanted);
			pnDst.saReceive	= pnSrc.saSend;
		}
		else
		{
			terResult	= terINSUF_PATH;
		}
	}
	else
	{
		// Ripple funds.

		// Redeem to limit.
		terResult	= calcOfferFill(
			accountHolds(pnSrc.saAccount, pnDst.saWanted.getCurrency(), pnDst.saWanted.getIssuer()),
			pnSrc.saIOURedeem,
			pnDst.saIOUForgive,
			bAllowPartial);

		if (terSUCCESS == terResult)
		{
			// Issue to wanted.
			terResult	= calcOfferFill(
				pnDst.saWanted,		// As much as wanted is available, limited by credit limit.
				pnSrc.saIOUIssue,
				pnDst.saIOUAccept,
				bAllowPartial);
		}

		if (terSUCCESS == terResult && !bAllowPartial)
		{
			STAmount	saTotal	= pnDst.saIOUForgive	+ pnSrc.saIOUAccept;

			if (saTotal != saWanted)
				terResult	= terINSUF_PATH;
		}
	}

	return terResult;
}
#endif

#if 0
// Get the next offer limited by funding.
// - Stop when becomes unfunded.
void TransactionEngine::calcOfferBridgeNext(
	const uint256&		uBookRoot,		// --> Which order book to look in.
	const uint256&		uBookEnd,		// --> Limit of how far to look.
	uint256&			uBookDirIndex,	// <-> Current directory. <-- 0 = no offer available.
	uint64&				uBookDirNode,	// <-> Which node. 0 = first.
	unsigned int&		uBookDirEntry,	// <-> Entry in node. 0 = first.
	STAmount&			saOfferIn,		// <-- How much to pay in, fee inclusive, to get saOfferOut out.
	STAmount&			saOfferOut		// <-- How much offer pays out.
	)
{
	saOfferIn		= 0;	// XXX currency & issuer
	saOfferOut		= 0;	// XXX currency & issuer

	bool			bDone	= false;

	while (!bDone)
	{
		uint256			uOfferIndex;

		// Get uOfferIndex.
		dirNext(uBookRoot, uBookEnd, uBookDirIndex, uBookDirNode, uBookDirEntry, uOfferIndex);

		SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

		uint160			uOfferOwnerID	= sleOffer->getIValueFieldAccount(sfAccount).getAccountID();
		STAmount		saOfferPays		= sleOffer->getIValueFieldAmount(sfTakerGets);
		STAmount		saOfferGets		= sleOffer->getIValueFieldAmount(sfTakerPays);

		if (sleOffer->getIFieldPresent(sfGetsIssuer))
			saOfferPays.setIssuer(sleOffer->getIValueFieldAccount(sfGetsIssuer).getAccountID());

		if (sleOffer->getIFieldPresent(sfPaysIssuer))
			saOfferGets.setIssuer(sleOffer->getIValueFieldAccount(sfPaysIssuer).getAccountID());

		if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
		{
			// Offer is expired.
			Log(lsINFO) << "calcOfferFirst: encountered expired offer";
		}
		else
		{
			STAmount		saOfferFunds	= accountFunds(uOfferOwnerID, saOfferPays);
			// Outbound fees are paid by offer owner.
			// XXX Calculate outbound fee rate.

			if (saOfferPays.isNative())
			{
				// No additional fees for stamps.

				nothing();
			}
			else if (saOfferPays.getIssuer() == uOfferOwnerID)
			{
				// Offerer is issue own IOUs.
				// No fees at this exact point, XXX receiving node may charge a fee.
				// XXX Make sure has a credit line with receiver, limit by credit line.

				nothing();
				// XXX Broken - could be issuing or redeeming or both.
			}
			else
			{
				// Offer must be redeeming IOUs.

				// No additional 
				// XXX Broken
			}

			if (!saOfferFunds.isPositive())
			{
				// Offer is unfunded.
				Log(lsINFO) << "calcOfferFirst: offer unfunded: delete";
			}
			else if (saOfferFunds >= saOfferPays)
			{
				// Offer fully funded.

				// Account transfering funds in to offer always pays inbound fees.

				saOfferIn	= saOfferGets;	// XXX Add in fees?

				saOfferOut	= saOfferPays;

				bDone		= true;
			}
			else
			{
				// Offer partially funded.

				// saOfferIn/saOfferFunds = saOfferGets/saOfferPays
				// XXX Round such that all saOffer funds are exhausted.
				saOfferIn	= (saOfferFunds*saOfferGets)/saOfferPays; // XXX Add in fees?
				saOfferOut	= saOfferFunds;

				bDone		= true;
			}
		}

		if (!bDone)
		{
			// mUnfunded.insert(uOfferIndex);
		}
	}
	while (bNext);
}
#endif

// <-- bSuccess: false= no transfer
// XXX Make sure missing ripple path is addressed cleanly.
bool TransactionEngine::calcNodeOfferRev(
	unsigned int		uIndex,				// 0 < uIndex < uLast-1
	PathState::pointer	pspCur,
	bool				bMultiQuality
	)
{
	bool			bSuccess		= false;

	paymentNode&	prvPN			= pspCur->vpnNodes[uIndex-1];
	paymentNode&	curPN			= pspCur->vpnNodes[uIndex];
	paymentNode&	nxtPN			= pspCur->vpnNodes[uIndex+1];

	uint160&		uPrvCurrencyID	= prvPN.uCurrencyID;
	uint160&		uPrvIssuerID	= prvPN.uIssuerID;
	uint160&		uCurCurrencyID	= curPN.uCurrencyID;
	uint160&		uCurIssuerID	= curPN.uIssuerID;
	uint160&		uNxtCurrencyID	= nxtPN.uCurrencyID;
	uint160&		uNxtIssuerID	= nxtPN.uIssuerID;
	uint160&		uNxtAccountID	= nxtPN.uAccountID;

	STAmount		saTransferRate	= STAmount(CURRENCY_ONE, rippleTransfer(uCurIssuerID), -9);

	uint256			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
	uint256			uDirectEnd		= Ledger::getQualityNext(uDirectTip);
	bool			bAdvance		= !entryCache(ltDIR_NODE, uDirectTip);

	STAmount&		saPrvDlvReq		= prvPN.saRevDeliver;
	STAmount		saPrvDlvAct;

	STAmount&		saCurDlvReq		= curPN.saRevDeliver;	// Reverse driver.
	STAmount		saCurDlvAct;

	while (!!uDirectTip	// Have a quality.
		&& saCurDlvAct != saCurDlvReq)
	{
		// Get next quality.
		if (bAdvance)
		{
			uDirectTip		= mLedger->getNextLedgerIndex(uDirectTip, uDirectEnd);
		}
		else
		{
			bAdvance		= true;
		}

		if (!!uDirectTip)
		{
			// Do a directory.
			// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
			SLE::pointer	sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			STAmount		saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip), uCurCurrencyID);	// For correct ratio
			unsigned int	uEntry			= 0;
			uint256			uCurIndex;

			while (saCurDlvReq != saCurDlvAct	// Have not met request.
				&& dirNext(uDirectTip, sleDirectDir, uEntry, uCurIndex))
			{
				SLE::pointer	sleCurOfr			= entryCache(ltOFFER, uCurIndex);
				uint160			uCurOfrAccountID	= sleCurOfr->getIValueFieldAccount(sfAccount).getAccountID();
				STAmount		saCurOfrOutReq		= sleCurOfr->getIValueFieldAmount(sfTakerGets);
				STAmount		saCurOfrIn			= sleCurOfr->getIValueFieldAmount(sfTakerPays);
					// XXX Move issuer into STAmount
					if (sleCurOfr->getIFieldPresent(sfGetsIssuer))
						saCurOfrOutReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfGetsIssuer).getAccountID());

					if (sleCurOfr->getIFieldPresent(sfPaysIssuer))
						saCurOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

				STAmount		saCurOfrFunds		= accountFunds(uCurOfrAccountID, saCurOfrOutReq);	// Funds left.

				// XXX Offer is also unfunded if funding source previously mentioned.
				if (!saCurOfrFunds)
				{
					// Offer is unfunded.
					Log(lsINFO) << "calcNodeOffer: encountered unfunded offer";

					// XXX Mark unfunded.
				}
				else if (sleCurOfr->getIFieldPresent(sfExpiration) && sleCurOfr->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
				{
					// Offer is expired.
					Log(lsINFO) << "calcNodeOffer: encountered expired offer";

					// XXX Mark unfunded.
				}
				else if (!!uNxtAccountID)
				{
					// Next is an account.

					STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtAccountID == uCurIssuerID
												? saOne
												: saTransferRate;
					bool		bFee		= saFeeRate != saOne;

					STAmount	saOutBase	= MIN(saCurOfrOutReq, saCurDlvReq-saCurDlvAct);	// Limit offer out by needed.
					STAmount	saOutCost	= MIN(
												bFee
													? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
													: saOutBase,
												saCurOfrFunds);								// Limit cost by fees & funds.
					STAmount	saOutDlvAct	= bFee
													? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
													: saOutCost;							// Out amount after fees.
					STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

					saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
					saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
				}
				else
				{
					// Next is an offer.

					uint256			uNxtTip		= Ledger::getBookBase(uCurCurrencyID, uCurIssuerID, uNxtCurrencyID, uNxtIssuerID);
					uint256			uNxtEnd		= Ledger::getQualityNext(uNxtTip);
					bool			bNxtAdvance	= !entryCache(ltDIR_NODE, uNxtTip);

					while (!!uNxtTip					// Have a quality.
						&& saCurDlvAct != saCurDlvReq)	// Have more to do.
					{
						if (bNxtAdvance)
						{
							uNxtTip		= mLedger->getNextLedgerIndex(uNxtTip, uNxtEnd);
						}
						else
						{
							bNxtAdvance		= true;
						}

						if (!!uNxtTip)
						{
							// Do a directory.
							// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
							SLE::pointer	sleNxtDir	= entryCache(ltDIR_NODE, uNxtTip);
// ??? STAmount		saOfrRate		= STAmount::setRate(STAmount::getQuality(uNxtTip), uCurCurrencyID);	// For correct ratio
							unsigned int	uEntry			= 0;
							uint256			uNxtIndex;

							while (saCurDlvReq != saCurDlvAct	// Have not met request.
								&& dirNext(uNxtTip, sleNxtDir, uEntry, uNxtIndex))
							{
								// YYY This could combine offers with the same fee before doing math.
								SLE::pointer	sleNxtOfr			= entryCache(ltOFFER, uNxtIndex);
								uint160			uNxtOfrAccountID	= sleNxtOfr->getIValueFieldAccount(sfAccount).getAccountID();
								STAmount		saNxtOfrIn			= sleNxtOfr->getIValueFieldAmount(sfTakerPays);
									// XXX Move issuer into STAmount
									if (sleNxtOfr->getIFieldPresent(sfPaysIssuer))
										saNxtOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

								STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtOfrAccountID == uCurIssuerID
															? saOne
															: saTransferRate;
								bool		bFee		= saFeeRate != saOne;

								STAmount	saOutBase	= MIN(saCurOfrOutReq, saCurDlvReq-saCurDlvAct);	// Limit offer out by needed.
											saOutBase	= MIN(saOutBase, saNxtOfrIn);					// Limit offer out by supplying offer.
								STAmount	saOutCost	= MIN(
															bFee
																? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
																: saOutBase,
															saCurOfrFunds);								// Limit cost by fees & funds.
								STAmount	saOutDlvAct	= bFee
															? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
															: saOutCost;								// Out amount after fees.
								STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

								saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
								saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
							}
						}

						// Do another nxt directory iff bMultiQuality
						if (!bMultiQuality)
							uNxtTip	= 0;
					}
				}
			}
		}

		// Do another cur directory iff bMultiQuality
		if (!bMultiQuality)
			uDirectTip	= 0;
	}

	if (saPrvDlvAct)
	{
		saPrvDlvReq	= saPrvDlvAct;	// Adjust request.
		bSuccess	= true;
	}

	return bSuccess;
}

bool TransactionEngine::calcNodeOfferFwd(
	unsigned int		uIndex,				// 0 < uIndex < uLast-1
	PathState::pointer	pspCur,
	bool				bMultiQuality
	)
{
	bool			bSuccess		= false;

	paymentNode&	prvPN			= pspCur->vpnNodes[uIndex-1];
	paymentNode&	curPN			= pspCur->vpnNodes[uIndex];
	paymentNode&	nxtPN			= pspCur->vpnNodes[uIndex+1];

	uint160&		uPrvCurrencyID	= prvPN.uCurrencyID;
	uint160&		uPrvIssuerID	= prvPN.uIssuerID;
	uint160&		uCurCurrencyID	= curPN.uCurrencyID;
	uint160&		uCurIssuerID	= curPN.uIssuerID;
	uint160&		uNxtCurrencyID	= nxtPN.uCurrencyID;
	uint160&		uNxtIssuerID	= nxtPN.uIssuerID;

	uint160&		uNxtAccountID	= nxtPN.uAccountID;
	STAmount		saTransferRate	= STAmount(CURRENCY_ONE, rippleTransfer(uCurIssuerID), -9);

	uint256			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
	uint256			uDirectEnd		= Ledger::getQualityNext(uDirectTip);
	bool			bAdvance		= !entryCache(ltDIR_NODE, uDirectTip);

	STAmount&		saPrvDlvReq		= prvPN.saFwdDeliver;	// Forward driver.
	STAmount		saPrvDlvAct;

	STAmount&		saCurDlvReq		= curPN.saFwdDeliver;
	STAmount		saCurDlvAct;

	while (!!uDirectTip										// Have a quality.
		&& saPrvDlvAct != saPrvDlvReq)
	{
		// Get next quality.
		if (bAdvance)
		{
			uDirectTip		= mLedger->getNextLedgerIndex(uDirectTip, uDirectEnd);
		}
		else
		{
			bAdvance		= true;
		}

		if (!!uDirectTip)
		{
			// Do a directory.
			// - Drive on computing saPrvDlvAct to derive saCurDlvAct.
			SLE::pointer	sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			STAmount		saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip), uCurCurrencyID);	// For correct ratio
			unsigned int	uEntry			= 0;
			uint256			uCurIndex;

			while (saPrvDlvReq != saPrvDlvAct	// Have not met request.
				&& dirNext(uDirectTip, sleDirectDir, uEntry, uCurIndex))
			{
				SLE::pointer	sleCurOfr			= entryCache(ltOFFER, uCurIndex);
				uint160			uCurOfrAccountID	= sleCurOfr->getIValueFieldAccount(sfAccount).getAccountID();
				STAmount		saCurOfrOutReq		= sleCurOfr->getIValueFieldAmount(sfTakerGets);
				STAmount		saCurOfrInReq		= sleCurOfr->getIValueFieldAmount(sfTakerPays);
					// XXX Move issuer into STAmount
					if (sleCurOfr->getIFieldPresent(sfGetsIssuer))
						saCurOfrOutReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfGetsIssuer).getAccountID());

					if (sleCurOfr->getIFieldPresent(sfPaysIssuer))
						saCurOfrInReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());
				STAmount		saCurOfrInAct;
				STAmount		saCurOfrFunds		= accountFunds(uCurOfrAccountID, saCurOfrOutReq);	// Funds left.

				saCurOfrInReq	= MIN(saCurOfrInReq, saPrvDlvReq-saPrvDlvAct);

				if (!!uNxtAccountID)
				{
					// Next is an account.

					STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtAccountID == uCurIssuerID
												? saOne
												: saTransferRate;
					bool		bFee		= saFeeRate != saOne;

					STAmount	saOutPass	= STAmount::divide(saCurOfrInReq, saOfrRate, uCurCurrencyID);
					STAmount	saOutBase	= MIN(saCurOfrOutReq, saOutPass);				// Limit offer out by needed.
					STAmount	saOutCost	= MIN(
												bFee
													? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
													: saOutBase,
												saCurOfrFunds);								// Limit cost by fees & funds.
					STAmount	saOutDlvAct	= bFee
													? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
													: saOutCost;		// Out amount after fees.
					STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uCurCurrencyID);	// Compute input w/o fees required.

					saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
					saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
				}
				else
				{
					// Next is an offer.

					uint256			uNxtTip		= Ledger::getBookBase(uCurCurrencyID, uCurIssuerID, uNxtCurrencyID, uNxtIssuerID);
					uint256			uNxtEnd		= Ledger::getQualityNext(uNxtTip);
					bool			bNxtAdvance	= !entryCache(ltDIR_NODE, uNxtTip);

					while (!!uNxtTip						// Have a quality.
						&& saPrvDlvAct != saPrvDlvReq)	// Have more to do.
					{
						if (bNxtAdvance)
						{
							uNxtTip		= mLedger->getNextLedgerIndex(uNxtTip, uNxtEnd);
						}
						else
						{
							bNxtAdvance		= true;
						}

						if (!!uNxtTip)
						{
							// Do a directory.
							// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
							SLE::pointer	sleNxtDir	= entryCache(ltDIR_NODE, uNxtTip);
// ??? STAmount		saOfrRate		= STAmount::setRate(STAmount::getQuality(uNxtTip));	// For correct ratio
							unsigned int	uEntry			= 0;
							uint256			uNxtIndex;

							while (saPrvDlvReq != saPrvDlvAct	// Have not met request.
								&& dirNext(uNxtTip, sleNxtDir, uEntry, uNxtIndex))
							{
								// YYY This could combine offers with the same fee before doing math.
								SLE::pointer	sleNxtOfr			= entryCache(ltOFFER, uNxtIndex);
								uint160			uNxtOfrAccountID	= sleNxtOfr->getIValueFieldAccount(sfAccount).getAccountID();
								STAmount		saNxtOfrIn			= sleNxtOfr->getIValueFieldAmount(sfTakerPays);
									// XXX Move issuer into STAmount
									if (sleNxtOfr->getIFieldPresent(sfPaysIssuer))
										saNxtOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

								STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtOfrAccountID == uCurIssuerID
															? saOne
															: saTransferRate;
								bool		bFee		= saFeeRate != saOne;

								STAmount	saInBase	= saCurOfrInReq-saCurOfrInAct;
								STAmount	saOutPass	= STAmount::divide(saInBase, saOfrRate, uCurCurrencyID);
								STAmount	saOutBase	= MIN(saCurOfrOutReq, saOutPass);				// Limit offer out by needed.
											saOutBase	= MIN(saOutBase, saNxtOfrIn);					// Limit offer out by supplying offer.
								STAmount	saOutCost	= MIN(
															bFee
																? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
																: saOutBase,
															saCurOfrFunds);								// Limit cost by fees & funds.
								STAmount	saOutDlvAct	= bFee
																? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
																: saOutCost;							// Out amount after fees.
								STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

								saCurOfrInAct			+= saOutDlvAct;									// Portion of driver served.
								saPrvDlvAct				+= saOutDlvAct;									// Portion needed in previous.
								saCurDlvAct				+= saInDlvAct;									// Portion of driver served.
							}
						}

						// Do another nxt directory iff bMultiQuality
						if (!bMultiQuality)
							uNxtTip	= 0;
					}
				}
			}
		}

		// Do another cur directory iff bMultiQuality
		if (!bMultiQuality)
			uDirectTip	= 0;
	}

	if (saCurDlvAct)
	{
		saCurDlvReq	= saCurDlvAct;	// Adjust request.
		bSuccess	= true;
	}

	return bSuccess;
}

#if 0
// If either currency is not stamps, then also calculates vs stamp bridge.
// --> saWanted: Limit of how much is wanted out.
// <-- saPay: How much to pay into the offer.
// <-- saGot: How much to the offer pays out.  Never more than saWanted.
// Given two value's enforce a minimum:
// - reverse: prv is maximum to pay in (including fee) - cur is what is wanted: generally, minimizing prv
// - forward: prv is actual amount to pay in (including fee) - cur is what is wanted: generally, minimizing cur
// Value in is may be rippled or credited from limbo. Value out is put in limbo.
// If next is an offer, the amount needed is in cur reedem.
// XXX What about account mentioned multiple times via offers?
void TransactionEngine::calcNodeOffer(
	bool			bForward,
	bool			bMultiQuality,	// True, if this is the only active path: we can do multiple qualities in this pass.
	const uint160&	uPrvAccountID,	// If 0, then funds from previous offer's limbo
	const uint160&	uPrvCurrencyID,
	const uint160&	uPrvIssuerID,
	const uint160&	uCurCurrencyID,
	const uint160&	uCurIssuerID,

	const STAmount& uPrvRedeemReq,	// --> In limit.
	STAmount&		uPrvRedeemAct,	// <-> In limit achived.
	const STAmount& uCurRedeemReq,	// --> Out limit. Driver when uCurIssuerID == uNxtIssuerID (offer would redeem to next)
	STAmount&		uCurRedeemAct,	// <-> Out limit achived.

	const STAmount& uCurIssueReq,	// --> In limit.
	STAmount&		uCurIssueAct,	// <-> In limit achived.
	const STAmount& uCurIssueReq,	// --> Out limit. Driver when uCurIssueReq != uNxtIssuerID (offer would effectively issue or transfer to next)
	STAmount&		uCurIssueAct,	// <-> Out limit achived.

	STAmount& saPay,
	STAmount& saGot
	) const
{
	TransactionEngineResult	terResult	= tenUNKNOWN;

	// Direct: not bridging via XNS
	bool			bDirectNext	= true;		// True, if need to load.
	uint256			uDirectQuality;
	uint256			uDirectTip	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);
	uint256			uDirectEnd	= Ledger::getQualityNext(uDirectTip);

	// Bridging: bridging via XNS
	bool			bBridge		= true;		// True, if bridging active. False, missing an offer.
	uint256			uBridgeQuality;
	STAmount		saBridgeIn;				// Amount available.
	STAmount		saBridgeOut;

	bool			bInNext		= true;		// True, if need to load.
	STAmount		saInIn;					// Amount available. Consumed in loop. Limited by offer funding.
	STAmount		saInOut;
	uint256			uInTip;					// Current entry.
	uint256			uInEnd;
	unsigned int	uInEntry;

	bool			bOutNext	= true;
	STAmount		saOutIn;
	STAmount		saOutOut;
	uint256			uOutTip;
	uint256			uOutEnd;
	unsigned int	uOutEntry;

	saPay.zero();
	saPay.setCurrency(uPrvCurrencyID);
	saPay.setIssuer(uPrvIssuerID);

	saNeed	= saWanted;

	if (!uCurCurrencyID && !uPrvCurrencyID)
	{
		// Bridging: Neither currency is XNS.
		uInTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, CURRENCY_XNS, ACCOUNT_XNS);
		uInEnd		= Ledger::getQualityNext(uInTip);
		uOutTip		= Ledger::getBookBase(CURRENCY_XNS, ACCOUNT_XNS, uCurCurrencyID, uCurIssuerID);
		uOutEnd		= Ledger::getQualityNext(uInTip);
	}

	// Find our head offer.

	bool		bRedeeming		= false;
	bool		bIssuing		= false;

	// The price varies as we change between issuing and transfering, so unless bMultiQuality, we must stick with a mode once it
	// is determined.

	if (bBridge && (bInNext || bOutNext))
	{
		// Bridging and need to calculate next bridge rate.
		// A bridge can consist of multiple offers. As offer's are consumed, the effective rate changes.

		if (bInNext)
		{
//					sleInDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uInIndex, uInEnd));
			// Get the next funded offer.
			offerBridgeNext(uInIndex, uInEnd, uInEntry, saInIn, saInOut);	// Get offer limited by funding.
			bInNext		= false;
		}

		if (bOutNext)
		{
//					sleOutDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uOutIndex, uOutEnd));
			offerNext(uOutIndex, uOutEnd, uOutEntry, saOutIn, saOutOut);
			bOutNext	= false;
		}

		if (!uInIndex || !uOutIndex)
		{
			bBridge	= false;	// No more offers to bridge.
		}
		else
		{
			// Have bridge in and out entries.
			// Calculate bridge rate.  Out offer pay ripple fee.  In offer fee is added to in cost.

			saBridgeOut.zero();

			if (saInOut < saOutIn)
			{
				// Limit by in.

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				// Limit bridge out: saInOut/saBridgeOut = saOutIn/saOutOut
				// Round such that we would take all of in offer, otherwise would have leftovers.
				saBridgeOut	= (saInOut * saOutOut) / saOutIn;
			}
			else if (saInOut > saOutIn)
			{
				// Limit by out, if at all.

				// XXX Need to include fees in saBridgeIn.
				// Limit bridge in:saInIn/saInOuts = aBridgeIn/saOutIn
				// Round such that would take all of out offer.
				saBridgeIn	= (saInIn * saOutIn) / saInOuts;
				saBridgeOut	= saOutOut;		// All of out.
			}
			else
			{
				// Entries match,

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				saBridgeOut	= saOutOut;	// All of out.
			}

			uBridgeQuality	= STAmount::getRate(saBridgeIn, saBridgeOut);	// Inclusive of fees.
		}
	}

	if (bBridge)
	{
		bUseBridge	= !uDirectTip || (uBridgeQuality < uDirectQuality)
	}
	else if (!!uDirectTip)
	{
		bUseBridge	= false
	}
	else
	{
		// No more offers. Declare success, even if none returned.
		saGot		= saWanted-saNeed;
		terResult	= terSUCCESS;
	}

	if (terSUCCESS != terResult)
	{
		STAmount&	saAvailIn	= bUseBridge ? saBridgeIn : saDirectIn;
		STAmount&	saAvailOut	= bUseBridge ? saBridgeOut : saDirectOut;

		if (saAvailOut > saNeed)
		{
			// Consume part of offer. Done.

			saNeed	= 0;
			saPay	+= (saNeed*saAvailIn)/saAvailOut; // Round up, prefer to pay more.
		}
		else
		{
			// Consume entire offer.

			saNeed	-= saAvailOut;
			saPay	+= saAvailIn;

			if (bUseBridge)
			{
				// Consume bridge out.
				if (saOutOut == saAvailOut)
				{
					// Consume all.
					saOutOut	= 0;
					saOutIn		= 0;
					bOutNext	= true;
				}
				else
				{
					// Consume portion of bridge out, must be consuming all of bridge in.
					// saOutIn/saOutOut = saSpent/saAvailOut
					// Round?
					saOutIn		-= (saOutIn*saAvailOut)/saOutOut;
					saOutOut	-= saAvailOut;
				}

				// Consume bridge in.
				if (saOutIn == saAvailIn)
				{
					// Consume all.
					saInOut		= 0;
					saInIn		= 0;
					bInNext		= true;
				}
				else
				{
					// Consume portion of bridge in, must be consuming all of bridge out.
					// saInIn/saInOut = saAvailIn/saPay
					// Round?
					saInOut	-= (saInOut*saAvailIn)/saInIn;
					saInIn	-= saAvailIn;
				}
			}
			else
			{
				bDirectNext	= true;
			}
		}
	}
}
#endif

// Cur is the driver and will be filled exactly.
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
// This is a minimizing routine: moving in reverse it propagates the send limit to the sender, moving forward it propagates the
// actual send toward the reciver.
// This routine works backwards as it calculates previous wants based on previous credit limits and current wants.
// This routine works forwards as it calculates current deliver based on previous delivery limits and current wants.
void TransactionEngine::calcNodeRipple(
	const uint32 uQualityIn,
	const uint32 uQualityOut,
	const STAmount& saPrvReq,	// --> in limit including fees, <0 = unlimited
	const STAmount& saCurReq,	// --> out limit (driver)
	STAmount& saPrvAct,			// <-> in limit including achieved
	STAmount& saCurAct)			// <-> out limit achieved.
{
	bool		bPrvUnlimited	= saPrvReq.isNegative();
	STAmount	saPrv			= bPrvUnlimited ? saZero : saPrvReq-saPrvAct;
	STAmount	saCur			= saCurReq-saCurAct;

	if (uQualityIn >= uQualityOut)
	{
		// No fee.
		STAmount	saTransfer	= bPrvUnlimited ? saCur : MIN(saPrv, saCur);

		saPrvAct	+= saTransfer;
		saCurAct	+= saTransfer;
	}
	else
	{
		// Fee.
		STAmount	saCurIn	= STAmount::divide(STAmount::multiply(saCur, uQualityOut, CURRENCY_ONE), uQualityIn, CURRENCY_ONE);

		if (bPrvUnlimited || saCurIn >= saPrv)
		{
			// All of cur. Some amount of prv.
			saCurAct	= saCurReq;
			saPrvAct	+= saCurIn;
		}
		else
		{
			// A part of cur. All of prv. (cur as driver)
			STAmount	saCurOut	= STAmount::divide(STAmount::multiply(saPrv, uQualityIn, CURRENCY_ONE), uQualityOut, CURRENCY_ONE);

			saCurAct	+= saCurOut;
			saPrvAct	= saPrvReq;
		}
	}
}

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver;
bool TransactionEngine::calcNodeAccountRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	bool			bSuccess		= true;
	unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	paymentNode&	prvPN			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	paymentNode&	curPN			= pspCur->vpnNodes[uIndex];
	paymentNode&	nxtPN			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	bool			bRedeem			= !!(curPN.uFlags & STPathElement::typeRedeem);
	bool			bPrvRedeem		= !!(prvPN.uFlags & STPathElement::typeRedeem);
	bool			bIssue			= !!(curPN.uFlags & STPathElement::typeIssue);
	bool			bPrvIssue		= !!(prvPN.uFlags & STPathElement::typeIssue);
	bool			bPrvAccount		= !!(prvPN.uFlags & STPathElement::typeAccount);
	bool			bNxtAccount		= !!(nxtPN.uFlags & STPathElement::typeAccount);

	uint160&		uPrvAccountID	= prvPN.uAccountID;
	uint160&		uCurAccountID	= curPN.uAccountID;
	uint160&		uNxtAccountID	= bNxtAccount ? nxtPN.uAccountID : uCurAccountID;	// Offers are always issue.

	uint160&		uCurrencyID		= curPN.uCurrencyID;

	uint32			uQualityIn		= rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID);
	uint32			uQualityOut		= rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID);

	// For bPrvAccount
	STAmount		saPrvBalance	= bPrvAccount ? rippleBalance(uCurAccountID, uPrvAccountID, uCurrencyID) : saZero;
	STAmount		saPrvLimit		= bPrvAccount ? rippleLimit(uCurAccountID, uPrvAccountID, uCurrencyID) : saZero;

	STAmount		saPrvRedeemReq	= bPrvRedeem && saPrvBalance.isNegative() ? -saPrvBalance : STAmount(uCurrencyID, 0);
	STAmount&		saPrvRedeemAct	= prvPN.saRevRedeem;

	STAmount		saPrvIssueReq	= bPrvIssue && saPrvLimit - saPrvBalance;
	STAmount&		saPrvIssueAct	= prvPN.saRevIssue;

	// For !bPrvAccount
	STAmount		saPrvDeliverReq	= STAmount(uCurrencyID, -1);	// Unlimited.
	STAmount&		saPrvDeliverAct	= prvPN.saRevDeliver;

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= curPN.saRevRedeem;
	STAmount		saCurRedeemAct;

	const STAmount&	saCurIssueReq	= curPN.saRevIssue;
	STAmount		saCurIssueAct;									// Track progress.

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= curPN.saRevDeliver;
	STAmount		saCurDeliverAct;

	// For uIndex == uLast
	const STAmount&	saCurWantedReq	= pspCur->saOutReq;				// XXX Credit limits?
	// STAmount		saPrvDeliverReq	= saPrvBalance.isPositive() ? saPrvLimit - saPrvBalance : saPrvLimit;
	STAmount		saCurWantedAct;

	if (bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $

			// Calculate redeem
			if (bRedeem
				&& saPrvRedeemReq)								// Previous has IOUs to redeem.
			{
				// Redeem at 1:1
				saCurWantedAct		= MIN(saPrvRedeemReq, saCurWantedReq);
				saPrvRedeemAct		= saCurWantedAct;
			}

			// Calculate issuing.
			if (bIssue
				&& saCurWantedReq != saCurWantedAct				// Need more.
				&& saPrvIssueReq)								// Will accept IOUs.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq, saPrvIssueAct, saCurWantedAct);
			}

			if (!saCurWantedAct)
			{
				// Must have processed something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
		else
		{
			// account --> ACCOUNT --> account

			// redeem (part 1) -> redeem
			if (bPrvRedeem
				&& bRedeem								// Allowed to redeem.
				&& saCurRedeemReq						// Next wants us to redeem.
				&& saPrvBalance.isNegative())			// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct);
			}

			// redeem (part 2) -> issue.
			if (bPrvRedeem
				&& bIssue								// Allowed to issue.
				&& saCurRedeemReq != saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& saPrvBalance.isNegative()			// Previous still has IOUs.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// issue (part 1)-> redeem
			if (bPrvIssue
				&& bRedeem								// Allowed to redeem.
				&& saCurRedeemReq != saCurRedeemAct		// Can only redeem if more to be redeemed.
				&& !saPrvBalance.isNegative())			// Previous has no IOUs.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct);
			}

			// issue (part 2) -> issue
			if (bPrvIssue
				&& bIssue								// Allowed to issue.
				&& saCurRedeemReq != saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& !saPrvBalance.isNegative()			// Previous has no IOUs.
				&& saCurIssueReq != saCurIssueAct)		// Need some issued.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct);
			}

			if (!saCurRedeemAct && !saCurIssueAct)
			{
				// Must want something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		// Note: deliver is always issue as ACCOUNT is the issuer for the offer input.

		// redeem -> deliver/issue.
		if (bPrvRedeem
			&& bIssue								// Allowed to issue.
			&& saPrvBalance.isNegative()			// Previous redeeming: Previous still has IOUs.
			&& saCurDeliverReq)						// Need some issued.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct);
		}

		// issue -> deliver/issue
		if (bPrvIssue
			&& bIssue								// Allowed to issue.
			&& !saPrvBalance.isNegative()			// Previous issuing: Previous has no IOUs.
			&& saCurDeliverReq != saCurDeliverAct)	// Need some issued.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			// terResult	= tenBAD_AMOUNT;
			bSuccess	= false;
		}
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $

			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq, saPrvDeliverAct, saCurWantedAct);

			if (!saCurWantedAct)
			{
				// Must have processed something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
		else
		{
			// offer --> ACCOUNT --> account
			// Note: offer is always deliver/redeeming as account is issuer.

			// deliver -> redeem
			if (bRedeem									// Allowed to redeem.
				&& saCurRedeemReq						// Next wants us to redeem.
				&& saPrvBalance.isNegative())			// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct);
			}

			// deliver -> issue.
			if (bIssue									// Allowed to issue.
				&& saCurRedeemReq != saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& saPrvBalance.isNegative()			// Previous still has IOUs.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct);
			}

			if (!saCurDeliverAct && !saCurIssueAct)
			{
				// Must want something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		if (bIssue									// Allowed to issue.
			&& saCurDeliverReq != saCurDeliverAct)	// Can only if issue if more can not be redeemed.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			// terResult	= tenBAD_AMOUNT;
			bSuccess	= false;
		}
	}

	// XXX Need a more nuanced return: temporary fail vs perm?
	return bSuccess;
}

// The previous node: specifies what to push through to current.
// - All of previous output is consumed.
// The current node: specify what to push through to next.
// - Output to next node minus fees.
// Perform balance adjustment with previous.
bool TransactionEngine::calcNodeAccountFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	bool			bSuccess		= true;
	unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	paymentNode&	prvPN			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	paymentNode&	curPN			= pspCur->vpnNodes[uIndex];
	paymentNode&	nxtPN			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	bool			bRedeem			= !!(curPN.uFlags & STPathElement::typeRedeem);
	bool			bIssue			= !!(curPN.uFlags & STPathElement::typeIssue);
	bool			bPrvAccount		= !!(prvPN.uFlags & STPathElement::typeAccount);
	bool			bNxtAccount		= !!(nxtPN.uFlags & STPathElement::typeAccount);

	uint160&		uPrvAccountID	= prvPN.uAccountID;
	uint160&		uCurAccountID	= curPN.uAccountID;
	uint160&		uNxtAccountID	= bNxtAccount ? nxtPN.uAccountID : uCurAccountID;	// Offers are always issue.

	uint160&		uCurrencyID		= curPN.uCurrencyID;

	uint32			uQualityIn		= rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID);
	uint32			uQualityOut		= rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID);

	// For bNxtAccount
	const STAmount&	saPrvRedeemReq	= prvPN.saFwdRedeem;
	STAmount		saPrvRedeemAct;

	const STAmount&	saPrvIssueReq	= prvPN.saFwdIssue;
	STAmount		saPrvIssueAct;

	// For !bPrvAccount
	const STAmount&	saPrvDeliverReq	= prvPN.saRevDeliver;
	STAmount		saPrvDeliverAct;

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= curPN.saRevRedeem;
	STAmount&		saCurRedeemAct	= curPN.saFwdRedeem;

	const STAmount&	saCurIssueReq	= curPN.saRevIssue;
	STAmount&		saCurIssueAct	= curPN.saFwdIssue;

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= curPN.saRevDeliver;
	STAmount&		saCurDeliverAct	= curPN.saFwdDeliver;

	STAmount&		saCurReceive	= pspCur->saOutAct;

	// Ripple through account.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT --> account

			// First node, calculate amount to send.
			// XXX Use stamp/ripple balance
			paymentNode&	curPN			= pspCur->vpnNodes[uIndex];

			STAmount&		saCurRedeemReq	= curPN.saRevRedeem;
			STAmount&		saCurRedeemAct	= curPN.saFwdRedeem;
			STAmount&		saCurIssueReq	= curPN.saRevIssue;
			STAmount&		saCurIssueAct	= curPN.saFwdIssue;

			STAmount&		saCurSendMaxReq	= pspCur->saInReq;
			STAmount&		saCurSendMaxAct = pspCur->saInAct;

			if (saCurRedeemReq)
			{
				// Redeem requested.
				saCurRedeemAct	= MIN(saCurRedeemAct, saCurSendMaxReq);
				saCurSendMaxAct	= saCurRedeemAct;
			}

			if (saCurIssueReq && saCurSendMaxReq != saCurRedeemAct)
			{
				// Issue requested and not over budget.
				saCurIssueAct	= MIN(saCurSendMaxReq-saCurRedeemAct, saCurIssueReq);
				// saCurSendMaxAct	+= saCurIssueReq; // Not needed.
			}
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $

			// Last node.  Accept all funds.  Calculate amount actually to credit.

			STAmount	saIssueCrd	= uQualityIn >= QUALITY_ONE
											? saPrvIssueReq													// No fee.
											: STAmount::multiply(saPrvIssueReq, uQualityIn, uCurrencyID);	// Fee.

			// Amount to credit.
			saCurReceive	= saPrvRedeemReq+saIssueCrd;

			// Actually receive.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq+saPrvIssueReq);
		}
		else
		{
			// account --> ACCOUNT --> account

			// Previous redeem part 1: redeem -> redeem
			if (bRedeem										// Can redeem.
				&& saPrvRedeemReq != saPrvRedeemAct)		// Previous wants to redeem. To next must be ok.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct);
			}

			// Previous redeem part 2: redeem -> issue.
			// wants to redeem and current would and can issue.
			// If redeeming cur to next is done, this implies can issue.
			if (bIssue										// Can issue.
				&& saPrvRedeemReq != saPrvRedeemAct			// Previous still wants to redeem.
				&& saCurRedeemReq == saCurRedeemAct			// Current has no more to redeem to next.
				&& saCurIssueReq)
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// Previous issue part 1: issue -> redeem
			if (bRedeem										// Can redeem.
				&& saPrvIssueReq != saPrvIssueAct			// Previous wants to issue.
				&& saCurRedeemReq != saCurRedeemAct)		// Current has more to redeem to next.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct);
			}

			// Previous issue part 2 : issue -> issue
			if (bIssue										// Can issue.
				&& saPrvIssueReq != saPrvIssueAct)			// Previous wants to issue. To next must be ok.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct);
			}

			// Adjust prv --> cur balance : take all inbound
			// XXX Currency must be in amount.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq);
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer

		// redeem -> issue.
		// wants to redeem and current would and can issue.
		// If redeeming cur to next is done, this implies can issue.
		if (saPrvRedeemReq)								// Previous wants to redeem.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct);
		}

		// issue -> issue
		if (saPrvRedeemReq == saPrvRedeemAct			// Previous done redeeming: Previous has no IOUs.
			&& saPrvIssueReq)							// Previous wants to issue. To next must be ok.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct);
		}

		// Adjust prv --> cur balance : take all inbound
		// XXX Currency must be in amount.
		rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq);
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $

			// Amount to credit.
			saCurReceive	= saPrvDeliverAct;

			// No income balance adjustments necessary.  The paying side inside the offer paid to this account.
		}
		else
		{
			// offer --> ACCOUNT --> account

			// deliver -> redeem
			if (bRedeem										// Allowed to redeem.
				&& saPrvDeliverReq)							// Previous wants to deliver.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct);
			}

			// deliver -> issue
			// Wants to redeem and current would and can issue.
			if (bIssue										// Allowed to issue.
				&& saPrvDeliverReq != saPrvDeliverAct		// Previous still wants to deliver.
				&& saCurRedeemReq == saCurRedeemAct			// Current has more to redeem to next.
				&& saCurIssueReq)							// Current wants issue.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		if (bIssue											// Allowed to issue.
			&& saPrvDeliverReq								// Previous wants to deliver
			&& saCurIssueReq)								// Current wants issue.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransfer(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct);
		}

		// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
	}

	return bSuccess;
}

// Return true, iff lhs has less priority than rhs.
bool PathState::lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs)
{
	if (lhs->uQuality != rhs->uQuality)
		return lhs->uQuality > rhs->uQuality;	// Bigger is worse.

	// Best quanity is second rank.
	if (lhs->saOutAct != rhs->saOutAct)
		return lhs->saOutAct < rhs->saOutAct;	// Smaller is worse.

	// Path index is third rank.
	return lhs->mIndex > rhs->mIndex;			// Bigger is worse.
}

// Add a node and insert any implied nodes.
// <-- bValid: true, if node is valid. false, if node is malformed.
bool PathState::pushNode(int iType, uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID)
{
			paymentNode		pnCur;
			bool			bFirst		= vpnNodes.empty();
	const	paymentNode&	pnPrv		= bFirst ? paymentNode() : vpnNodes.back();
			bool			bAccount	= !!(iType & STPathElement::typeAccount);
			bool			bCurrency	= !!(iType & STPathElement::typeCurrency);
			bool			bIssuer		= !!(iType & STPathElement::typeIssuer);
			bool			bRedeem		= !!(iType & STPathElement::typeRedeem);
			bool			bIssue		= !!(iType & STPathElement::typeIssue);
			bool			bValid		= true;

	pnCur.uFlags		= iType;

	if (iType & ~STPathElement::typeValidBits)
	{
		bValid	= false;
	}
	else if (bAccount)
	{
		if (bRedeem || bIssue)
		{
			// Account link

			pnCur.uAccountID	= uAccountID;
			pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
			pnCur.uIssuerID		= bIssuer ? uIssuerID : uAccountID;

			// An intermediate node may be implied.

			if (uCurrencyID != pnPrv.uCurrencyID)
			{
				// Implied preceeding offer.

				bValid	= pushNode(
							0,
							ACCOUNT_ONE,
							CURRENCY_ONE,	// Inherit from previous
							ACCOUNT_ONE);	// Inherit from previous
			}

			if (bValid && uIssuerID != pnPrv.uIssuerID)
			{
				// Implied preceeding account.

				bValid	= pushNode(
							STPathElement::typeAccount
								| STPathElement::typeRedeem
								| STPathElement::typeIssue,
							uIssuerID,
							CURRENCY_ONE,	// Inherit from previous
							ACCOUNT_ONE);	// Default same as account.
			}

			if (bValid)
				vpnNodes.push_back(pnCur);
		}
		else
		{
			bValid	= false;
		}
	}
	else
	{
		// Offer link
		if (bRedeem || bIssue)
		{
			bValid	= false;
		}
		else
		{
			pnCur.uAccountID	= uAccountID;
			pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
			pnCur.uIssuerID		= bIssuer ? uIssuerID : pnCur.uAccountID;

			if (!!pnPrv.uAccountID							// Previous is an account.
				&& pnPrv.uAccountID != pnCur.uIssuerID)		// Account is not issuer.
			{
				// Implied preceeding account.

				bValid	= pushNode(
							STPathElement::typeAccount
								| STPathElement::typeIssue,
							uIssuerID,
							CURRENCY_ONE,					// Inherit from previous
							ACCOUNT_ONE);					// Default same as account.
			}
			else if (bValid)
			{
				// Verify that previous account is allowed to issue.
				const paymentNode&	pnLst		= vpnNodes.back();
				bool				bLstAccount	= !!(pnLst.uFlags & STPathElement::typeAccount);
				bool				bLstIssue	= !!(pnLst.uFlags & STPathElement::typeIssue);

				if (bLstAccount && !bLstIssue)
					bValid	= false;						// Malformed path.
			}

			if (bValid)
				vpnNodes.push_back(pnCur);
		}
	}

	return bValid;
}

// XXX Disallow loops in ripple paths
PathState::PathState(
	int						iIndex,
	const LedgerEntrySet&	lesSource,
	const STPath&			spSourcePath,
	uint160					uReceiverID,
	uint160					uSenderID,
	STAmount				saSend,
	STAmount				saSendMax,
	bool					bPartialPayment
	)
	: mIndex(iIndex), uQuality(0), bDirty(true)
{
	lesEntries				= lesSource.duplicate();

	saOutReq				= saSend;
	saInReq					= saSendMax;

	pushNode(STPathElement::typeAccount, uSenderID, saSendMax.getCurrency(), saSendMax.getIssuer());

	BOOST_FOREACH(const STPathElement& speElement, spSourcePath)
	{
		pushNode(speElement.getNodeType(), speElement.getAccountID(), speElement.getCurrency(), speElement.getIssuerID());
	}

	pushNode(STPathElement::typeAccount, uReceiverID, saOutReq.getCurrency(), saOutReq.getIssuer());
}

// Calculate a node and its previous nodes.
// From the destination work towards the source calculating how much must be asked for.
// --> bAllowPartial: If false, fail if can't meet requirements.
// <-- bSuccess: true=success, false=insufficient funds / liqudity.
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
// XXX Disallow looping.
bool TransactionEngine::calcNode(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	paymentNode&	curPN		= pspCur->vpnNodes[uIndex];
	bool			bCurAccount	= !!(curPN.uFlags & STPathElement::typeAccount);
	bool			bSuccess;

	// Do current node reverse.
	bSuccess	= bCurAccount
					? calcNodeAccountRev(uIndex, pspCur, bMultiQuality)
					: calcNodeOfferRev(uIndex, pspCur, bMultiQuality);

	// Do previous.
	if (bSuccess && uIndex)
	{
		bSuccess	= calcNode(uIndex-1, pspCur, bMultiQuality);
	}

	// Do current node forward.
	if (bSuccess)
	{
		bSuccess	= bCurAccount
						? calcNodeAccountFwd(uIndex, pspCur, bMultiQuality)
						: calcNodeOfferFwd(uIndex, pspCur, bMultiQuality);
	}

	return bSuccess;
}

// Calculate the next increment of a path.
void TransactionEngine::pathNext(PathState::pointer pspCur, int iPaths)
{
	// The next state is what is available in preference order.
	// This is calculated when referenced accounts changed.

	unsigned int	uLast	= pspCur->vpnNodes.size() - 1;

	if (!calcNode(uLast, pspCur, iPaths == 1))
	{
		// Mark path as inactive.
		pspCur->uQuality	= 0;
		pspCur->bDirty		= false;
	}
}

// Apply an increment of the path, then calculate the next increment.
void TransactionEngine::pathApply(PathState::pointer pspCur)
{
}

// XXX Need to audit for things like setting accountID not having memory.
TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn)
{
	// Ripple if source or destination is non-native or if there are paths.
	uint32		uTxFlags		= txn.getFlags();
	bool		bCreate			= !!(uTxFlags & tfCreateAccount);
	bool		bNoRippleDirect	= !!(uTxFlags & tfNoRippleDirect);
	bool		bPartialPayment	= !!(uTxFlags & tfPartialPayment);
	bool		bPaths			= txn.getITFieldPresent(sfPaths);
	bool		bMax			= txn.getITFieldPresent(sfSendMax);
	uint160		uDstAccountID	= txn.getITFieldAccount(sfDestination);
	STAmount	saDstAmount		= txn.getITFieldAmount(sfAmount);
	STAmount	saMaxAmount		= bMax ? txn.getITFieldAmount(sfSendMax) : saDstAmount;
	uint160		uSrcCurrency	= saMaxAmount.getCurrency();
	uint160		uDstCurrency	= saDstAmount.getCurrency();

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Invalid transaction: Payment destination account not specifed.";

		return tenDST_NEEDED;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getHumanCurrency() << " " << saDstAmount.getText();

		return tenBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return tenREDUNDANT;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		if (bCreate && !saDstAmount.isNative())
		{
			// This restriction could be relaxed.
			Log(lsINFO) << "doPayment: Invalid transaction: Create account may only fund XNS.";

			return tenCREATEXNS;
		}
		else if (!bCreate)
		{
			Log(lsINFO) << "doPayment: Delay transaction: Destination account does not exist.";

			return terNO_DST;
		}

		// Create the account.
		sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setIFieldAccount(sfAccount, uDstAccountID);
		sleDst->setIFieldU32(sfSequence, 1);
	}
	// Destination exists.
	else if (bCreate)
	{
		// Retryable: if account created this ledger, reordering might allow account to be made by this transaction.
		Log(lsINFO) << "doPayment: Invalid transaction: Account already created.";

		return terCREATED;
	}
	else
	{
		entryModify(sleDst);
	}

	bool		bRipple			= bPaths || bMax || !saDstAmount.isNative();

	if (!bRipple)
	{
		// Direct XNS payment.
		STAmount	saSrcXNSBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);

		if (saSrcXNSBalance < saDstAmount)
		{
			// Transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficent funds.";

			return terUNFUNDED;
		}

		mTxnAccount->setIFieldAmount(sfBalance, saSrcXNSBalance - saDstAmount);
		sleDst->setIFieldAmount(sfBalance, sleDst->getIValueFieldAmount(sfBalance) + saDstAmount);

		return terSUCCESS;
	}

	//
	// Ripple payment
	//

	// Try direct ripple first.
	if (!bNoRippleDirect && mTxnAccountID != uDstAccountID && uSrcCurrency == uDstCurrency)
	{
		SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uDstCurrency));

		if (sleRippleState)
		{
			// There is a direct credit-line of some direction.
			// - We can always pay IOUs back.
			// - We can issue IOUs to the limit.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bSendHigh		= uLowID == mTxnAccountID && uHighID == uDstAccountID;
			bool		bSendLow		= uLowID == uDstAccountID && uHighID == mTxnAccountID;
			// Flag we need if we end up owing IOUs.
			uint32		uFlags			= bSendHigh ? lsfLowIndexed : lsfHighIndexed;

			assert(bSendLow || bSendHigh);

			STAmount	saDstLimit		= sleRippleState->getIValueFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);

			STAmount	saDstBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
				if (bSendHigh)
				{
					// Put balance in dst terms.
					saDstBalance.negate();
				}

			saDstBalance += saDstAmount;
			if (saDstBalance > saDstLimit)
			{
				// Would exceed credit limit.
				// YYY Note: in the future could push out other credits to make payment fit.

				Log(lsINFO) << "doPayment: Delay transaction: Over limit: proposed balance="
					<< saDstBalance.getText()
					<< " limit="
					<< saDstLimit.getText();

				return terOVER_LIMIT;
			}

			if (saDstBalance.isZero())
			{
				// XXX May be able to delete indexes for credit limits which are zero.
				nothing();
			}
			else if (saDstBalance.isNegative())
			{
				// dst still has outstanding IOUs, sle already indexed.
				nothing();
			}
			// src has outstanding IOUs, sle should be indexed.
			else if (! (sleRippleState->getFlags() & uFlags))
			{
				// Need to add index.
				TransactionEngineResult	terResult	= terSUCCESS;
				uint64					uSrcRef;					// Ignored, ripple_state dirs never delete.

				terResult	= dirAdd(
					uSrcRef,
					Ledger::getRippleDirIndex(mTxnAccountID),		// The source ended up owing.
					sleRippleState->getIndex());					// Adding current entry.

				if (terSUCCESS != terResult)
					return terResult;

				sleRippleState->setFlag(uFlags);					// Note now indexed.
			}

			if (bSendHigh)
			{
				// Put balance in low terms.
				saDstBalance.negate();
			}

			sleRippleState->setIFieldAmount(sfBalance, saDstBalance);
			entryModify(sleRippleState);

			return terSUCCESS;
		}
	}

	STPathSet	spsPaths = txn.getITFieldPathSet(sfPaths);

	if (!spsPaths.isEmpty())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: No paths.";

		return tenRIPPLE_EMPTY;
	}
	else if (spsPaths.getPathCount() > RIPPLE_PATHS_MAX)
	{
		return tenBAD_PATH_COUNT;
	}

	// Incrementally search paths.
	std::vector<PathState::pointer>	vpsPaths;

	BOOST_FOREACH(const STPath& spPath, spsPaths)
	{
		vpsPaths.push_back(PathState::createPathState(
			vpsPaths.size(),
			mNodes,
			spPath,
			uDstAccountID,
			mTxnAccountID,
			saDstAmount,
			saMaxAmount,
			bPartialPayment
			));
	}

	TransactionEngineResult	terResult;
	STAmount				saPaid;
	STAmount				saWanted;

	terResult	= tenUNKNOWN;
	while (tenUNKNOWN == terResult)
	{
		PathState::pointer	pspBest;

		// Find the best path.
		BOOST_FOREACH(PathState::pointer pspCur, vpsPaths)
		{
			if (pspCur->bDirty)
			{
				pspCur->bDirty		= false;
				pspCur->lesEntries	= mNodes.duplicate();

				// XXX Compute increment
				pathNext(pspCur, vpsPaths.size());
			}

			if (!pspBest || (pspCur->uQuality && PathState::lessPriority(pspBest, pspCur)))
				pspBest	= pspCur;
		}

		if (!pspBest)
		{
			//
			// Apply path.
			//

			// Install changes for path.
			mNodes.swapWith(pspBest->lesEntries);

			// Mark that path as dirty.
			pspBest->bDirty	= true;

			// Mark as dirty any other path that intersected.
			BOOST_FOREACH(PathState::pointer& pspOther, vpsPaths)
			{
				// Look for intersection of best and the others.
				// - Will forget the intersection applied.
				//   - Anything left will not interfere with it.
				// - Will remember the non-intersection non-applied for future consideration.
				if (!pspOther->bDirty
					&& pspOther->uQuality
					&& LedgerEntrySet::intersect(pspBest->lesEntries, pspOther->lesEntries))
					pspOther->bDirty	= true;
			}

			// Figure out if done.
			if (tenUNKNOWN == terResult && saPaid == saWanted)
			{
				terResult	= terSUCCESS;
			}
		}
		// Ran out of paths.
		else if (!bPartialPayment)
		{
			// Partial payment not allowed.
			terResult	= terPATH_PARTIAL;		// XXX No effect, except unfunded and charge fee.
		}
		else if (saPaid.isZero())
		{
			// Nothing claimed.
			terResult	= terPATH_EMPTY;	// XXX No effect except unfundeds and charge fee.
			// XXX
		}
		else
		{
			terResult	= terSUCCESS;
		}
	}

	Log(lsINFO) << "doPayment: Delay transaction: No ripple paths could be satisfied.";

	return terBAD_RIPPLE;
}

TransactionEngineResult TransactionEngine::doWalletAdd(const SerializedTransaction& txn)
{
	std::cerr << "WalletAdd>" << std::endl;

	std::vector<unsigned char>	vucPubKey		= txn.getITFieldVL(sfPubKey);
	std::vector<unsigned char>	vucSignature	= txn.getITFieldVL(sfSignature);
	uint160						uAuthKeyID		= txn.getITFieldAccount(sfAuthorizedKey);
	NewcoinAddress				naMasterPubKey	= NewcoinAddress::createAccountPublic(vucPubKey);
	uint160						uDstAccountID	= naMasterPubKey.getAccountID();

	if (!naMasterPubKey.accountPublicVerify(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature))
	{
		std::cerr << "WalletAdd: unauthorized:  bad signature " << std::endl;

		return tenBAD_ADD_AUTH;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tenCREATED;
	}

	STAmount			saAmount		= txn.getITFieldAmount(sfAmount);
	STAmount			saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);

	if (saSrcBalance < saAmount)
	{
		std::cerr
			<< str(boost::format("WalletAdd: Delay transaction: insufficent balance: balance=%s amount=%s")
				% saSrcBalance.getText()
				% saAmount.getText())
			<< std::endl;

		return terUNFUNDED;
	}

	// Deduct initial balance from source account.
	mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setIFieldAccount(sfAccount, uDstAccountID);
	sleDst->setIFieldU32(sfSequence, 1);
	sleDst->setIFieldAmount(sfBalance, saAmount);
	sleDst->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	std::cerr << "WalletAdd<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doInvoice(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker offers (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// <-- saTakerPaid: What taker paid not including fees. To reduce an offer.
// <--  saTakerGot: What taker got not including fees. To reduce an offer.
// <--   terResult: terSUCCESS or terNO_ACCOUNT
// Note: All SLE modifications must always occur even on failure.
// XXX: Fees should be paid by the source of the currency.
TransactionEngineResult TransactionEngine::takeOffers(
	bool				bPassive,
	const uint256&		uBookBase,
	const uint160&		uTakerAccountID,
	const SLE::pointer&	sleTakerAccount,
	const STAmount&		saTakerPays,
	const STAmount&		saTakerGets,
	STAmount&			saTakerPaid,
	STAmount&			saTakerGot)
{
	assert(!saTakerPays.isZero() && !saTakerGets.isZero());

	Log(lsINFO) << "takeOffers: against book: " << uBookBase.ToString();

	uint256					uTipIndex			= uBookBase;
	uint256					uBookEnd			= Ledger::getQualityNext(uBookBase);
	uint64					uTakeQuality		= STAmount::getRate(saTakerGets, saTakerPays);
	uint160					uTakerPaysAccountID	= saTakerPays.getIssuer();
	uint160					uTakerPaysCurrency	= saTakerPays.getCurrency();
	uint160					uTakerGetsAccountID	= saTakerGets.getIssuer();
	uint160					uTakerGetsCurrency	= saTakerGets.getCurrency();
	TransactionEngineResult	terResult			= tenUNKNOWN;

	saTakerPaid	= 0;
	saTakerGot	= 0;

	while (tenUNKNOWN == terResult)
	{
		SLE::pointer	sleOfferDir;
		uint64			uTipQuality;

		// Figure out next offer to take, if needed.
		if (saTakerGets != saTakerGot && saTakerPays != saTakerPaid)
		{
			// Taker has needs.

			sleOfferDir		= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uTipIndex, uBookEnd));
			if (sleOfferDir)
			{
				Log(lsINFO) << "takeOffers: possible counter offer found";

				uTipIndex		= sleOfferDir->getIndex();
				uTipQuality		= Ledger::getQuality(uTipIndex);
			}
			else
			{
				Log(lsINFO) << "takeOffers: counter offer book is empty: "
					<< uTipIndex.ToString()
					<< " ... "
					<< uBookEnd.ToString();
			}
		}

		if (!sleOfferDir || uTakeQuality < uTipQuality || (bPassive && uTakeQuality == uTipQuality))
		{
			// Done.
			Log(lsINFO) << "takeOffers: done";

			terResult	= terSUCCESS;
		}
		else
		{
			// Have an offer to consider.
			Log(lsINFO) << "takeOffers: considering dir : " << sleOfferDir->getJson(0);

			SLE::pointer	sleBookNode;
			unsigned int	uBookEntry;
			uint256			uOfferIndex;

			dirFirst(uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

			SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

			Log(lsINFO) << "takeOffers: considering offer : " << sleOffer->getJson(0);

			uint160			uOfferOwnerID	= sleOffer->getIValueFieldAccount(sfAccount).getAccountID();
			STAmount		saOfferPays		= sleOffer->getIValueFieldAmount(sfTakerGets);
			STAmount		saOfferGets		= sleOffer->getIValueFieldAmount(sfTakerPays);

			if (sleOffer->getIFieldPresent(sfGetsIssuer))
				saOfferPays.setIssuer(sleOffer->getIValueFieldAccount(sfGetsIssuer).getAccountID());

			if (sleOffer->getIFieldPresent(sfPaysIssuer))
				saOfferGets.setIssuer(sleOffer->getIValueFieldAccount(sfPaysIssuer).getAccountID());

			if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired. Delete it.
				Log(lsINFO) << "takeOffers: encountered expired offer";

				offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);

				mUnfunded.insert(uOfferIndex);
			}
			else if (uOfferOwnerID == uTakerAccountID)
			{
				// Would take own offer. Consider old offer unfunded.
				Log(lsINFO) << "takeOffers: encountered taker's own old offer";

				offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);
			}
			else
			{
				// Get offer funds available.

				Log(lsINFO) << "takeOffers: saOfferPays=" << saOfferPays.getFullText();

				STAmount		saOfferFunds	= accountFunds(uOfferOwnerID, saOfferPays);
				STAmount		saTakerFunds	= accountFunds(uTakerAccountID, saTakerPays);
				SLE::pointer	sleOfferAccount;	// Owner of offer.

				if (!saOfferFunds.isPositive())
				{
					// Offer is unfunded, possibly due to previous balance action.
					Log(lsINFO) << "takeOffers: offer unfunded: delete";

					offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);

					mUnfunded.insert(uOfferIndex);
				}
				else
				{
					STAmount	saPay		= saTakerPays - saTakerPaid;
						if (saTakerFunds < saPay)
							saPay	= saTakerFunds;
					STAmount	saSubTakerPaid;
					STAmount	saSubTakerGot;

					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPaid: " << saTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saTakerFunds: " << saTakerFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saOfferFunds: " << saOfferFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:          saPay: " << saPay.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferPays: " << saOfferPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferGets: " << saOfferGets.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerGets: " << saTakerGets.getFullText();

					bool	bOfferDelete	= STAmount::applyOffer(
						saOfferFunds,
						saPay,				// Driver XXX need to account for fees.
						saOfferPays,
						saOfferGets,
						saTakerPays,
						saTakerGets,
						saSubTakerPaid,
						saSubTakerGot);

					Log(lsINFO) << "takeOffers: applyOffer: saSubTakerPaid: " << saSubTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText();

					// Adjust offer
					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.
						Log(lsINFO) << "takeOffers: offer claimed: delete";

						offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);
					}
					else
					{
						Log(lsINFO) << "takeOffers: offer partial claim: modify";

						// Offer owner will pay less.  Subtract what taker just got.
						sleOffer->setIFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);

						// Offer owner will get less.  Subtract what owner just paid.
						sleOffer->setIFieldAmount(sfTakerPays, saOfferGets -= saSubTakerPaid);

						entryModify(sleOffer);
					}

					// Offer owner pays taker.
					saSubTakerGot.setIssuer(uTakerGetsAccountID);	// XXX Move this earlier?

					accountSend(uOfferOwnerID, uTakerAccountID, saSubTakerGot);

					saTakerGot	+= saSubTakerGot;

					// Taker pays offer owner.
					saSubTakerPaid.setIssuer(uTakerPaysAccountID);

					accountSend(uTakerAccountID, uOfferOwnerID, saSubTakerPaid);

					saTakerPaid	+= saSubTakerPaid;
				}
			}
		}
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCreate(const SerializedTransaction& txn)
{
Log(lsWARNING) << "doOfferCreate> " << txn.getJson(0);
	uint32					txFlags			= txn.getFlags();
	bool					bPassive		= !!(txFlags & tfPassive);
	uint160					uPaysIssuerID	= txn.getITFieldAccount(sfPaysIssuer);
	uint160					uGetsIssuerID	= txn.getITFieldAccount(sfGetsIssuer);
	STAmount				saTakerPays		= txn.getITFieldAmount(sfTakerPays);
		saTakerPays.setIssuer(uPaysIssuerID);
Log(lsWARNING) << "doOfferCreate: saTakerPays=" << saTakerPays.getFullText();
	STAmount				saTakerGets		= txn.getITFieldAmount(sfTakerGets);
		saTakerGets.setIssuer(uGetsIssuerID);
Log(lsWARNING) << "doOfferCreate: saTakerGets=" << saTakerGets.getFullText();
	uint32					uExpiration		= txn.getITFieldU32(sfExpiration);
	bool					bHaveExpiration	= txn.getITFieldPresent(sfExpiration);
	uint32					uSequence		= txn.getSequence();

	uint256					uLedgerIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer		= entryCreate(ltOFFER, uLedgerIndex);

	Log(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	uint160					uPaysCurrency	= saTakerPays.getCurrency();
	uint160					uGetsCurrency	= saTakerGets.getCurrency();
	uint64					uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TransactionEngineResult	terResult		= terSUCCESS;
	uint256					uDirectory;		// Delete hints.
	uint64					uOwnerNode;
	uint64					uBookNode;

	if (bHaveExpiration && !uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad expiration";

		terResult	= tenBAD_EXPIRATION;
	}
	else if (bHaveExpiration && mLedger->getParentCloseTimeNC() >= uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Expired transaction: offer expired";

		terResult	= tenEXPIRED;
	}
	else if (saTakerPays.isNative() && saTakerGets.isNative())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: XNS for XNS";

		terResult	= tenBAD_OFFER;
	}
	else if (saTakerPays.isZero() || saTakerGets.isZero())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad amount";

		terResult	= tenBAD_OFFER;
	}
	else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: redundant offer";

		terResult	= tenREDUNDANT;
	}
	else if (saTakerPays.isNative() != uPaysIssuerID.isZero() || saTakerGets.isNative() != uGetsIssuerID.isZero())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad issuer";

		terResult	= tenBAD_ISSUER;
	}
	else if (!accountFunds(mTxnAccountID, saTakerGets).isPositive())
	{
		Log(lsWARNING) << "doOfferCreate: delay: offers must be funded";

		terResult	= terUNFUNDED;
	}

	if (terSUCCESS == terResult && !saTakerPays.isNative())
	{
		SLE::pointer		sleTakerPays	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uPaysIssuerID));

		if (!sleTakerPays)
		{
			Log(lsWARNING) << "doOfferCreate: delay: can't receive IOUs from non-existant issuer: " << NewcoinAddress::createHumanAccountID(uPaysIssuerID);

			terResult	= terNO_ACCOUNT;
		}
	}

	if (terSUCCESS == terResult)
	{
		STAmount		saOfferPaid;
		STAmount		saOfferGot;
		uint256			uTakeBookBase	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

		Log(lsINFO) << str(boost::format("doOfferCreate: take against book: %s : %s/%s -> %s/%s")
			% uTakeBookBase.ToString()
			% saTakerGets.getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer())
			% saTakerPays.getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer()));

		// Take using the parameters of the offer.
		terResult	= takeOffers(
						bPassive,
						uTakeBookBase,
						mTxnAccountID,
						mTxnAccount,
						saTakerGets,
						saTakerPays,
						saOfferPaid,	// How much was spent.
						saOfferGot		// How much was got.
					);

		Log(lsWARNING) << "doOfferCreate: takeOffers=" << terResult;
		Log(lsWARNING) << "doOfferCreate: takeOffers: saOfferPaid=" << saOfferPaid.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers:  saOfferGot=" << saOfferGot.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();

		if (terSUCCESS == terResult)
		{
			saTakerPays		-= saOfferGot;				// Reduce payin from takers by what offer just got.
			saTakerGets		-= saOfferPaid;				// Reduce payout to takers by what srcAccount just paid.
		}
	}

	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer());
	Log(lsWARNING) << "doOfferCreate: takeOffers: mTxnAccountID=" << NewcoinAddress::createHumanAccountID(mTxnAccountID);
	Log(lsWARNING) << "doOfferCreate: takeOffers:         funds=" << accountFunds(mTxnAccountID, saTakerGets).getFullText();

	// Log(lsWARNING) << "doOfferCreate: takeOffers: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
	// Log(lsWARNING) << "doOfferCreate: takeOffers: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);

	if (terSUCCESS == terResult
		&& !saTakerPays.isZero()						// Still wanting something.
		&& !saTakerGets.isZero()						// Still offering something.
		&& accountFunds(mTxnAccountID, saTakerGets).isPositive())	// Still funded.
	{
		// We need to place the remainder of the offer into its order book.

		// Add offer to owner's directory.
		terResult	= dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(mTxnAccountID), uLedgerIndex);

		if (terSUCCESS == terResult)
		{
			uint256	uBookBase	= Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

			Log(lsINFO) << str(boost::format("doOfferCreate: adding to book: %s : %s/%s -> %s/%s")
				% uBookBase.ToString()
				% saTakerPays.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer())
				% saTakerGets.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer()));

			uDirectory	= Ledger::getQualityIndex(uBookBase, uRate);	// Use original rate.

			// Add offer to order book.
			terResult	= dirAdd(uBookNode, uDirectory, uLedgerIndex);
		}

		if (terSUCCESS == terResult)
		{
			// Log(lsWARNING) << "doOfferCreate: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
			// Log(lsWARNING) << "doOfferCreate: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);
			// Log(lsWARNING) << "doOfferCreate: saTakerPays.isNative()=" << saTakerPays.isNative();
			// Log(lsWARNING) << "doOfferCreate: saTakerGets.isNative()=" << saTakerGets.isNative();
			// Log(lsWARNING) << "doOfferCreate: uPaysCurrency=" << saTakerPays.getHumanCurrency();
			// Log(lsWARNING) << "doOfferCreate: uGetsCurrency=" << saTakerGets.getHumanCurrency();

			sleOffer->setIFieldAccount(sfAccount, mTxnAccountID);
			sleOffer->setIFieldU32(sfSequence, uSequence);
			sleOffer->setIFieldH256(sfBookDirectory, uDirectory);
			sleOffer->setIFieldAmount(sfTakerPays, saTakerPays);
			sleOffer->setIFieldAmount(sfTakerGets, saTakerGets);
			sleOffer->setIFieldU64(sfOwnerNode, uOwnerNode);
			sleOffer->setIFieldU64(sfBookNode, uBookNode);

			if (!saTakerPays.isNative())
				sleOffer->setIFieldAccount(sfPaysIssuer, uPaysIssuerID);

			if (!saTakerGets.isNative())
				sleOffer->setIFieldAccount(sfGetsIssuer, uGetsIssuerID);

			if (uExpiration)
				sleOffer->setIFieldU32(sfExpiration, uExpiration);

			if (bPassive)
				sleOffer->setFlag(lsfPassive);
		}
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCancel(const SerializedTransaction& txn)
{
	TransactionEngineResult	terResult;
	uint32					uSequence	= txn.getITFieldU32(sfOfferSequence);
	uint256					uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer	= entryCache(ltOFFER, uOfferIndex);

	if (sleOffer)
	{
		Log(lsWARNING) << "doOfferCancel: uSequence=" << uSequence;

		terResult	= offerDelete(sleOffer, uOfferIndex, mTxnAccountID);
	}
	else
	{
		Log(lsWARNING) << "doOfferCancel: offer not found: "
			<< NewcoinAddress::createHumanAccountID(mTxnAccountID)
			<< " : " << uSequence
			<< " : " << uOfferIndex.ToString();

		terResult	= terOFFER_NOT_FOUND;
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doTake(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doStore(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doDelete(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

// vim:ts=4
