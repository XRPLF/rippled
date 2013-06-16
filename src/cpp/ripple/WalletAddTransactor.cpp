//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (WalletAddTransactor)

TER WalletAddTransactor::doApply ()
{
    std::cerr << "WalletAdd>" << std::endl;

    Blob const vucPubKey    = mTxn.getFieldVL (sfPublicKey);
    Blob const vucSignature = mTxn.getFieldVL (sfSignature);
    const uint160                       uAuthKeyID      = mTxn.getFieldAccount160 (sfRegularKey);
    const RippleAddress                 naMasterPubKey  = RippleAddress::createAccountPublic (vucPubKey);
    const uint160                       uDstAccountID   = naMasterPubKey.getAccountID ();

    const uint32                        uTxFlags        = mTxn.getFlags ();

    if (uTxFlags)
    {
        WriteLog (lsINFO, WalletAddTransactor) << "WalletAdd: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    // FIXME: This should be moved to the transaction's signature check logic and cached
    if (!naMasterPubKey.accountPublicVerify (Serializer::getSHA512Half (uAuthKeyID.begin (), uAuthKeyID.size ()), vucSignature))
    {
        std::cerr << "WalletAdd: unauthorized: bad signature " << std::endl;

        return tefBAD_ADD_AUTH;
    }

    SLE::pointer        sleDst  = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

    if (sleDst)
    {
        std::cerr << "WalletAdd: account already created" << std::endl;

        return tefCREATED;
    }

    // Direct XRP payment.

    STAmount        saDstAmount     = mTxn.getFieldAmount (sfAmount);
    const STAmount  saSrcBalance    = mTxnAccount->getFieldAmount (sfBalance);
    const uint32    uOwnerCount     = mTxnAccount->getFieldU32 (sfOwnerCount);
    const uint64    uReserve        = mEngine->getLedger ()->getReserve (uOwnerCount);
    STAmount        saPaid          = mTxn.getTransactionFee ();

    // Make sure have enough reserve to send. Allow final spend to use reserve for fee.
    if (saSrcBalance + saPaid < saDstAmount + uReserve)     // Reserve is not scaled by fee.
    {
        // Vote no. However, transaction might succeed, if applied in a different order.
        WriteLog (lsINFO, WalletAddTransactor) << boost::str (boost::format ("WalletAdd: Delay transaction: Insufficient funds: %s / %s (%d)")
                                               % saSrcBalance.getText () % (saDstAmount + uReserve).getText () % uReserve);

        return tecUNFUNDED_ADD;
    }

    // Deduct initial balance from source account.
    mTxnAccount->setFieldAmount (sfBalance, saSrcBalance - saDstAmount);

    // Create the account.
    sleDst  = mEngine->entryCreate (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

    sleDst->setFieldAccount (sfAccount, uDstAccountID);
    sleDst->setFieldU32 (sfSequence, 1);
    sleDst->setFieldAmount (sfBalance, saDstAmount);
    sleDst->setFieldAccount (sfRegularKey, uAuthKeyID);

    std::cerr << "WalletAdd<" << std::endl;

    return tesSUCCESS;
}

// vim:ts=4
