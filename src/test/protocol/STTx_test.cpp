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
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/types.h>
#include <ripple/json/to_string.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class STTx_test : public beast::unit_test::suite
{
public:
    void run()
    {
        testcase ("secp256k1 signatures");
        testSTTx (KeyType::secp256k1);

        testcase ("ed25519 signatures");
        testSTTx (KeyType::ed25519);
    }

    void testSTTx(KeyType keyType)
    {
        auto const keypair = randomKeyPair (keyType);

        STTx j (ttACCOUNT_SET,
            [&keypair](auto& obj)
            {
                obj.setAccountID (sfAccount, calcAccountID(keypair.first));
                obj.setFieldVL (sfMessageKey, keypair.first.slice());
                obj.setFieldVL (sfSigningPubKey, keypair.first.slice());
            });
        j.sign (keypair.first, keypair.second);

        unexpected (!j.checkSign (true).first, "Transaction fails signature test");

        Serializer rawTxn;
        j.add (rawTxn);
        SerialIter sit (rawTxn.slice());
        STTx copy (sit);

        if (copy != j)
        {
            log <<
                "j=" << j.getJson (0) << '\n' <<
                "copy=" << copy.getJson (0) << std::endl;
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
            log <<
                "ORIG: " << j.getJson (0) << '\n' <<
                "BUILT " << parsed.object->getJson (0) << std::endl;
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
        auto const kp1 = randomKeyPair (KeyType::secp256k1);
        auto const id1 = calcAccountID(kp1.first);

        STTx txn (ttACCOUNT_SET,
            [&id1,&kp1](auto& obj)
            {
                obj.setAccountID (sfAccount, id1);
                obj.setFieldVL (sfMessageKey, kp1.first.slice());
                // Make empty signature for multi-signing
                obj.setFieldVL (sfSigningPubKey, Slice{});
            });

        // Create fields for a SigningAccount
        auto const kp2 = randomKeyPair (KeyType::secp256k1);
        auto const id2 = calcAccountID(kp2.first);

        // Get the stream of the transaction for use in multi-signing.
        Serializer s = buildMultiSigningData (txn, id2);

        auto const saMultiSignature = sign (kp2.first, kp2.second, s.slice());

        // The InnerObjectFormats say a Signer is supposed to look
        // like this:
        // Signer {
        //     Account: "...",
        //     TxnSignature: "...",
        //     PublicKey: "...""
        // }
        // Make one well formed Signer and several mal-formed ones.  See
        // whether the serializer lets the good one through and catches
        // the bad ones.

        // This lambda contains the bulk of the test code.
        auto testMalformedSigningAccount =
            [this, &txn]
                (STObject const& signer, bool expectPass)
        {
            // Create SigningAccounts array.
            STArray signers (sfSigners, 1);
            signers.push_back (signer);

            // Insert signers into transaction.
            STTx tempTxn (txn);
            tempTxn.setFieldArray (sfSigners, signers);

            Serializer rawTxn;
            tempTxn.add (rawTxn);
            SerialIter sit (rawTxn.slice());
            bool serialized = false;
            try
            {
                STTx copy (sit);
                serialized = true;
            }
            catch (std::exception const&)
            {
                ; // If it threw then serialization failed.
            }
            BEAST_EXPECT(serialized == expectPass);
        };

        {
            // Test case 1.  Make a valid Signer object.
            STObject soTest1 (sfSigner);
            soTest1.setAccountID (sfAccount, id2);
            soTest1.setFieldVL (sfSigningPubKey, kp1.first.slice());
            soTest1.setFieldVL (sfTxnSignature, saMultiSignature);
            testMalformedSigningAccount (soTest1, true);
        }
        {
            // Test case 2.  Omit sfSigningPubKey from SigningAccount.
            STObject soTest2 (sfSigner);
            soTest2.setAccountID (sfAccount, id2);
            soTest2.setFieldVL (sfTxnSignature, saMultiSignature);
            testMalformedSigningAccount (soTest2, false);
        }
        {
            // Test case 3.  Extra sfAmount in SigningAccount.
            STObject soTest3 (sfSigner);
            soTest3.setAccountID (sfAccount, id2);
            soTest3.setFieldVL (sfSigningPubKey, kp1.first.slice());
            soTest3.setFieldVL (sfTxnSignature, saMultiSignature);
            soTest3.setFieldAmount (sfAmount, STAmount (10000));
            testMalformedSigningAccount (soTest3, false);
        }
        {
            // Test case 4.  Right number of fields, but wrong ones.
            STObject soTest4 (sfSigner);
            soTest4.setFieldVL (sfSigningPubKey, kp1.first.slice());
            soTest4.setFieldVL (sfTxnSignature, saMultiSignature);
            soTest4.setFieldAmount (sfAmount, STAmount (10000));
            testMalformedSigningAccount (soTest4, false);
        }
    }
};

BEAST_DEFINE_TESTSUITE(STTx,ripple_app,ripple);
BEAST_DEFINE_TESTSUITE(InnerObjectFormatsSerializer,ripple_app,ripple);

} // ripple
