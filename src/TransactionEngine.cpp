//
// XXX Should make sure all fields and are recognized on a transactions.
// XXX Make sure fee is claimed for failed transactions.
//

#include "TransactionEngine.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>

#include "../json/writer.h"

#include "Config.h"
#include "TransactionFormats.h"
#include "utils.h"
#include "Log.h"

#define DIR_NODE_MAX	32

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		TransactionEngineResult	terCode;
		const char*				cpToken;
		const char*				cpHuman;
	} transResultInfoA[] = {
		{	tenBAD_ADD_AUTH,		"tenBAD_ADD_AUTH",			"Not authorized to add account."					},
		{	tenBAD_AMOUNT,			"tenBAD_AMOUNT",			"Can only send positive amounts."					},
		{	tenBAD_AUTH_MASTER,		"tenBAD_AUTH_MASTER",		"Auth for unclaimed account needs correct master key."	},
		{	tenBAD_CLAIM_ID,		"tenBAD_CLAIM_ID",			"Malformed."										},
		{	tenBAD_EXPIRATION,		"tenBAD_EXPIRATION",		"Malformed."										},
		{	tenBAD_GEN_AUTH,		"tenBAD_GEN_AUTH",			"Not authorized to claim generator."				},
		{	tenBAD_ISSUER,			"tenBAD_ISSUER",			"Malformed."										},
		{	tenBAD_OFFER,			"tenBAD_OFFER",				"Malformed."										},
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
		{	terNODE_NO_ROOT,        "terNODE_NO_ROOT",			"Directory doesn't exist."													},
		{	terNO_ACCOUNT,			"terNO_ACCOUNT",			"The source account does not exist"					},
		{	terNO_DST,				"terNO_DST",				"The destination does not exist"					},
		{	terNO_LINE_NO_ZERO,		"terNO_LINE_NO_ZERO",		"Can't zero non-existant line, destination might make it."	},
		{	terNO_PATH,				"terNO_PATH",				"No path existed or met transaction/balance requirements"	},
		{	terOFFER_NOT_FOUND,		"terOFFER_NOT_FOUND",		"Can not cancel offer."						},
		{	terOVER_LIMIT,			"terOVER_LIMIT",			"Over limit."										},
		{	terPAST_LEDGER,			"terPAST_LEDGER",			"The transaction expired and can't be applied"		},
		{	terPAST_SEQ,			"terPAST_SEQ",				"This sequence number has already past"				},
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

// <->     accounts: Affected accounts for the transaction.
// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TransactionEngineResult TransactionEngine::dirAdd(
	std::vector<AffectedAccount>&	accounts,
	uint64&							uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	SLE::pointer		sleNode;
	STVector256			svIndexes;
	LedgerStateParms	lspRoot		= lepNONE;
	SLE::pointer		sleRoot		= mLedger->getDirNode(lspRoot, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		Log(lsTRACE) << "dirAdd: Creating dir root: " << uRootIndex.ToString();

		uNodeDir	= 0;

		sleRoot	= boost::make_shared<SerializedLedgerEntry>(ltDIR_NODE);
		sleRoot->setIndex(uRootIndex);

		sleNode	= sleRoot;

		accounts.push_back(std::make_pair(taaCREATE, sleRoot));
	}
	else
	{
		uNodeDir		= sleRoot->getIFieldU64(sfIndexPrevious);

		uint64		uNodePrevious	= uNodeDir;
		uint256		uNodeIndex;

		if (uNodeDir)
		{
			// Try adding to non-root.
			uNodeIndex	= Ledger::getDirNodeIndex(uRootIndex, uNodeDir);
			lspRoot		= lepNONE;
			sleNode		= mLedger->getDirNode(lspRoot, uNodeIndex);
		}
		else
		{
			// Try adding to root.
			uNodeIndex		= uRootIndex;
		}

		svIndexes	= sleNode->getIFieldV256(sfIndexes);

		if (DIR_NODE_MAX != svIndexes.peekValue().size())
		{
			// Add to current node.
			accounts.push_back(std::make_pair(taaMODIFY, sleNode));
		}
		// Add to new node.
		else if (!++uNodeDir)
		{
			return terDIR_FULL;
		}
		else
		{
			// Have root point to new node.
			sleRoot->setIFieldU64(sfIndexPrevious, uNodeDir);
			accounts.push_back(std::make_pair(taaMODIFY, sleRoot));

			// Have old last point to new node, if it was not root.
			if (uNodePrevious)
			{
				// Previous node is not root node.
				sleNode->setIFieldU64(sfIndexNext, uNodeDir);
				accounts.push_back(std::make_pair(taaMODIFY, sleNode));
			}

			// Create the new node.
			svIndexes	= STVector256();
			sleNode		= boost::make_shared<SerializedLedgerEntry>(ltDIR_NODE);
			sleNode->setIndex(uNodeIndex);

			if (uNodePrevious)
				sleNode->setIFieldU64(sfIndexPrevious, uNodePrevious);

			accounts.push_back(std::make_pair(taaCREATE, sleNode));
		}
	}

	svIndexes.peekValue().push_back(uLedgerIndex);	// Append entry.
	sleNode->setIFieldV256(sfIndexes, svIndexes);	// Save entry.

	Log(lsTRACE) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();
	Log(lsTRACE) << "dirAdd:  appending: Node: " << strHex(uNodeDir);
	Log(lsTRACE) << "dirAdd:  appending: Entry: " << uLedgerIndex.ToString();

	return terSUCCESS;
}

