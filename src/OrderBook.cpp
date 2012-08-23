#include "OrderBook.h"
#include "Ledger.h"

OrderBook::pointer OrderBook::newOrderBook(SerializedLedgerEntry::pointer ledgerEntry)
{
	if(ledgerEntry->getType() != ltOFFER) return( OrderBook::pointer());
	
	return( OrderBook::pointer(new OrderBook(ledgerEntry)));
}

OrderBook::OrderBook(SerializedLedgerEntry::pointer ledgerEntry)
{
	mCurrencyIn=ledgerEntry->getIValueFieldAmount(sfTakerGets).getCurrency();
	mCurrencyOut=ledgerEntry->getIValueFieldAmount(sfTakerPays).getCurrency();
	mIssuerIn=ledgerEntry->getIValueFieldAccount(sfGetsIssuer).getAccountID();
	mIssuerOut=ledgerEntry->getIValueFieldAccount(sfPaysIssuer).getAccountID();

	mBookBase=Ledger::getBookBase(mCurrencyOut,mIssuerOut,mCurrencyIn,mIssuerIn);
}