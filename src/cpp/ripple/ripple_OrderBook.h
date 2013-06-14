#ifndef RIPPLE_ORDERBOOK_H
#define RIPPLE_ORDERBOOK_H

/** Describes a serialized ledger entry for an order book.
*/
class OrderBook
{
public:
    typedef boost::shared_ptr <OrderBook> pointer;

    typedef boost::shared_ptr <OrderBook> const& ref;

public:
    /** Construct from a currency specification.

        @param index ???
        @param currencyIn  The base currency.
        @param currencyOut The destination currency.
        @param issuerIn    The base issuer.
        @param issuerOut   The destination issuer.
    */
    // VFALCO NOTE what is the meaning of the index parameter?
    // VFALCO TODO group the issuer and currency parameters together.
    // VFALCO TODO give typedef names to uint256 / uint160 params
    OrderBook (uint256 const& index,
               uint160 const& currencyIn,
               uint160 const& currencyOut,
               uint160 const& issuerIn,
               uint160 const& issuerOut);

    uint256 const& getBookBase () const;

    uint160 const& getCurrencyIn () const;

    uint160 const& getCurrencyOut () const;

    uint160 const& getIssuerIn () const;

    uint160 const& getIssuerOut () const;


private:
    uint256 const mBookBase;
    uint160 const mCurrencyIn;
    uint160 const mCurrencyOut;
    uint160 const mIssuerIn;
    uint160 const mIssuerOut;
};

#endif
