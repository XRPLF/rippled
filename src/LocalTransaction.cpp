
#include <boost/lexical_cast.hpp>

#include "../json/writer.h"

#include "LocalTransaction.h"
#include "Application.h"
#include "Wallet.h"

bool LocalTransaction::makeTransaction()
{
	if(!!mTransaction) return true;

	std::cerr << "LocalTransaction is obsolete." << std::endl;
	return false;

#if 0
	LocalAccount::pointer lac(theApp->getWallet().findAccountForTransaction(mAmount));
	if(!lac)
	{
		std::cerr << "Account with sufficient balance not found" << std::endl;
		return false;
	}

	mTransaction=Transaction::pointer(new Transaction(lac, mDestAcctID, mAmount, mTag,
		theApp->getOPs().getCurrentLedgerID()));
	if(mTransaction->getStatus()!=NEW)
	{
#ifdef DEBUG
		std::cerr << "Status not NEW" << std::endl;
		Json::Value t=mTransaction->getJson(true);
        Json::StyledStreamWriter w;
        w.write(std::cerr, t);
#endif
		return false;
	}
	return true;
#endif
}

void LocalTransaction::performTransaction()
{
	mTransaction=theApp->getOPs().processTransaction(mTransaction);
}

Json::Value LocalTransaction::getJson() const
{
	if(!mTransaction)
	{ // has no corresponding transaction
		Json::Value ret(Json::objectValue);
		ret["Status"]="unfunded";
		ret["Amount"]=boost::lexical_cast<std::string>(mAmount);

		Json::Value destination(Json::objectValue);
        destination["AccountID"]=mDestAcctID.humanAccountID();

        ret["Destination"]=destination;

        return ret;
	}

	return mTransaction->getJson(true, isPaid(), isCredited());
}

// vim:ts=4
