
#include "TransactionEngine.h"

#include "TransactionFormats.h"

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params)
{
	TransactionEngineResult result = terSUCCESS;

	uint256 txID = txn.getTransactionID();
	if(!txID) return tenINVALID;

	// Extract signing key
	// Transactions contain a signing key.  This allows us to trivially verify a transaction has at least been properly signed
	// without going to disk.  Each transaction also notes a source account id.  This is used to verify that the signing key is
	// associated with the account.
	CKey acctKey;
	if (!acctKey.SetPubKey(txn.peekSigningPubKey())) return tenINVALID;

	// check signature
	if (!txn.checkSign(acctKey)) return tenINVALID;

	bool	bPrepaid	= false;

	// Customize behavoir based on transaction type.
	switch(txn.getTxnType())
	{
		case ttCLAIM:
			bPrepaid	= true;
			break;

		case ttMAKE_PAYMENT:
		case ttINVOICE:
		case ttEXCHANGE_OFFER:
			result = terSUCCESS;
			break;

		case ttINVALID:
			result = tenINVALID;
			break;

		default:
			result = tenUNKNOWN;
			break;
	}

	if (terSUCCESS != result)
		return result;

	uint64 txnFee = txn.getTransactionFee();
	if ( (params & tepNO_CHECK_FEE) != tepNONE)
	{
		if (bPrepaid)
		{
			if (txnFee)
				// Transaction is malformed.
				return tenINSUF_FEE_P;
		}
		else
		{
			// WRITEME: Check if fee is adequate
			if (txnFee == 0)
				return tenINSUF_FEE_P;
		}
	}

	// get source account ID
	uint160 srcAccount = txn.getSourceAccount().getAccountID();
	if (!srcAccount) return tenINVALID;

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	// find source account
	// If we are only verifying some transactions, this would be probablistic.
	LedgerStateParms qry = lepNONE;
	SerializedLedgerEntry::pointer src = mLedger->getAccountRoot(qry, srcAccount);
	if (!src) return terNO_ACCOUNT;

	// deduct the fee, so it's not available during the transaction
	// we only write the account back if the transaction succeeds
	if (txnFee)
	{
		uint64 balance = src->getIFieldU64(sfBalance);

		if (balance < txnFee)
			return terINSUF_FEE_B;

		src->setIFieldU64(sfBalance, balance - txnFee);
	}

	// Validate sequence
	uint32 t_seq = txn.getSequence();

	if (bPrepaid)
	{
		if (t_seq)
			return terPAST_SEQ;
	}
	else
	{
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
	}

	std::vector<AffectedAccount> accounts;
	accounts.push_back(std::make_pair(taaMODIFY, src));

	switch(txn.getTxnType())
	{
		case ttINVALID:
			result = tenINVALID;
			break;

		case ttCLAIM:
			result = doClaim(txn, accounts);
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
			result = tenUNKNOWN;
			break;
	}

	if (result == terSUCCESS)
	{ // Write back the account states and add the transaction to the ledger
		// WRITEME: Special case code for changing transaction key
		for(std::vector<AffectedAccount>::iterator it=accounts.begin(), end=accounts.end();
			it != end; ++it)
		{	if (it->first == taaCREATE)
			{
				if (mLedger->writeBack(lepCREATE, it->second) & lepERROR)
					assert(false);
			}
			else if (it->first==taaMODIFY)
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
		mLedger->addTransaction(txID, s, txnFee);
	}

	return result;
}

TransactionEngineResult TransactionEngine::doClaim(const SerializedTransaction& txn,
	 std::vector<AffectedAccount>& accounts)
{
	NewcoinAddress				naSigningPubKey;

	naSigningPubKey.setAccountPublic(txn.peekSigningPubKey());

	uint160	sourceAccountID	= naSigningPubKey.getAccountID();

	if (sourceAccountID != txn.getSourceAccount().getAccountID())
		// Signing Pub Key must be for Source Account ID.
		return tenINVALID;

	LedgerStateParms				qry				= lepNONE;
	SerializedLedgerEntry::pointer	dest			= mLedger->getAccountRoot(qry, sourceAccountID);

	if (!dest)
		// Source account does not exist.  Could succeed if it was created first.
		return terNO_ACCOUNT;

	if (dest->getIFieldPresent(sfAuthorizedKey))
		// Source account already claimed.
		return tenCLAIMED;

	uint160							hGeneratorID	= txn.getITFieldH160(sfGeneratorID);
									qry				= lepNONE;
	SerializedLedgerEntry::pointer	gen				= mLedger->getGenerator(qry, hGeneratorID);
	if (gen)
		// Generator is already in use.  Regular passphrases limited to one wallet.
		return tenGEN_IN_USE;

	//
	// Claim the account.
	//
	std::vector<unsigned char>		vucCipher		= txn.getITFieldVL(sfGenerator);

	// Set the public key needed to use the account.
	dest->setIFieldH160(sfAuthorizedKey, hGeneratorID);

	accounts.push_back(std::make_pair(taaMODIFY, dest));

	// Construct a generator map entry.
									gen				= boost::make_shared<SerializedLedgerEntry>(ltGENERATOR_MAP);

	gen->setIndex(Ledger::getGeneratorIndex(hGeneratorID));
	gen->setIFieldH160(sfGeneratorID, hGeneratorID);
	gen->setIFieldVL(sfGenerator, vucCipher);

	accounts.push_back(std::make_pair(taaCREATE, gen));

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	uint32 txFlags = txn.getFlags();
	uint160 destAccount = txn.getITFieldAccount(sfDestination);

	// Does the destination account exist?
	if (!destAccount) return tenINVALID;
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
		return tenUNKNOWN;
	}

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doInvoice(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doOffer(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doTake(const SerializedTransaction& txn,
	std::vector<AffectedAccount>& accounts)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doCancel(const SerializedTransaction& txn,
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
