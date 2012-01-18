
#include "json/writer.h"

#include "LocalTransaction.h"
#include "Application.h"
#include "Wallet.h"

bool LocalTransaction::makeTransaction()
{
	if(!!mTransaction) return true;

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
}
