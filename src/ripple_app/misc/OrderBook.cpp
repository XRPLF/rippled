//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================


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
