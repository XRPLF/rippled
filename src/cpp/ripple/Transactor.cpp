#include "Transactor.h"
#include "Log.h"
#include "Config.h"
#include "PaymentTransactor.h"
#include "RegularKeySetTransactor.h"
#include "AccountSetTransactor.h"
#include "WalletAddTransactor.h"
#include "OfferCancelTransactor.h"
#include "OfferCreateTransactor.h"
#include "TrustSetTransactor.h"

SETUP_LOG();

std::auto_ptr<Transactor> Transactor::makeTransactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine)
{
	switch(txn.getTxnType())
	{
	case ttPAYMENT:
		return std::auto_ptr<Transactor>(new PaymentTransactor(txn, params, engine));
	case ttACCOUNT_SET:
		return std::auto_ptr<Transactor>(new AccountSetTransactor(txn, params, engine));
	case ttREGULAR_KEY_SET:
		return std::auto_ptr<Transactor>(new RegularKeySetTransactor(txn, params, engine));
	case ttTRUST_SET:
		return std::auto_ptr<Transactor>(new TrustSetTransactor(txn, params, engine));
	case ttOFFER_CREATE:
		return std::auto_ptr<Transactor>(new OfferCreateTransactor(txn, params, engine));
	case ttOFFER_CANCEL:
		return std::auto_ptr<Transactor>(new OfferCancelTransactor(txn, params, engine));
	case ttWALLET_ADD:
		return std::auto_ptr<Transactor>(new WalletAddTransactor(txn, params, engine));
	default:
		return std::auto_ptr<Transactor>();
	}
}


Transactor::Transactor(const SerializedTransaction& txn,TransactionEngineParams params, TransactionEngine* engine) : mTxn(txn), mEngine(engine), mParams(params)
{
	mHasAuthKey=false;
}

void Transactor::calculateFee()
{
	mFeeDue	= STAmount(mEngine->getLedger()->scaleFeeLoad(calculateBaseFee()));
}

uint64_t Transactor::calculateBaseFee()
{
	return theConfig.FEE_DEFAULT;
}

TER Transactor::payFee()
{
	STAmount saPaid = mTxn.getTransactionFee();

	// Only check fee is sufficient when the ledger is open.
	if (isSetBit(mParams, tapOPEN_LEDGER) && saPaid < mFeeDue)
	{
		cLog(lsINFO) << "applyTransaction: insufficient fee";

		return telINSUF_FEE_P;
	}

	if (saPaid.isNegative() || !saPaid.isNative())
		return temBAD_AMOUNT;

	if (!saPaid) return tesSUCCESS;

	// Deduct the fee, so it's not available during the transaction.
	// Will only write the account back, if the transaction succeeds.
	if (mSourceBalance < saPaid)
	{
		cLog(lsINFO)
			<< boost::str(boost::format("applyTransaction: Delay: insufficient balance: balance=%s paid=%s")
			% mSourceBalance.getText()
			% saPaid.getText());

		return terINSUF_FEE_B;
	}

	mSourceBalance -= saPaid;
	mTxnAccount->setFieldAmount(sfBalance, mSourceBalance);

	return tesSUCCESS;
}

TER Transactor::checkSig()
{
	// Consistency: Check signature
	// Verify the transaction's signing public key is the key authorized for signing.
	if (mHasAuthKey && mSigningPubKey.getAccountID() == mTxnAccount->getFieldAccount(sfRegularKey).getAccountID())
	{
		// Authorized to continue.
		nothing();
	}
	else if (mSigningPubKey.getAccountID() == mTxnAccountID)
	{
		// Authorized to continue.
		nothing();
	}
	else if (mHasAuthKey)
	{
		cLog(lsINFO) << "applyTransaction: Delay: Not authorized to use account.";

		return tefBAD_AUTH;
	}
	else
	{
		cLog(lsINFO) << "applyTransaction: Invalid: Not authorized to use account.";

		return temBAD_AUTH_MASTER;
	}

	return tesSUCCESS;
}

TER Transactor::checkSeq()
{
	uint32 t_seq = mTxn.getSequence();
	uint32 a_seq = mTxnAccount->getFieldU32(sfSequence);

	cLog(lsTRACE) << "Aseq=" << a_seq << ", Tseq=" << t_seq;

	if (t_seq != a_seq)
	{
		if (a_seq < t_seq)
		{
			cLog(lsINFO) << "applyTransaction: future sequence number";

			return terPRE_SEQ;
		}
		else
		{
			uint256 txID = mTxn.getTransactionID();
			if (mEngine->getLedger()->hasTransaction(txID))
				return tefALREADY;
		}

		cLog(lsWARNING) << "applyTransaction: past sequence number";

		return tefPAST_SEQ;
	}else
	{
		mTxnAccount->setFieldU32(sfSequence, t_seq + 1);
	}

	return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
TER Transactor::preCheck()
{

	mTxnAccountID	= mTxn.getSourceAccount().getAccountID();
	if (!mTxnAccountID)
	{
		cLog(lsWARNING) << "applyTransaction: bad source id";

		return temINVALID;
	}

	// Extract signing key
	// Transactions contain a signing key.  This allows us to trivially verify a transaction has at least been properly signed
	// without going to disk.  Each transaction also notes a source account id.  This is used to verify that the signing key is
	// associated with the account.
	// XXX This could be a lot cleaner to prevent unnecessary copying.
	mSigningPubKey	= RippleAddress::createAccountPublic(mTxn.getSigningPubKey());

	// Consistency: really signed.
	if ( !isSetBit(mParams, tapNO_CHECK_SIGN) && !mTxn.checkSign(mSigningPubKey))
	{
		cLog(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

		return temINVALID;
	}

	return tesSUCCESS;
}

TER Transactor::apply()
{
	TER		terResult	= tesSUCCESS;
	terResult=preCheck();
	if(terResult != tesSUCCESS) return(terResult);

	calculateFee();

	boost::recursive_mutex::scoped_lock sl(mEngine->getLedger()->mLock);

	mTxnAccount	= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(mTxnAccountID));

	// Find source account
	// If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probabilistic.

	if (!mTxnAccount)
	{
		cLog(lsTRACE) << boost::str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			mTxn.getSourceAccount().humanAccountID());

		return terNO_ACCOUNT;
	}
	else
	{
		mSourceBalance	= mTxnAccount->getFieldAmount(sfBalance);
		mHasAuthKey	= mTxnAccount->isFieldPresent(sfRegularKey);
	}

	terResult = payFee();
	if (terResult != tesSUCCESS) return(terResult);

	terResult = checkSig();
	if (terResult != tesSUCCESS) return(terResult);

	terResult = checkSeq();
	if (terResult != tesSUCCESS) return(terResult);

	mEngine->entryModify(mTxnAccount);

	return doApply();
}

// vim:ts=4