// <->     accounts: Affected accounts for the transaction.
// -->     uNodeDir: Node containing entry.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Ledger must be in a state for this to work.
TransactionEngineResult TransactionEngine::dirDelete(
	std::vector<AffectedAccount>&	accounts,
	const uint64&					uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	uint64				uNodeCur	= uNodeDir;
	uint256				uNodeIndex	= Ledger::getDirNodeIndex(uRootIndex, uNodeCur);
	LedgerStateParms	lspNode		= lepNONE;
	SLE::pointer		sleNode		= mLedger->getDirNode(lspNode, uNodeIndex);

	assert(sleNode);

	if (!sleNode)
	{
		Log(lsWARNING) << "dirDelete: no such node";

		return terBAD_LEDGER;
	}
	else
	{
		STVector256						svIndexes	= sleNode->getIFieldV256(sfIndexes);
		std::vector<uint256>&			vuiIndexes	= svIndexes.peekValue();
		std::vector<uint256>::iterator	it;

		it = std::find(vuiIndexes.begin(), vuiIndexes.end(), uLedgerIndex);

		assert(vuiIndexes.end() != it);
		if (vuiIndexes.end() == it)
		{
			Log(lsWARNING) << "dirDelete: node not mentioned";

			return terBAD_LEDGER;
		}
		else
		{
			// Remove the element.
			if (vuiIndexes.size() > 1)
				*it = vuiIndexes[vuiIndexes.size()-1];

			vuiIndexes.resize(vuiIndexes.size()-1);

			if (vuiIndexes.size() > 0)
			{
				// Node is not being deleted.
				sleNode->setIFieldV256(sfIndexes, svIndexes);
				accounts.push_back(std::make_pair(taaMODIFY, sleNode));
			}
			else
			{
				bool			bRootDirty	= false;
				SLE::pointer	sleRoot;

				// May be able to delete nodes.
				if (uNodeCur)
				{
					uint64	uNodePrevious	= sleNode->getIFieldU64(sfIndexPrevious);
					uint64	uNodeNext		= sleNode->getIFieldU64(sfIndexNext);

					accounts.push_back(std::make_pair(taaDELETE, sleNode));

					// Fix previous link.
					if (uNodePrevious)
					{
						LedgerStateParms	lspPrevious		= lepNONE;
						SLE::pointer		slePrevious		= mLedger->getDirNode(lspPrevious, Ledger::getDirNodeIndex(uRootIndex, uNodePrevious));

						assert(slePrevious);
						if (!slePrevious)
						{
							Log(lsWARNING) << "dirDelete: previous node is missing";

							return terBAD_LEDGER;
						}
						else if (uNodeNext)
						{
							slePrevious->setIFieldU64(sfIndexNext, uNodeNext);
						}
						else
						{
							slePrevious->makeIFieldAbsent(sfIndexNext);
						}
						accounts.push_back(std::make_pair(taaMODIFY, slePrevious));
					}
					else
					{
						// Read root.
						bRootDirty	= true;

						sleRoot	= mLedger->getDirNode(lspNode, uRootIndex);

						if (uNodeNext)
						{
							sleRoot->setIFieldU64(sfIndexNext, uNodeNext);
						}
						else
						{
							sleRoot->makeIFieldAbsent(sfIndexNext);
						}
					}

					// Fix next link.
					if (uNodeNext)
					{
						LedgerStateParms	lspNext		= lepNONE;
						SLE::pointer		sleNext		= mLedger->getDirNode(lspNext, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));

						assert(sleNext);
						if (!sleNext)
						{
							Log(lsWARNING) << "dirDelete: next node is missing";

							return terBAD_LEDGER;
						}
						else if (uNodeNext)
						{
							sleNext->setIFieldU64(sfIndexNext, uNodeNext);
						}
						else
						{
							sleNext->makeIFieldAbsent(sfIndexNext);
						}
						accounts.push_back(std::make_pair(taaMODIFY, sleNext));
					}
					else
					{
						// Read root.
						bRootDirty	= true;

						sleRoot	= mLedger->getDirNode(lspNode, uRootIndex);

						if (uNodePrevious)
						{
							sleRoot->setIFieldU64(sfIndexPrevious, uNodePrevious);
						}
						else
						{
							sleRoot->makeIFieldAbsent(sfIndexPrevious);
						}
					}

					if (bRootDirty)
					{
						// Need to update sleRoot;
						uNodeCur	= 0;

						// If we might be able to delete root, load it.
						if (!uNodePrevious && !uNodeNext)
							vuiIndexes	= svIndexes.peekValue();
					}
				}
				else
				{
					bRootDirty	= true;
				}

				if (!uNodeCur)
				{
					// Looking at root node.
					uint64	uRootPrevious	= sleNode->getIFieldU64(sfIndexPrevious);
					uint64	uRootNext		= sleNode->getIFieldU64(sfIndexNext);

					if (!uRootPrevious && !uRootNext && vuiIndexes.empty())
					{
						accounts.push_back(std::make_pair(taaDELETE, sleRoot));
					}
					else if (bRootDirty)
					{
						accounts.push_back(std::make_pair(taaMODIFY, sleRoot));
					}
				}
			}
		}

		return terSUCCESS;
	}
}

// Set the authorized public ket for an account.  May also set the generator map.
TransactionEngineResult	TransactionEngine::setAuthorized(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts, bool bMustSetGenerator)
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

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleGen			= mLedger->getGenerator(qry, hGeneratorID);
	if (!sleGen)
	{
		Log(lsTRACE) << "createGenerator: creating generator";
		// Create the generator.
						sleGen			= boost::make_shared<SerializedLedgerEntry>(ltGENERATOR_MAP);

		sleGen->setIndex(Ledger::getGeneratorIndex(hGeneratorID));
		sleGen->setIFieldVL(sfGenerator, vucCipher);

		accounts.push_back(std::make_pair(taaCREATE, sleGen));
	}
	else if (bMustSetGenerator)
	{
		// Doing a claim.  Must set generator.
		// Generator is already in use.  Regular passphrases limited to one wallet.
		Log(lsWARNING) << "createGenerator: generator already in use";

		return tenGEN_IN_USE;
	}

	// Set the public key needed to use the account.
	SLE::pointer		sleDst			= accounts[0].second;

	uint160				uAuthKeyID		= bMustSetGenerator
											? hGeneratorID								// Claim
											: txn.getITFieldAccount(sfAuthorizedKey);	// PasswordSet

	sleDst->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return terSUCCESS;
}

