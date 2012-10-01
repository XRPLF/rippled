#include "OrderBook.h"
#include "Ledger.h"

OrderBook::pointer OrderBook::newOrderBook(SerializedLedgerEntry::pointer ledgerEntry)
{
	if(ledgerEntry->getType() != ltOFFER) return( OrderBook::pointer());

	return( OrderBook::pointer(new OrderBook(ledgerEntry)));
}

OrderBook::OrderBook(SerializedLedgerEntry::pointer ledgerEntry)
{
	const STAmount	saTakerGets	= ledgerEntry->getValueFieldAmount(sfTakerGets);
	const STAmount	saTakerPays	= ledgerEntry->getValueFieldAmount(sfTakerPays);

	mCurrencyIn		= saTakerGets.getCurrency();
	mCurrencyOut	= saTakerPays.getCurrency();
	mIssuerIn		= saTakerGets.getIssuer();
	mIssuerOut		= saTakerPays.getIssuer();

	mBookBase=Ledger::getBookBase(mCurrencyOut,mIssuerOut,mCurrencyIn,mIssuerIn);
}
// vim:ts=4
