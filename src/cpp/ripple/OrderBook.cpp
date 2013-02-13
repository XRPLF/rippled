#include "OrderBook.h"
#include "Ledger.h"

OrderBook::pointer OrderBook::newOrderBook(SerializedLedgerEntry::ref ledgerEntry)
{
	if(ledgerEntry->getType() != ltOFFER) return( OrderBook::pointer());

	return( OrderBook::pointer(new OrderBook(ledgerEntry)));
}

OrderBook::OrderBook(SerializedLedgerEntry::ref ledgerEntry)
{
	const STAmount	saTakerGets	= ledgerEntry->getFieldAmount(sfTakerGets);
	const STAmount	saTakerPays	= ledgerEntry->getFieldAmount(sfTakerPays);

	mCurrencyIn		= saTakerPays.getCurrency();
	mCurrencyOut	= saTakerGets.getCurrency();
	mIssuerIn		= saTakerPays.getIssuer();
	mIssuerOut		= saTakerGets.getIssuer();

	mBookBase=Ledger::getBookBase(mCurrencyIn, mIssuerIn, mCurrencyOut, mIssuerOut);
}


// vim:ts=4