Ledger::pointer TransactionEngine::getTransactionLedger(uint32 targetLedger)
{
	Ledger::pointer ledger = mDefaultLedger;
	if (mAlternateLedger && (targetLedger != 0) &&
		(targetLedger != mLedger->getLedgerSeq()) && (targetLedger == mAlternateLedger->getLedgerSeq()))
	{
		Log(lsINFO) << "Transaction goes into wobble ledger";
		ledger = mAlternateLedger;
	}
	return ledger;
}

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params, uint32 targetLedger)
{
	return applyTransaction(txn, params, getTransactionLedger(targetLedger));
}

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params, Ledger::pointer ledger)
{
	Log(lsTRACE) << "applyTransaction>";
	mLedger = ledger;

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

	TransactionEngineResult result = terSUCCESS;

	uint256 txID = txn.getTransactionID();
	if (!txID)
	{
		Log(lsWARNING) << "applyTransaction: invalid transaction id";

		result	= tenINVALID;
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

	if (terSUCCESS == result)
		naSigningPubKey	= NewcoinAddress::createAccountPublic(txn.peekSigningPubKey());

	// Consistency: really signed.
	if (terSUCCESS == result && !txn.checkSign(naSigningPubKey))
	{
		Log(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

		result	= tenINVALID;
	}

	STAmount	saCost		= theConfig.FEE_DEFAULT;

	// Customize behavoir based on transaction type.
	if (terSUCCESS == result)
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
					LedgerStateParms	qry				= lepNONE;
					SLE::pointer		sleNickname		= mLedger->getNickname(qry, txn.getITFieldH256(sfNickname));

					if (!sleNickname)
						saCost	= theConfig.FEE_NICKNAME_CREATE;
				}
				break;

			case ttACCOUNT_SET:
			case ttCREDIT_SET:
			case ttINVOICE:
			case ttOFFER:
			case ttOFFER_CANCEL:
			case ttPASSWORD_FUND:
			case ttTRANSIT_SET:
			case ttWALLET_ADD:
				nothing();
				break;

			case ttINVALID:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: ttINVALID transaction type";
				result = tenINVALID;
				break;

			default:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
				result = tenUNKNOWN;
				break;
		}
	}

	STAmount saPaid = txn.getTransactionFee();

	if (terSUCCESS == result && (params & tepNO_CHECK_FEE) == tepNONE)
	{
		if (!saCost.isZero())
		{
			if (saPaid < saCost)
			{
				Log(lsINFO) << "applyTransaction: insufficient fee";

				result	= tenINSUF_FEE_P;
			}
		}
		else
		{
			if (!saPaid.isZero())
			{
				// Transaction is malformed.
				Log(lsWARNING) << "applyTransaction: fee not allowed";

				result	= tenINSUF_FEE_P;
			}
		}
	}

	// Get source account ID.
	uint160 srcAccountID = txn.getSourceAccount().getAccountID();
	if (terSUCCESS == result && !srcAccountID)
	{
		Log(lsWARNING) << "applyTransaction: bad source id";

		result	= tenINVALID;
	}

	if (terSUCCESS != result)
	{
		// Avoid unnecessary locking.
		mLedger = Ledger::pointer();

		return result;
	}

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	// find source account
	// If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probablistic.

	STAmount			saSrcBalance;
	uint32				t_seq			= txn.getSequence();
	LedgerStateParms	lspRoot			= lepNONE;
	SLE::pointer		sleSrc			= mLedger->getAccountRoot(lspRoot, srcAccountID);
	bool				bHaveAuthKey	= false;

	if (!sleSrc)
	{
		Log(lsTRACE) << str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		result			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= sleSrc->getIValueFieldAmount(sfBalance);
		bHaveAuthKey	= sleSrc->getIFieldPresent(sfAuthorizedKey);
	}

	// Check if account cliamed.
	if (terSUCCESS == result)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				if (bHaveAuthKey)
				{
					Log(lsWARNING) << "applyTransaction: Account already claimed.";

					result	= tenCLAIMED;
				}
				break;

			default:
				nothing();
				break;
		}
	}

	// Consistency: Check signature
	if (terSUCCESS == result)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != srcAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					result	= tenBAD_CLAIM_ID;
				}
				break;

			case ttPASSWORD_SET:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != srcAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					result	= tenBAD_SET_ID;
				}
				break;

			default:
				// Verify the transaction's signing public key is the key authorized for signing.
				if (bHaveAuthKey && naSigningPubKey.getAccountID() == sleSrc->getIValueFieldAccount(sfAuthorizedKey).getAccountID())
				{
					// Authorized to continue.
					nothing();
				}
				else if (naSigningPubKey.getAccountID() == srcAccountID)
				{
					// Authorized to continue.
					nothing();
				}
				else if (bHaveAuthKey)
				{
					std::cerr << "applyTransaction: Delay: Not authorized to use account." << std::endl;

					result	= terBAD_AUTH;
				}
				else
				{
					std::cerr << "applyTransaction: Invalid: Not authorized to use account." << std::endl;

					result	= tenBAD_AUTH_MASTER;
				}
				break;
		}
	}

	// Deduct the fee, so it's not available during the transaction.
	// Will only write the account back, if the transaction succeeds.
	if (terSUCCESS != result || saCost.isZero())
	{
		nothing();
	}
	else if (saSrcBalance < saPaid)
	{
		std::cerr
			<< str(boost::format("applyTransaction: Delay: insufficent balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText())
			<< std::endl;

		result	= terINSUF_FEE_B;
	}
	else
	{
		sleSrc->setIFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (terSUCCESS != result)
	{
		nothing();
	}
	else if (!saCost.isZero())
	{
		uint32 a_seq = sleSrc->getIFieldU32(sfSequence);
		Log(lsTRACE) << "Aseq=" << a_seq << ", Tseq=" << t_seq;
		if (t_seq != a_seq)
		{
			if (a_seq < t_seq)
			{
				Log(lsTRACE) << "applyTransaction: future sequence number";

				result	= terPRE_SEQ;
			}
			else if (mLedger->hasTransaction(txID))
			{
				std::cerr << "applyTransaction: duplicate sequence number" << std::endl;

				result	= terALREADY;
			}
			else
			{
				std::cerr << "applyTransaction: past sequence number" << std::endl;

				result	= terPAST_SEQ;
			}
		}
		else
		{
			sleSrc->setIFieldU32(sfSequence, t_seq + 1);
		}
	}
	else
	{
		Log(lsINFO) << "Zero cost transaction";
		if (t_seq)
		{
			std::cerr << "applyTransaction: bad sequence for pre-paid transaction" << std::endl;

			result	= terPAST_SEQ;
		}
	}

	std::vector<AffectedAccount> accounts;

	if (terSUCCESS == result)
	{
		accounts.push_back(std::make_pair(taaMODIFY, sleSrc));

		switch(txn.getTxnType())
		{
			case ttACCOUNT_SET:
				result = doAccountSet(txn, accounts);
				break;

			case ttCLAIM:
				result = doClaim(txn, accounts);
				break;

			case ttCREDIT_SET:
				result = doCreditSet(txn, accounts, srcAccountID);
				break;

			case ttINVALID:
				std::cerr << "applyTransaction: invalid type" << std::endl;
				result = tenINVALID;
				break;

			case ttINVOICE:
				result = doInvoice(txn, accounts);
				break;

			case ttOFFER:
				result = doOffer(txn, accounts, srcAccountID);
				break;

			case ttOFFER_CANCEL:
				result = doOfferCancel(txn, accounts, srcAccountID);
				break;

			case ttNICKNAME_SET:
				result = doNicknameSet(txn, accounts, srcAccountID);
				break;

			case ttPASSWORD_FUND:
				result = doPasswordFund(txn, accounts, srcAccountID);
				break;

			case ttPASSWORD_SET:
				result = doPasswordSet(txn, accounts);
				break;

			case ttPAYMENT:
				result = doPayment(txn, accounts, srcAccountID);
				break;

			case ttTRANSIT_SET:
				result = doTransitSet(txn, accounts);
				break;

			case ttWALLET_ADD:
				result = doWalletAdd(txn, accounts);
				break;

			default:
				result = tenUNKNOWN;
				break;
		}
	}

	if (terSUCCESS == result)
	{ // Write back the account states and add the transaction to the ledger
		for (std::vector<AffectedAccount>::iterator it = accounts.begin(), end = accounts.end();
			it != end; ++it)
		{
			if (it->first == taaCREATE)
			{
				std::cerr << "applyTransaction: taaCREATE: " << it->second->getText() << std::endl;

				if (mLedger->writeBack(lepCREATE, it->second) & lepERROR)
					assert(false);
			}
			else if (it->first == taaMODIFY)
			{
				std::cerr << "applyTransaction: taaMODIFY: " << it->second->getText() << std::endl;

				if (mLedger->writeBack(lepNONE, it->second) & lepERROR)
					assert(false);
			}
			else if (it->first == taaDELETE)
			{
				std::cerr << "applyTransaction: taaDELETE: " << it->second->getText() << std::endl;

				if (!mLedger->peekAccountStateMap()->delItem(it->second->getIndex()))
					assert(false);
			}
		}

		Serializer s;
		txn.add(s);
		if (!mLedger->addTransaction(txID, s))
			assert(false);
		if ((params & tepUPDATE_TOTAL) != tepNONE)
			mLedger->destroyCoins(saPaid.getNValue());
	}

	mLedger = Ledger::pointer();

	return result;
}

TransactionEngineResult TransactionEngine::doAccountSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts)
{
	std::cerr << "doAccountSet>" << std::endl;

	SLE::pointer		sleSrc			= accounts[0].second;
	uint32				txFlags			= txn.getFlags();

	//
	// EmailHash
	//

	if (txFlags & tfUnsetEmailHash)
	{
		std::cerr << "doAccountSet: unset email hash" << std::endl;

		sleSrc->makeIFieldAbsent(sfEmailHash);
	}
	else if (txn.getITFieldPresent(sfEmailHash))
	{
		std::cerr << "doAccountSet: set email hash" << std::endl;

		sleSrc->setIFieldH128(sfEmailHash, txn.getITFieldH128(sfEmailHash));
	}

	//
	// WalletLocator
	//

	if (txFlags & tfUnsetWalletLocator)
	{
		std::cerr << "doAccountSet: unset wallet locator" << std::endl;

		sleSrc->makeIFieldAbsent(sfWalletLocator);
	}
	else if (txn.getITFieldPresent(sfWalletLocator))
	{
		std::cerr << "doAccountSet: set wallet locator" << std::endl;

		sleSrc->setIFieldH256(sfWalletLocator, txn.getITFieldH256(sfWalletLocator));
	}

	//
	// MessageKey
	//

	if (!txn.getITFieldPresent(sfMessageKey))
	{
		nothing();

	}
	else if (sleSrc->getIFieldPresent(sfMessageKey))
	{
		std::cerr << "doAccountSet: can not change message key" << std::endl;

		return tenMSG_SET;
	}
	else
	{
		std::cerr << "doAccountSet: set message key" << std::endl;

		sleSrc->setIFieldVL(sfMessageKey, txn.getITFieldVL(sfMessageKey));
	}

	std::cerr << "doAccountSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doClaim(const SerializedTransaction& txn,
	 std::vector<AffectedAccount>& accounts)
{
	std::cerr << "doClaim>" << std::endl;

	TransactionEngineResult	result	= setAuthorized(txn, accounts, true);

	std::cerr << "doClaim<" << std::endl;

	return result;
}

TransactionEngineResult TransactionEngine::doCreditSet(const SerializedTransaction& txn,
	std::vector<AffectedAccount>&accounts,
	const uint160& uSrcAccountID)
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
	else if (uSrcAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Can not extend credit to self.";

		return tenDST_IS_SRC;
	}

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleDst			= mLedger->getAccountRoot(qry, uDstAccountID);
	if (!sleDst)
	{
		Log(lsINFO) << "doCreditSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	STAmount			saLimitAmount	= txn.getITFieldAmount(sfLimitAmount);
	uint160				uCurrency		= saLimitAmount.getCurrency();
	bool				bSltD			= uSrcAccountID < uDstAccountID;
	uint32				uFlags			= bSltD ? lsfLowIndexed : lsfHighIndexed;
	STAmount			saBalance(uCurrency);
	bool				bAddIndex		= false;
	bool				bDelIndex		= false;

						qry				= lepNONE;
	SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, uSrcAccountID, uDstAccountID, uCurrency);
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
				terResult	= dirDelete(accounts, uSrcRef, Ledger::getRippleDirIndex(uSrcAccountID), sleRippleState->getIndex());
			}
		}
