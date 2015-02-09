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

#include <BeastConfig.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/json/to_string.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class STTx_test : public beast::unit_test::suite
{
public:
    void run()
    {
        RippleAddress seed;
        seed.setSeedRandom ();
        RippleAddress generator = RippleAddress::createGeneratorPublic (seed);
        RippleAddress publicAcct = RippleAddress::createAccountPublic (generator, 1);
        RippleAddress privateAcct = RippleAddress::createAccountPrivate (generator, seed, 1);

        STTx j (ttACCOUNT_SET);
        j.setSourceAccount (publicAcct);
        j.setSigningPubKey (publicAcct);
        j.setFieldVL (sfMessageKey, publicAcct.getAccountPublic ());
        j.sign (privateAcct);

        unexpected (!j.checkSign (), "Transaction fails signature test");

        Serializer rawTxn;
        j.add (rawTxn);
        SerialIter sit (rawTxn);
        STTx copy (sit);

        if (copy != j)
        {
            log << j.getJson (0);
            log << copy.getJson (0);
            fail ("Transaction fails serialize/deserialize test");
        }
        else
        {
            pass ();
        }

        STParsedJSONObject parsed ("test", j.getJson (0));
        if (!parsed.object)
            fail ("Unable to build object from json");

        if (STObject (j) != parsed.object)
        {
            log << "ORIG: " << j.getJson (0);
            log << "BUILT " << parsed.object->getJson (0);
            fail ("Built a different transaction");
        }
        else
        {
            pass ();
        }
    }
};

class InnerObjectFormatsSerializer_test : public beast::unit_test::suite
{
public:
    void run()
    {
        // Create a transaction
        RippleAddress txnSeed;
        txnSeed.setSeedRandom ();
        RippleAddress txnGenerator = txnSeed.createGeneratorPublic (txnSeed);
        RippleAddress txnPublicAcct = txnSeed.createAccountPublic (txnGenerator, 1);

        STTx txn (ttACCOUNT_SET);
        txn.setSourceAccount (txnPublicAcct);
        txn.setSigningPubKey (txnPublicAcct);
        txn.setFieldVL (sfMessageKey, txnPublicAcct.getAccountPublic ());
        Blob const emptyBlob;  // Make empty signature for multi-signing
        txn.setFieldVL (sfSigningPubKey, emptyBlob);

        // Create fields for a SigningAccount
        RippleAddress saSeed;
        saSeed.setSeedGeneric ("masterpassphrase");
        RippleAddress const saGenerator = saSeed.createGeneratorPublic (saSeed);
        RippleAddress const saPublicAcct =
            saSeed.createAccountPublic (saGenerator, 1);
        Account const saID = saPublicAcct.getAccountID ();

        // Create a field for SigningFor
        Account const signingForID = txnPublicAcct.getAccountID ();

        RippleAddress saPrivateAcct =
            saSeed.createAccountPrivate(saGenerator, saSeed, 0);

        // Get the stream of the transaction for use in multi-signing.
        Serializer s = txn.getMultiSigningData (saPublicAcct, saPublicAcct);

        Blob saMultiSignature =
            saPrivateAcct.accountPrivateSign (s.getData());

        // The InnerObjectFormats say a SigningAccount is supposed to look
        // like this:
        // SigningAccount {
        //     Account: "...",
        //     MultiSignature: "...",
        //     PublicKey: "...""
        // }
        // Make one well formed SigningAccount and several mal-formed ones.
        // See whether the serializer lets the good one through and catches
        // the bad ones.

        // This lambda contains the bulk of the test code.
        auto testMalformedSigningAccount =
            [this, &txn, &signingForID]
                (STObject const& signingAcct, bool expectPass)
        {
            // Create SigningAccounts array.
            STArray signingAccts (sfSigningAccounts, 1);
            signingAccts.push_back (signingAcct);

            // Insert SigningAccounts array into SigningFor object.
            STObject signingFor (sfSigningFor);
            signingFor.setFieldAccount (sfAccount, signingForID);
            signingFor.setFieldArray (sfSigningAccounts, signingAccts);

            // Insert SigningFor into MultiSigners.
            STArray multiSigners (sfMultiSigners, 1);
            multiSigners.push_back (signingFor);

            // Insert MultiSigners into transaction.
            STTx tempTxn (txn);
            tempTxn.setFieldArray (sfMultiSigners, multiSigners);

            Serializer rawTxn;
            tempTxn.add (rawTxn);
            SerialIter sit (rawTxn);
            bool serialized = false;
            try
            {
                STTx copy (sit);
                serialized = true;
            }
            catch (...)
            {
                ; // If it threw then serialization failed.
            }
            expect (serialized == expectPass,
                "Unexpected serialized = " + std::to_string (serialized) +
                      ".  Object:\n" + signingAcct.getFullText () + "\n");
        };

        {
            // Test case 1.  Make a valid SigningAccount object.
            STObject soTest1 (sfSigningAccount);
            soTest1.setFieldAccount (sfAccount, saID);
            soTest1.setFieldVL (sfSigningPubKey,
                txnPublicAcct.getAccountPublic ());
            soTest1.setFieldVL (sfMultiSignature, saMultiSignature);
            testMalformedSigningAccount (soTest1, true);
        }
        {
            // Test case 2.  Omit sfSigningPubKey from SigningAccount.
            STObject soTest2 (sfSigningAccount);
            soTest2.setFieldAccount (sfAccount, saID);
            soTest2.setFieldVL (sfMultiSignature, saMultiSignature);
            testMalformedSigningAccount (soTest2, false);
        }
        {
            // Test case 3.  Extra sfAmount in SigningAccount.
            STObject soTest3 (sfSigningAccount);
            soTest3.setFieldAccount (sfAccount, saID);
            soTest3.setFieldVL (sfSigningPubKey,
                txnPublicAcct.getAccountPublic ());
            soTest3.setFieldVL (sfMultiSignature, saMultiSignature);
            soTest3.setFieldAmount (sfAmount, STAmount (10000));
            testMalformedSigningAccount (soTest3, false);
        }
        {
            // Test case 4.  Right number of fields, but wrong ones.
            STObject soTest4 (sfSigningAccount);
            soTest4.setFieldVL (sfSigningPubKey,
                txnPublicAcct.getAccountPublic ());
            soTest4.setFieldVL (sfMultiSignature, saMultiSignature);
            soTest4.setFieldAmount (sfAmount, STAmount (10000));
            testMalformedSigningAccount (soTest4, false);
        }
    }
};

BEAST_DEFINE_TESTSUITE(STTx,ripple_app,ripple);
BEAST_DEFINE_TESTSUITE(InnerObjectFormatsSerializer,ripple_app,ripple);

} // ripple
