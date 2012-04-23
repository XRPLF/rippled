
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

	uint64 txnFee = txn.getTransactionFee();
	if ( (params & tepNO_CHECK_FEE) != tepNONE)
	{
		// WRITEME: Check if fee is adequate
		if (txnFee == 0) return terINSUF_FEE_P;
	}

	// get source account ID
	uint160 srcAccount = txn.getSigningAccount();
	if (!srcAccount) return terINVALID;

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	// find source account
	LedgerStateParms qry = lepNONE;
	SerializedLedgerEntry::pointer src = mLedger->getAccountRoot(qry, srcAccount);
	if (!src) return terNO_ACCOUNT;

	// deduct the fee, so it's not available during the transaction
	// we only write the account back if the transaction succeeds
	uint64 balance = src->getIFieldU64(sfBalance);
	if (balance < txnFee)
		return terINSUF_FEE_B;
	src->setIFieldU64(sfBalance, balance - txnFee);

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
	else src->setIFieldU32(sfSequence, t_seq);

	std::vector<AffectedAccount> accounts;
	accounts.push_back(std::make_pair(taaMODIFY, src));
	TransactionEngineResult result = terUNKNOWN;
	switch(txn.getTxnType())
	{
		case ttINVALID:
			result = terINVALID;
			break;

		case ttMAKE_PAYMENT:
			result = doPayment(txn, accounts);
			break;

		case ttINVOICE:
			result = doInvoice(txn, accounts);
			break;

		case ttEXCHANGE_OFFER:
			result = doOffer(txn, accounts);
			break;

		default:
			result = terUNKNOWN;
			break;
	}

	if (result == terSUCCESS)
	{ // Write back the account states and add the transaction to the ledger
		// WRITEME: Special case code for changing transaction key
		for(std::vector<AffectedAccount>::iterator it=accounts.begin(), end=accounts.end();
			it != end; ++it)
		{
			if ( (it->first==taaMODIFY) || (it->first==taaCREATE) )
			{
				if(mLedger->writeBack(lepNONE, it->second) & lepERROR)
					assert(false);
			}
			else if (it->first == taaDELETE)
			{
				if(!mLedger->peekAccountStateMap()->delItem(it->second->getIndex()))
					assert(false);
			}
		}

		Serializer s;
		txn.add(s);
		mLedger->addTransaction(txID, s);
	}

	return result;
}

TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	uint32 txFlags = txn.getFlags();
	uint160 destAccount = txn.getITFieldAccount(sfDestination);

	// Does the destination account exist?
	if (!destAccount) return terINVALID;
	LedgerStateParms qry = lepNONE;
	SerializedLedgerEntry::pointer dest = mLedger->getAccountRoot(qry, destAccount);
	if (!dest)
	{ // can this transaction create an account
		if ((txFlags & 0x00010000) == 0) // no
			return terNO_TARGET;

		dest = boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);
		dest->setIndex(Ledger::getAccountRootIndex(destAccount));
		dest->setIFieldAccount(sfAccount, destAccount);
		dest->setIFieldU32(sfSequence, 1);
		accounts.push_back(std::make_pair(taaCREATE, dest));
	}
	else accounts.push_back(std::make_pair(taaMODIFY, dest));

	uint64 amount = txn.getITFieldU64(sfAmount);

	uint160 currency;
	if(txn.getITFieldPresent(sfCurrency))
		currency = txn.getITFieldH160(sfCurrency);
	bool native = !!currency;

	if (native)
	{
		uint64 balance = accounts[0].second->getIFieldU64(sfBalance);
		if (balance < amount) return terUNFUNDED;
		accounts[0].second->setIFieldU64(sfBalance, balance - amount);
		accounts[1].second->setIFieldU64(sfBalance, accounts[1].second->getIFieldU64(sfBalance) + amount);
	}
	else
	{
		// WRITEME: Handle non-native currencies, paths
		return terUNKNOWN;
	}

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doInvoice(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}

TransactionEngineResult TransactionEngine::doOffer(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}

TransactionEngineResult TransactionEngine::doTake(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}

TransactionEngineResult TransactionEngine::doCancel(const SerializedTransaction& txn,
	 std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}

TransactionEngineResult TransactionEngine::doStore(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}

TransactionEngineResult TransactionEngine::doDelete(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return terUNKNOWN;
}