#endif
		if (!bDelIndex)
		{
			bAddIndex		= !(sleRippleState->getFlags() & uFlags);

			sleRippleState->setIFieldAmount(bSltD ? sfLowLimit : sfHighLimit, saLimitAmount);

			if (bAddIndex)
				sleRippleState->setFlag(uFlags);

			accounts.push_back(std::make_pair(taaMODIFY, sleRippleState));
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
		STAmount		saZero(uCurrency);

						bAddIndex		= true;
						sleRippleState	= boost::make_shared<SerializedLedgerEntry>(ltRIPPLE_STATE);

		sleRippleState->setIndex(Ledger::getRippleStateIndex(uSrcAccountID, uDstAccountID, uCurrency));
		Log(lsINFO) << "doCreditSet: Creating ripple line: "
			<< sleRippleState->getIndex().ToString();

		sleRippleState->setFlag(uFlags);
		sleRippleState->setIFieldAmount(sfBalance, saZero);	// Zero balance in currency.
		sleRippleState->setIFieldAmount(bSltD ? sfLowLimit : sfHighLimit, saLimitAmount);
		sleRippleState->setIFieldAmount(bSltD ? sfHighLimit : sfLowLimit, saZero);
		sleRippleState->setIFieldAccount(bSltD ? sfLowID : sfHighID, uSrcAccountID);
		sleRippleState->setIFieldAccount(bSltD ? sfHighID : sfLowID, uDstAccountID);

		accounts.push_back(std::make_pair(taaCREATE, sleRippleState));
	}

	if (bAddIndex)
	{
		uint64			uSrcRef;	// Ignored, ripple_state dirs never delete.

		// XXX Make dirAdd more flexiable to take vector.
		terResult	= dirAdd(accounts, uSrcRef, Ledger::getRippleDirIndex(uSrcAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doNicknameSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts, const uint160& uSrcAccountID)
{
	std::cerr << "doNicknameSet>" << std::endl;

	SLE::pointer		sleSrc			= accounts[0].second;

	uint256				uNickname		= txn.getITFieldH256(sfNickname);
	bool				bMinOffer		= txn.getITFieldPresent(sfMinimumOffer);
	STAmount			saMinOffer		= bMinOffer ? txn.getITFieldAmount(sfAmount) : STAmount();

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleNickname		= mLedger->getNickname(qry, uNickname);

	if (sleNickname)
	{
		// Edit old entry.
		sleNickname->setIFieldAccount(sfAccount, uSrcAccountID);

		if (bMinOffer && !saMinOffer.isZero())
		{
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
		}
		else
		{
			sleNickname->makeIFieldAbsent(sfMinimumOffer);
		}

		accounts.push_back(std::make_pair(taaMODIFY, sleNickname));
	}
	else
	{
		// Make a new entry.
		// XXX Need to include authorization limiting.

						sleNickname	= boost::make_shared<SerializedLedgerEntry>(ltNICKNAME);

		sleNickname->setIndex(Ledger::getNicknameIndex(uNickname));
		std::cerr << "doNicknameSet: Creating nickname node: " << sleNickname->getIndex().ToString() << std::endl;

		sleNickname->setIFieldAccount(sfAccount, uSrcAccountID);

		if (bMinOffer && !saMinOffer.isZero())
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);

		accounts.push_back(std::make_pair(taaCREATE, sleNickname));
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordFund(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts, const uint160& uSrcAccountID)
{
	std::cerr << "doPasswordFund>" << std::endl;

	uint160				uDstAccountID	= txn.getITFieldAccount(sfDestination);
	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleSrc			= accounts[0].second;
	SLE::pointer		sleDst			= uSrcAccountID == uDstAccountID
											? sleSrc
											: mLedger->getAccountRoot(qry, uDstAccountID);
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

		if (uSrcAccountID != uDstAccountID) {
			std::cerr << "doPasswordFund: Destination modified." << std::endl;
			accounts.push_back(std::make_pair(taaMODIFY, sleDst));
		}
	}

	std::cerr << "doPasswordFund<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordSet(const SerializedTransaction& txn, std::vector<AffectedAccount>& accounts)
{
	std::cerr << "doPasswordSet>" << std::endl;

	SLE::pointer		sleSrc			= accounts[0].second;

	if (sleSrc->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	sleSrc->setFlag(lsfPasswordSpent);

	TransactionEngineResult	result	= setAuthorized(txn, accounts, false);

	std::cerr << "doPasswordSet<" << std::endl;

	return result;
}

#ifdef WORK_IN_PROGRESS
TransactionEngineResult calcOfferFill(SAAmount& saSrc, paymentNode& pnSrc, paymentNode& pnDst)
{
	TransactionEngineResult	terResult;

	if (!saSrc.isZero())
	{

	}

	return bSuccess;
}

// Find offers to satisfy pnDst.
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

	terResult	= calcOfferFill(pnSrc.saIOURedeem, pnSrc, pnDst, bAllowPartial);

	if (terSUCCESS == terResult)
	{
		terResult	= calcOfferFill(pnSrc.saIOUIssue, pnSrc, pnDst, bAllowPartial)
	}

	if (terSUCCESS == terResult && !bAllowPartial)
	{
		STAmount	saTotal	= pnSrc.saIOURedeem;
		saTotal	+= pnSrc.saIOUIssue;

		if (saTotal != saWanted)
			terResult	= terINSUF_PATH;
	}

	return terResult;
}

// From the destination work towards the source calculating how much must be asked for.
// --> bAllowPartial: If false, can fail if can't meet requirements.
// <-- bSuccess: true=success, false=insufficient funds.
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
bool calcPaymentReverse(std::vector<paymentNode>& pnNodes, bool bAllowPartial)
{
	bool	bDone		= false;
	bool	bSuccess	= false;

	// path: dst .. src

	while (!bDone)
	{
		if (cur->saWanted.isZero())
		{
			// Must want something.
			terResult	= terINVALID;
			bDone		= true;
		}
		else if (cur->saWanted.isNative())
		{
			if (prv->how == direct)
			{
				// Stamp transfer desired.
				if (prv->prev())
				{
					// More entries before stamp transfer.
					terResult	= terINVALID;
					bDone		= true;
				}
				else if (prv->account->saBalance() >= cur->saWanted)
				{
					// Transfer stamps.
					prv->saSend = cur->saWanted;
					bDone		= true;
					bSuccess	= true;
				}
				else
				{
					// Insufficient funds for transfer
					bDone		= true;
				}
			}
			else
			{
				// Must convert to stamps via offer.
				if (calcOfferFill(prv, cur, bAllowPartial))
				{

				}
				else
				{
					bDone	= false;
				}
			}
		}
		else
		{
			// Rippling.

		}
	}
}

// From the source work toward the destination calculate how much is transfered at each step and finally.
// <-> pnNodes:
//     --> [0]saWanted.mAmount
//     --> [all]saWanted.saSend
//     --> [all]saWanted.IOURedeem
//     --> [all]saWanted.IOUIssue
//     --> [all]saAccount
bool calcPaymentForward(std::vector<paymentNode>& pnNodes)
{
	cur = src;

	if (!cur->saSend.isZero())
	{
		// Sending stamps - always final step.
		assert(!cur->next);
		nxt->saReceive	= cur->saSend;
		bDone			= true;
	}
	else
	{
		// Rippling.

	}
}
#endif

// XXX Need to audit for things like setting accountID not having memory.
TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts,
	const uint160& uSrcAccountID)
{
	// Ripple if source or destination is non-native or if there are paths.
	uint32		txFlags			= txn.getFlags();
	bool		bCreate			= !!(txFlags & tfCreateAccount);
	bool		bNoRippleDirect	= !!(txFlags & tfNoRippleDirect);
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
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getCurrencyHuman() << " " << saDstAmount.getText();

		return tenBAD_AMOUNT;
	}
	else if (uSrcAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% uSrcAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return tenREDUNDANT;
	}

	LedgerStateParms	qry		= lepNONE;
	SLE::pointer		sleDst	= mLedger->getAccountRoot(qry, uDstAccountID);
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
		sleDst = boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);

		sleDst->setIndex(Ledger::getAccountRootIndex(uDstAccountID));
		sleDst->setIFieldAccount(sfAccount, uDstAccountID);
		sleDst->setIFieldU32(sfSequence, 1);

		accounts.push_back(std::make_pair(taaCREATE, sleDst));
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
		accounts.push_back(std::make_pair(taaMODIFY, sleDst));
	}

	bool		bRipple			= bPaths || bMax || !saDstAmount.isNative();

	if (!bRipple)
	{
		// Direct XNS payment.
		STAmount	saSrcXNSBalance	= accounts[0].second->getIValueFieldAmount(sfBalance);

		if (saSrcXNSBalance < saDstAmount)
		{
			// Transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficent funds.";

			return terUNFUNDED;
		}

		accounts[0].second->setIFieldAmount(sfBalance, saSrcXNSBalance - saDstAmount);
		accounts[1].second->setIFieldAmount(sfBalance, accounts[1].second->getIValueFieldAmount(sfBalance) + saDstAmount);

		return terSUCCESS;
	}

	//
	// Ripple payment
	//

	// Try direct ripple first.
	if (!bNoRippleDirect && uSrcAccountID != uDstAccountID && uSrcCurrency == uDstCurrency)
	{
							qry				= lepNONE;
		SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, uSrcAccountID, uDstAccountID, uDstCurrency);

		if (sleRippleState)
		{
			// There is a direct credit-line of some direction.
			// - We can always pay IOUs back.
			// - We can issue IOUs to the limit.
			// XXX Not implemented:
			// - Give preference to pushing out IOUs over sender credit limit.
			// - Give preference to pushing out IOUs to creating them.
			// - Create IOUs as last resort.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bSendHigh		= uLowID == uSrcAccountID && uHighID == uDstAccountID;
			bool		bSendLow		= uLowID == uDstAccountID && uHighID == uSrcAccountID;
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

				terResult	= dirAdd(accounts,
					uSrcRef,
					Ledger::getRippleDirIndex(uSrcAccountID),		// The source ended up owing.
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
			accounts.push_back(std::make_pair(taaMODIFY, sleRippleState));

			return terSUCCESS;
		}
	}

	STPathSet	spsPaths = txn.getITFieldPathSet(sfPaths);

	// XXX If we are parsing for determing forwarding check maximum path count.
	if (!spsPaths.getPathCount())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: No paths.";

		return tenRIPPLE_EMPTY;
	}
