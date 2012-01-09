
#include "LocalTransaction.h"
#include "Application.h"
#include "Wallet.h"

bool LocalTransaction::makeTransaction()
{
	if(!!mTransaction) return true;

	LocalAccount::pointer lac(theApp->getWallet().findAccountForTransaction(mAmount));
	if(!lac) return false;

	mTransaction=Transaction::pointer(new Transaction(lac, mDestAcctID, mAmount, mTag,
		theApp->getOPs().getCurrentLedgerID()));
	if(mTransaction->getStatus()!=NEW) return false;
	return true;
}
