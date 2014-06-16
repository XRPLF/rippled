//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================


namespace ripple {

// profile offers <pass_a> <account_a> <currency_offer_a> <account_b> <currency_offer_b> <count> [submit]
// profile 0:offers 1:pass_a 2:account_a 3:currency_offer_a 4:account_b 5:currency_offer_b 6:<count> 7:[submit]
// issuer is the offering account
// --> submit: 'submit|true|false': defaults to false
// Prior to running allow each to have a credit line of what they will be getting from the other account.
Json::Value doProfile (RPC::Context& context)
{
    /* need to fix now that sharedOfferCreate is gone
    int             iArgs   = context.params_.size();
    RippleAddress   naSeedA;
    RippleAddress   naAccountA;
    uint160         uCurrencyOfferA;
    RippleAddress   naSeedB;
    RippleAddress   naAccountB;
    uint160         uCurrencyOfferB;
    uint32          iCount  = 100;
    bool            bSubmit = false;

    if (iArgs < 6 || "offers" != context.params_[0u].asString())
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (!naSeedA.setSeedGeneric(context.params_[1u].asString()))                          // <pass_a>
        return rpcError(rpcINVALID_PARAMS);

    naAccountA.setAccountID(context.params_[2u].asString());                              // <account_a>

    if (!STAmount::currencyFromString(uCurrencyOfferA, context.params_[3u].asString()))   // <currency_offer_a>
        return rpcError(rpcINVALID_PARAMS);

    naAccountB.setAccountID(context.params_[4u].asString());                              // <account_b>
    if (!STAmount::currencyFromString(uCurrencyOfferB, context.params_[5u].asString()))   // <currency_offer_b>
        return rpcError(rpcINVALID_PARAMS);

    iCount  = lexicalCast <uint32>(context.params_[6u].asString());

    if (iArgs >= 8 && "false" != context.params_[7u].asString())
        bSubmit = true;

    LogSink::get()->setMinSeverity(lsFATAL,true);

    boost::posix_time::ptime            ptStart(boost::posix_time::microsec_clock::local_time());

    for(unsigned int n=0; n<iCount; n++)
    {
        RippleAddress           naMasterGeneratorA;
        RippleAddress           naAccountPublicA;
        RippleAddress           naAccountPrivateA;
        AccountState::pointer   asSrcA;
        STAmount                saSrcBalanceA;

        Json::Value             jvObjA      = authorize(uint256(0), naSeedA, naAccountA, naAccountPublicA, naAccountPrivateA,
            saSrcBalanceA, getConfig ().FEE_DEFAULT, asSrcA, naMasterGeneratorA);

        if (!jvObjA.empty())
            return jvObjA;

        Transaction::pointer    tpOfferA    = Transaction::sharedOfferCreate(
            naAccountPublicA, naAccountPrivateA,
            naAccountA,                                                 // naSourceAccount,
            asSrcA->getSeq(),                                           // uSeq
            getConfig ().FEE_DEFAULT,
            0,                                                          // uSourceTag,
            false,                                                      // bPassive
            STAmount(uCurrencyOfferA, naAccountA.getAccountID(), 1),    // saTakerPays
            STAmount(uCurrencyOfferB, naAccountB.getAccountID(), 1+n),  // saTakerGets
            0);                                                         // uExpiration

        if (bSubmit)
            tpOfferA    = context.netOps_.submitTransactionSync(tpOfferA); // FIXME: Don't use synch interface
    }

    boost::posix_time::ptime            ptEnd(boost::posix_time::microsec_clock::local_time());
    boost::posix_time::time_duration    tdInterval      = ptEnd-ptStart;
    long                                lMicroseconds   = tdInterval.total_microseconds();
    int                                 iTransactions   = iCount;
    float                               fRate           = lMicroseconds ? iTransactions/(lMicroseconds/1000000.0) : 0.0;

    Json::Value obj(Json::objectValue);

    obj["transactions"]     = iTransactions;
    obj["submit"]           = bSubmit;
    obj["start"]            = boost::posix_time::to_simple_string(ptStart);
    obj["end"]              = boost::posix_time::to_simple_string(ptEnd);
    obj["interval"]         = boost::posix_time::to_simple_string(tdInterval);
    obj["rate_per_second"]  = fRate;
    */
    Json::Value obj (Json::objectValue);
    return obj;
}

} // ripple