#if 0
	std::vector<STPath> spPath;

	BOOST_FOREACH(std::vector<STPath>& spPath, spsPaths)
	{

		Log(lsINFO) << "doPayment: Implementation error: Not implemented.";

		return tenUNKNOWN;
	}
#endif

	Log(lsINFO) << "doPayment: Delay transaction: No ripple paths could be satisfied.";

	return terBAD_RIPPLE;
}

TransactionEngineResult TransactionEngine::doTransitSet(const SerializedTransaction& st, std::vector<AffectedAccount>&)
{
	std::cerr << "doTransitSet>" << std::endl;
#if 0
	SLE::pointer	sleSrc	= accounts[0].second;

	bool	bTxnTransitRate			= st->getIFieldPresent(sfTransitRate);
	bool	bTxnTransitStart		= st->getIFieldPresent(sfTransitStart);
	bool	bTxnTransitExpire		= st->getIFieldPresent(sfTransitExpire);
	uint32	uTxnTransitRate			= bTxnTransitRate ? st->getIFieldU32(sfTransitRate) : 0;
	uint32	uTxnTransitStart		= bTxnTransitStart ? st->getIFieldU32(sfTransitStart) : 0;
	uint32	uTxnTransitExpire		= bTxnTransitExpire ? st->getIFieldU32(sfTransitExpire) : 0;

	bool	bActTransitRate			= sleSrc->getIFieldPresent(sfTransitRate);
	bool	bActTransitExpire		= sleSrc->getIFieldPresent(sfTransitExpire);
	bool	bActNextTransitRate		= sleSrc->getIFieldPresent(sfNextTransitRate);
	bool	bActNextTransitStart	= sleSrc->getIFieldPresent(sfNextTransitStart);
	bool	bActNextTransitExpire	= sleSrc->getIFieldPresent(sfNextTransitExpire);
	uint32	uActTransitRate			= bActTransitRate ? sleSrc->getIFieldU32(sfTransitRate) : 0;
	uint32	uActTransitExpire		= bActTransitExpire ? sleSrc->getIFieldU32(sfTransitExpire) : 0;
	uint32	uActNextTransitRate		= bActNextTransitRate ? sleSrc->getIFieldU32(sfNextTransitRate) : 0;
	uint32	uActNextTransitStart	= bActNextTransitStart ? sleSrc->getIFieldU32(sfNextTransitStart) : 0;
	uint32	uActNextTransitExpire	= bActNextTransitExpire ? sleSrc->getIFieldU32(sfNextTransitExpire) : 0;

	//
	// Update view
	//

	bool	bNoCurrent		= !bActTransitRate;
	bool	bCurrentExpired	=
		bActTransitExpire						// Current can expire
			&& bActNextTransitStart				// Have a replacement
			&& uActTransitExpire <= uLedger;	// Current is expired

	// Replace current with next if need.
	if (bNoCurrent								// No current.
		&& bActNextTransitRate					// Have next.
		&& uActNextTransitStart <= uLedger)		// Next has started.
	{
		// Make next current.
		uActTransitRate			= uActNextTransitRate;
		bActTransitExpire		= bActNextTransitStart;
		uActTransitExpire		= uActNextTransitExpire;

		// Remove next.
		uActNextTransitStart	= 0;
	}

	//
	// Determine new transaction deposition.
	//

	bool	bBetterThanCurrent =
		!no current
			|| (
				Expires same or later than current
				Start before or same as current
				Fee same or less than current
			)

	bool	bBetterThanNext =
		!no next
			|| (
				Expires same or later than next
				Start before or same as next
				Fee same or less than next
			)

	bool	bBetterThanBoth =
		bBetterThanCurrent && bBetterThanNext

	bool	bCurrentBlocks =
		!bBetterThanCurrent
		&& overlaps with current

	bool	bNextBlocks =
		!bBetterThanNext
		&& overlaps with next

	if (bBetterThanBoth)
	{
		// Erase both and install.

		// If not starting now, install as next.
	}
	else if (bCurrentBlocks || bNextBlocks)
	{
		// Invalid ignore
	}
	else if (bBetterThanCurrent)
	{
		// Install over current
	}
	else if (bBetterThanNext)
	{
		// Install over next
	}
	else
	{
		// Error.
	}

	return tenTRANSIT_WORSE;

	// Set current.
	uDstTransitRate			= uTxnTransitRate;
	uDstTransitExpire		= uTxnTransitExpire;	// 0 for never expire.

	// Set future.
	uDstNextTransitRate		= uTxnTransitRate;
	uDstNextTransitStart	= uTxnTransitStart;
	uDstNextTransitExpire	= uTxnTransitExpire;	// 0 for never expire.

	if (txn.getITFieldPresent(sfCurrency))
#endif
	std::cerr << "doTransitSet<" << std::endl;
	return tenINVALID;
}

