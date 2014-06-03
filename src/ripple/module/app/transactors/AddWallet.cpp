//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

TER AddWallet::doApply ()
{
    std::uint32_t const uTxFlags = mTxn.getFlags ();

    if (uTxFlags & tfUniversalMask)
    {
        m_journal.trace <<
            "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    Blob const vucPubKey    = mTxn.getFieldVL (sfPublicKey);
    Blob const vucSignature = mTxn.getFieldVL (sfSignature);

    uint160 const uAuthKeyID (mTxn.getFieldAccount160 (sfRegularKey));
    RippleAddress const naMasterPubKey (
        RippleAddress::createAccountPublic (vucPubKey));
    uint160 const uDstAccountID (naMasterPubKey.getAccountID ());

    // FIXME: This should be moved to the transaction's signature check logic and cached
    if (!naMasterPubKey.accountPublicVerify (
        Serializer::getSHA512Half (uAuthKeyID.begin (), uAuthKeyID.size ()), 
        vucSignature, ECDSA::not_strict))
    {
        m_journal.trace <<
            "Unauthorized: bad signature ";
        return tefBAD_ADD_AUTH;
    }

    SLE::pointer sleDst (mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID)));

    if (sleDst)
    {
        m_journal.trace <<
            "account already created";
        return tefCREATED;
    }

    // Direct XRP payment.

    STAmount saDstAmount = mTxn.getFieldAmount (sfAmount);
    STAmount saPaid = mTxn.getTransactionFee ();
    STAmount const saSrcBalance = mTxnAccount->getFieldAmount (sfBalance);
    std::uint32_t const uOwnerCount = mTxnAccount->getFieldU32 (sfOwnerCount);
    std::uint64_t const uReserve = mEngine->getLedger ()->getReserve (uOwnerCount);
    

    // Make sure have enough reserve to send. Allow final spend to use reserve
    // for fee.
    // Note: Reserve is not scaled by fee.
    if (saSrcBalance + saPaid < saDstAmount + uReserve)
    {
        // Vote no. However, transaction might succeed, if applied in a
        // different order.
        m_journal.trace <<
            "Delay transaction: Insufficient funds: %s / %s (%d)" <<
            saSrcBalance.getText () << " / " <<
            (saDstAmount + uReserve).getText () << " with reserve = " <<
            uReserve;
        return tecUNFUNDED_ADD;
    }

    // Deduct initial balance from source account.
    mTxnAccount->setFieldAmount (sfBalance, saSrcBalance - saDstAmount);

    // Create the account.
    sleDst  = mEngine->entryCreate (ltACCOUNT_ROOT,
        Ledger::getAccountRootIndex (uDstAccountID));

    sleDst->setFieldAccount (sfAccount, uDstAccountID);
    sleDst->setFieldU32 (sfSequence, 1);
    sleDst->setFieldAmount (sfBalance, saDstAmount);
    sleDst->setFieldAccount (sfRegularKey, uAuthKeyID);

    return tesSUCCESS;
}

}
