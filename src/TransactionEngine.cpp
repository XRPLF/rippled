
#include "TransactionEngine.h"
#include "TransactionFormats.h"

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params)
{
	uint256 txID = txn.getTransactionID();
	if(!txID) return terINVALID;

	// extract signing key
	CKey acctKey;
	if (!acctKey.SetPubKey(txn.getRawSigningAccount())) return terINVALID;

	// check signature
	if (!txn.checkSign(acctKey)) return terINVALID;

	// WRITEME: Check fee

	// get source account ID
	uint160 srcAccount = txn.getSigningAccount();
	if (!srcAccount) return terINVALID;

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	// find source account
	LedgerStateParms qry = lepNONE;
	SerializedLedgerEntry::pointer src = mLedger->getAccountRoot(qry, srcAccount);
	if(!src) return terNO_ACCOUNT;

	// validate sequence
	uint32 t_seq = txn.getSequence();
	uint32 a_seq = src->getIFieldU32(sfSequence);
	if (t_seq != a_seq)
	{
		// WRITEME: Special case code for changing transaction key
		if (a_seq < t_seq) return terPRE_SEQ;
		if (mLedger->hasTransaction(txID))
			return terALREADY;
		return terPAST_SEQ;
	}

	TransactionEngineResult result = terUNKNOWN;
	switch(txn.getTxnType())
	{
		case ttINVALID:
			result = terINVALID;
			break;

		case ttMAKE_PAYMENT:
			result = doPayment(txn, *src);
			break;

		case ttINVOICE:
			result = doInvoice(txn, *src);
			break;

		case ttEXCHANGE_OFFER:
			result = doOffer(txn, *src);
			break;

		default:
			result = terUNKNOWN;
			break;
	}

	if (result == terSUCCESS)
	{ // Write back the source account state and add the transaction to the ledger
		// WRITEME: Special case code for changing transaction key
		src->setIFieldU32(sfSequence, t_seq);
		if(mLedger->writeBack(lepNONE, src) & lepERROR)
		{
			assert(false);
			return terFAILED;
		}

		Serializer s;
		txn.add(s);
		mLedger->addTransaction(txID, s);
	}

	return result;
}