TransactionEngineResult TransactionEngine::doWalletAdd(const SerializedTransaction& txn,
	 std::vector<AffectedAccount>& accounts)
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

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleDst			= mLedger->getAccountRoot(qry, uDstAccountID);

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tenCREATED;
	}

	SLE::pointer		sleSrc			= accounts[0].second;
	STAmount			saAmount		= txn.getITFieldAmount(sfAmount);
	STAmount			saSrcBalance	= sleSrc->getIValueFieldAmount(sfBalance);

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
	sleSrc->setIFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst = boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);

	sleDst->setIndex(Ledger::getAccountRootIndex(uDstAccountID));
	sleDst->setIFieldAccount(sfAccount, uDstAccountID);
	sleDst->setIFieldU32(sfSequence, 1);
	sleDst->setIFieldAmount(sfBalance, saAmount);
	sleDst->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	accounts.push_back(std::make_pair(taaCREATE, sleDst));

	std::cerr << "WalletAdd<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doInvoice(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

// XXX Needs to take offers.
// XXX Use bPassive when taking.
// XXX Also use quality when rippling a take.
TransactionEngineResult TransactionEngine::doOffer(
	const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts,
	const uint160& uSrcAccountID)
{
	uint32					txFlags			= txn.getFlags();
	bool					bPassive		= !!(txFlags & tfPassive);
	STAmount				saAmountIn		= txn.getITFieldAmount(sfAmountIn);
	STAmount				saAmountOut		= txn.getITFieldAmount(sfAmountOut);
	uint160					uIssuerIn		= txn.getITFieldAccount(sfIssuerIn);
	uint160					uIssuerOut		= txn.getITFieldAccount(sfIssuerOut);
	uint32					uExpiration		= txn.getITFieldU32(sfExpiration);
	bool					bHaveExpiration	= txn.getITFieldPresent(sfExpiration);
	uint32					uSequence		= txn.getSequence();

	// LedgerStateParms		qry				= lepNONE;
	SLE::pointer			sleOffer		= boost::make_shared<SerializedLedgerEntry>(ltOFFER);

	uint256					uLedgerIndex	= Ledger::getOfferIndex(uSrcAccountID, uSequence);
	Log(lsINFO) << "doOffer: Creating offer node: " << uLedgerIndex.ToString();

	uint160					uCurrencyIn		= saAmountIn.getCurrency();
	uint160					uCurrencyOut	= saAmountOut.getCurrency();

	TransactionEngineResult	terResult;
	uint64					uOwnerNode;		// Delete hint.
	uint64					uOfferNode;		// Delete hint.
	// uint64					uBookNode;		// Delete hint.

	uint32					uPrevLedgerTime	= 0;	// XXX Need previous

	if (!bHaveExpiration || !uExpiration)
	{
		Log(lsWARNING) << "doOffer: Malformed offer: bad expiration";

		terResult	= tenBAD_EXPIRATION;
	}
	else if (!bHaveExpiration || uPrevLedgerTime >= uExpiration)
	{
		Log(lsWARNING) << "doOffer: Expired transaction: offer expired";

		terResult	= tenEXPIRED;
	}
	else if (saAmountIn.isNative() && saAmountOut.isNative())
	{
		Log(lsWARNING) << "doOffer: Malformed offer: stamps for stamps";

		terResult	= tenBAD_OFFER;
	}
	else if (saAmountIn.isZero() || saAmountOut.isZero())
	{
		Log(lsWARNING) << "doOffer: Malformed offer: bad amount";

		terResult	= tenBAD_OFFER;
	}
	else if (uCurrencyIn == uCurrencyOut && uIssuerIn == uIssuerOut)
	{
		Log(lsWARNING) << "doOffer: Malformed offer: no conversion";

		terResult	= tenREDUNDANT;
	}
	else if (uCurrencyIn.isZero() == uIssuerIn.isZero() && uCurrencyOut.isZero() == uIssuerOut.isZero())
	{
		Log(lsWARNING) << "doOffer: Malformed offer: bad issuer";

		terResult	= tenBAD_ISSUER;
	}

	// XXX check currencies and accounts
	// XXX check funded
	// XXX check output credit line exists
	// XXX when deleting a credit line, delete outstanding offers

	// XXX Only place the offer if a portion is not filled.

	if (terSUCCESS == terResult)
		terResult	= dirAdd(accounts, uOwnerNode, Ledger::getOfferDirIndex(uSrcAccountID), uLedgerIndex);

	if (terSUCCESS == terResult)
	{
		terResult	= dirAdd(accounts, uOfferNode,
			Ledger::getQualityIndex(
				Ledger::getBookBase(uCurrencyIn, uIssuerIn, uCurrencyOut, uIssuerOut),
				STAmount::getRate(saAmountOut, saAmountIn)),
			uLedgerIndex);
	}

	if (terSUCCESS == terResult)
	{
		sleOffer->setIndex(uLedgerIndex);

		sleOffer->setIFieldAccount(sfAccount, uSrcAccountID);
		sleOffer->setIFieldU32(sfSequence, uSequence);
		sleOffer->setIFieldAmount(sfAmountIn, saAmountIn);
		sleOffer->setIFieldAmount(sfAmountOut, saAmountOut);
		sleOffer->setIFieldU64(sfOwnerNode, uOwnerNode);
		sleOffer->setIFieldU64(sfOfferNode, uOfferNode);
		sleOffer->setIFieldU32(sfExpiration, uExpiration);

		if (bPassive)
			sleOffer->setFlag(lsfPassive);

		accounts.push_back(std::make_pair(taaCREATE, sleOffer));
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCancel(
	const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts,
	const uint160& uSrcAccountID)
{
	uint32					uSequence		= txn.getITFieldU32(sfSequence);
	uint256					uLedgerIndex	= Ledger::getOfferIndex(uSrcAccountID, uSequence);

	LedgerStateParms		qry				= lepNONE;
	SLE::pointer			sleOffer		= mLedger->getOffer(qry, uLedgerIndex);
	TransactionEngineResult	terResult;

	if (sleOffer)
	{

		terResult	= tenUNKNOWN;
#if 0
		uint64	uOwnerNode		= sleOffer->getIFieldU64(sfOwnerNode);
		uint64	uOwnerNode		= sleOffer->getIFieldU64(sfOfferNode);

		terResult	= dirDelete(accounts, uOwnerNode, ___, uLedgerIndex);

		if (terSUCCESS == terResult)
		{
			terResult	= dirDelete(accounts, uOfferNode, ___, uLedgerIndex);
		}
#endif
		accounts.push_back(std::make_pair(taaDELETE, sleOffer));
	}
	else
	{
		terResult	= terOFFER_NOT_FOUND;
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doTake(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doStore(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doDelete(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

// vim:ts=4
