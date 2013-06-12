

OrderBook::OrderBook (uint256 const& index,
                      uint160 const& currencyIn,
                      uint160 const& currencyOut,
                      uint160 const& issuerIn,
                      uint160 const& issuerOut)
    : mBookBase (index)
    , mCurrencyIn (currencyIn)
    , mCurrencyOut (currencyOut)
    , mIssuerIn (issuerIn)
    , mIssuerOut (issuerOut)
{
}

uint256 const& OrderBook::getBookBase () const
{
    return mBookBase;
}

uint160 const& OrderBook::getCurrencyIn () const
{
    return mCurrencyIn;
}

uint160 const& OrderBook::getCurrencyOut () const
{
    return mCurrencyOut;
}

uint160 const& OrderBook::getIssuerIn () const
{
    return mIssuerIn;
}

uint160 const& OrderBook::getIssuerOut () const
{
    return mIssuerOut;
}
