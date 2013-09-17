//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

TxFormats::TxFormats ()
{
    add ("AccountSet", ttACCOUNT_SET)
            << SOElement (sfEmailHash,       SOE_OPTIONAL)
            << SOElement (sfWalletLocator,   SOE_OPTIONAL)
            << SOElement (sfWalletSize,      SOE_OPTIONAL)
            << SOElement (sfMessageKey,      SOE_OPTIONAL)
            << SOElement (sfDomain,          SOE_OPTIONAL)
            << SOElement (sfTransferRate,    SOE_OPTIONAL)
            << SOElement (sfSetFlag,         SOE_OPTIONAL)
            << SOElement (sfClearFlag,       SOE_OPTIONAL)
            ;

    add ("TrustSet", ttTRUST_SET)
            << SOElement (sfLimitAmount,     SOE_OPTIONAL)
            << SOElement (sfQualityIn,       SOE_OPTIONAL)
            << SOElement (sfQualityOut,      SOE_OPTIONAL)
            ;

    add ("OfferCreate", ttOFFER_CREATE)
            << SOElement (sfTakerPays,       SOE_REQUIRED)
            << SOElement (sfTakerGets,       SOE_REQUIRED)
            << SOElement (sfExpiration,      SOE_OPTIONAL)
            << SOElement (sfOfferSequence,   SOE_OPTIONAL)
            ;

    add ("OfferCancel", ttOFFER_CANCEL)
            << SOElement (sfOfferSequence,   SOE_REQUIRED)
            ;

    add ("SetRegularKey", ttREGULAR_KEY_SET)
            << SOElement (sfRegularKey,  SOE_OPTIONAL)
            ;

    add ("Payment", ttPAYMENT)
            << SOElement (sfDestination,     SOE_REQUIRED)
            << SOElement (sfAmount,          SOE_REQUIRED)
            << SOElement (sfSendMax,         SOE_OPTIONAL)
            << SOElement (sfPaths,           SOE_DEFAULT)
            << SOElement (sfInvoiceID,       SOE_OPTIONAL)
            << SOElement (sfDestinationTag,  SOE_OPTIONAL)
            ;

    add ("Contract", ttCONTRACT)
            << SOElement (sfExpiration,      SOE_REQUIRED)
            << SOElement (sfBondAmount,      SOE_REQUIRED)
            << SOElement (sfStampEscrow,     SOE_REQUIRED)
            << SOElement (sfRippleEscrow,    SOE_REQUIRED)
            << SOElement (sfCreateCode,      SOE_OPTIONAL)
            << SOElement (sfFundCode,        SOE_OPTIONAL)
            << SOElement (sfRemoveCode,      SOE_OPTIONAL)
            << SOElement (sfExpireCode,      SOE_OPTIONAL)
            ;

    add ("RemoveContract", ttCONTRACT_REMOVE)
            << SOElement (sfTarget,          SOE_REQUIRED)
            ;

    add ("EnableFeature", ttFEATURE)
            << SOElement (sfFeature,         SOE_REQUIRED)
            ;

    add ("SetFee", ttFEE)
            << SOElement (sfBaseFee,             SOE_REQUIRED)
            << SOElement (sfReferenceFeeUnits,   SOE_REQUIRED)
            << SOElement (sfReserveBase,         SOE_REQUIRED)
            << SOElement (sfReserveIncrement,    SOE_REQUIRED)
            ;
}

void TxFormats::addCommonFields (Item& item)
{
    item
        << SOElement(sfTransactionType,     SOE_REQUIRED)
        << SOElement(sfFlags,               SOE_OPTIONAL)
        << SOElement(sfSourceTag,           SOE_OPTIONAL)
        << SOElement(sfAccount,             SOE_REQUIRED) 
        << SOElement(sfSequence,            SOE_REQUIRED) 
        << SOElement(sfPreviousTxnID,       SOE_OPTIONAL) 
        << SOElement(sfFee,                 SOE_REQUIRED) 
        << SOElement(sfOperationLimit,      SOE_OPTIONAL) 
        << SOElement(sfSigningPubKey,       SOE_REQUIRED) 
        << SOElement(sfTxnSignature,        SOE_OPTIONAL)
        ;
}

TxFormats* TxFormats::getInstance ()
{
    return SharedSingleton <TxFormats>::getInstance ();
}
