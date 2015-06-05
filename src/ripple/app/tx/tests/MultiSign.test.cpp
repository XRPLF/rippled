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
#include <ripple/app/tx/tests/common_transactor.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace test {

class MultiSign_test : public beast::unit_test::suite
{
    static std::uint64_t const xrp = std::mega::num;
    static int const stdFee = 10;

    // Unfunded accounts to use for phantom signing.
    UserAccount bogie {KeyType::secp256k1, "bogie"};
    UserAccount ghost {KeyType::ed25519,   "ghost"};
    UserAccount haunt {KeyType::secp256k1, "haunt"};
    UserAccount jinni {KeyType::ed25519,   "jinni"};
    UserAccount shade {KeyType::secp256k1, "shade"};
    UserAccount spook {KeyType::ed25519,   "spook"};

//------------------------------------------------------------------------------

    void test_singleSig (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts.
        std::uint64_t aliceBalance = 1000000000;
        UserAccount alice (kType, "alice");

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        // Pay from alice to master, but alice doesn't sign.  Should fail.
        {
            STTx tx = getPaymentTx (alice, master, 990);
            singleSign (tx, bogie);
            ledger.applyBadTransaction (tx, tefBAD_AUTH_MASTER);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Pay from alice to master using alice's master key.
        payInDrops (ledger, alice, master, 1000 - stdFee);
        aliceBalanceCheck (1000);

        // Give alice a regular key.
        alice.setRegKey (ledger, kType, "alie");
        aliceBalanceCheck (stdFee);

        // Make another payment to master, but still use the master key.
        payInDrops (ledger, alice, master, 1000 - stdFee);
        aliceBalanceCheck (1000);

        // Tell alice to use the regular key and make another payment.
        alice.useRegKey (true);
        payInDrops (ledger, alice, master, 1000 - stdFee);
        aliceBalanceCheck (1000);

        // Disable alice's master key.
        alice.useRegKey (false);
        alice.disableMaster (ledger, true);
        aliceBalanceCheck (stdFee);

        // Have alice make another payment with her master key.  Should fail.
        {
            STTx tx = getPaymentTx (alice, master, 1000 - stdFee);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, tefMASTER_DISABLED);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }

        // alice makes another payment with her regular key.  Should succeed.
        alice.useRegKey (true);
        payInDrops (ledger, alice, master, 1000 - stdFee);
        aliceBalanceCheck (1000);
    }

//------------------------------------------------------------------------------

    void test_noReserve (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        std::uint64_t aliceBalance = 200000000;
        UserAccount alice (kType, "alice");

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        // Create a signerlist that we can attach to alice.
        SignerList aliceSigners {{bogie, 1}, {ghost, 2}, {haunt, 3}};
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 3);
            singleSign (tx, alice);
            ledger.applyTecTransaction (tx, tecINSUFFICIENT_RESERVE);
            aliceBalanceCheck (stdFee);
        }
        // Fund alice better.  SignerListSet should succeed now.
        aliceBalance += 1000000000;
        payInDrops (ledger, master, alice, 1000000000);
        aliceBalanceCheck (0);
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 3);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
    }

//------------------------------------------------------------------------------

    void test_signerListSet (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        // Attach a signer to alice.  Should fail since there's only one signer.
        {
            SignerList aliceSigners {{bogie, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 3);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temMALFORMED);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 0);
        }
        // Try again with two multi-signers. Should work.
        {
            SignerList aliceSigners {{bogie, 3}, {ghost, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 6);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
            expect(getOwnerCount (ledger, alice) == 4);
        }
        // Try to add alice as a multi-signer on her own account.  Should fail.
        {
            SignerList aliceSigners {{alice, 3}, {bogie, 3}, {ghost, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temBAD_SIGNER);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 4);
        }
        // Try to add the same account twice.  Should fail.
        {
            SignerList aliceSigners {
                {bogie, 3}, {ghost, 3}, {haunt, 3}, {shade, 3}, {ghost, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temBAD_SIGNER);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 4);
        }
        // Set a signer list where the quorum can't be met.  Should fail.
        {
            SignerList aliceSigners {{bogie, 3}, {ghost, 3}, {haunt, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 10);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temBAD_QUORUM);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 4);
        }
        // Try setting a signer list where the quorum can barely be met.  Also,
        // set a weight of zero, which is legal.
        {
            SignerList aliceSigners{{bogie, 0}, {ghost, 65535}, {haunt, 65535}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 131070);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
            expect(getOwnerCount (ledger, alice) == 5);
        }
        // Try a zero quorum.  Should fail.
        {
            SignerList aliceSigners{{bogie, 0}, {ghost, 0}, {haunt, 0}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 0);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temMALFORMED);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 5);
        }

        // Try to create a signer list that's barely too big.  Should fail.
        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);

        UserAccount cheri (kType, "cheri");
        payInDrops (ledger, master, cheri, 1000*xrp);

        UserAccount daria (kType, "daria");
        payInDrops (ledger, master, daria, 1000*xrp);
        {
            SignerList aliceSigners{{bogie, 1}, {ghost, 1}, {haunt, 1},
                {jinni, 1}, {shade, 1}, {spook, 1}, {becky, 1}, {cheri, 1},
                {daria, 1}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyBadTransaction (tx, temMALFORMED);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
            expect(getOwnerCount (ledger, alice) == 5);
        }
        // Make the biggest allowed list.  This one should succeed.
        {
            SignerList aliceSigners{{bogie, 1}, {ghost, 1}, {haunt, 1},
                {jinni, 1}, {shade, 1}, {spook, 1}, {becky, 1}, {cheri, 1}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
            expect(getOwnerCount (ledger, alice) == 10);
        }
        // Remove alice's SignerList.  Should succeed.
        {
            SignerList aliceSigners;
            STTx tx = getSignerListSetTx (alice, aliceSigners, 0);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
            expect(getOwnerCount (ledger, alice) == 0);
        }
    }

//------------------------------------------------------------------------------

    void test_phantomSigners (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        // Attach phantom signers to alice.  Should work.
        {
            SignerList aliceSigners {{bogie, 3}, {ghost, 3}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 6);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Make a multi-signed payment from alice to master.  Should succeed.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, bogie, tx},
                {alice, ghost, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);

            // You can't re-use signatures on a new transaction.  Verify that.
            STTx badTx = getPaymentTx (alice, master, 1000 - stdFee);
            multiSign (badTx, multiSigs);
            ledger.applyBadTransaction (badTx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Try mal-ordered signers.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> badSigs {
                {alice, bogie, tx},
                {alice, ghost, tx}
            };
            std::sort (badSigs.begin(), badSigs.end());
            std::reverse (badSigs.begin(), badSigs.end());
            insertMultiSigs (tx, badSigs);
            ledger.applyBadTransaction (tx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Try duplicate signers.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> badSigs {
                {alice, ghost, tx},
                {alice, ghost, tx}
            };
            multiSign (tx, badSigs);
            ledger.applyBadTransaction (tx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Both single- and and multi-sign.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, bogie, tx},
                {alice, ghost, tx}
            };
            singleSign (tx, alice);
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Don't meet the quorum.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> badSigs {
                {alice, bogie, tx}
            };
            multiSign (tx, badSigs);
            ledger.applyBadTransaction (tx, tefBAD_QUORUM);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Multi-sign where one of the signers is not valid.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, bogie, tx},
                {alice, ghost, tx},
                {alice, haunt, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
    }

//------------------------------------------------------------------------------

    void test_masterSigners (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);

        UserAccount cheri (kType, "cheri");
        payInDrops (ledger, master, cheri, 1000*xrp);

        UserAccount daria (kType, "daria");
        payInDrops (ledger, master, daria, 1000*xrp);

        // To mix things up, give alice a regular key, but don't use it.
        alice.setRegKey (ledger, kType, "alie");
        aliceBalanceCheck (stdFee);

        // Attach signers to alice.  Should work.
        {
            SignerList aliceSigners {{becky, 3}, {cheri, 4}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 7);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Make a multi-signed payment from alice to master.  Should succeed.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // Attempt a multi-signed transaction that doesn't meet the quorum
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_QUORUM);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Attempt a multi-signed transaction where one signer is not valid.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx},
                {alice, daria, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Give becky and cheri regular keys but don't use them.  Should work.
        {
            becky.setRegKey (ledger, kType, "beck");
            cheri.setRegKey (ledger, kType, "cher");

            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
    }

//------------------------------------------------------------------------------

    void test_regularSigners (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts.  Have everyone use regular keys.
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);

        alice.setRegKey (ledger, kType, "alie");
        alice.useRegKey (true);
        aliceBalanceCheck (stdFee);

        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);
        becky.setRegKey (ledger, kType, "beck");
        becky.useRegKey (true);

        // Disable cheri's master key to mix things up.
        UserAccount cheri (kType, "cheri");
        payInDrops (ledger, master, cheri, 1000*xrp);
        cheri.setRegKey (ledger, kType, "cher");
        cheri.disableMaster(ledger, true);
        cheri.useRegKey (true);

        UserAccount daria (kType, "daria");
        payInDrops (ledger, master, daria, 1000*xrp);
        daria.setRegKey (ledger, kType, "darr");
        daria.useRegKey (true);

        // Attach signers to alice.  Should work.
        {
            SignerList aliceSigners {{becky, 3}, {cheri, 4}};
            STTx tx = getSignerListSetTx (alice, aliceSigners, 7);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }

        // Make a multi-signed payment from alice to master.  Should succeed.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }

        // Attempt a multi-signed transaction that doesn't meet the quorum
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_QUORUM);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }

        // Attempt a multi-signed transaction where one signer is not valid.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx},
                {alice, daria, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }

        // Have becky sign with her master key and then disable the
        // master before we submit the transaction.  Should fail.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            becky.useRegKey (false);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            becky.disableMaster(ledger, true);
            becky.useRegKey (true);
            ledger.applyBadTransaction (tx, tefMASTER_DISABLED);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }

        // Now that becky is using her regular key her signature should succeed.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
    }

//------------------------------------------------------------------------------

    void test_heterogeneousSigners (KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts.  alice uses a regular key with the master disabled.
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        payInDrops (ledger, master, alice, aliceBalance);
        aliceBalanceCheck (0);
        alice.setRegKey (ledger, kType, "alie");
        aliceBalanceCheck (stdFee);

        alice.disableMaster (ledger, true);
        aliceBalanceCheck (stdFee);

        alice.useRegKey (true);

        // becky is master only, without a regular key.
        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);

        // cheri is master, but with a regular key.
        UserAccount cheri (kType, "cheri");
        payInDrops (ledger, master, cheri, 1000*xrp);
        cheri.setRegKey (ledger, kType, "cher");

        // daria uses her regular key, but leaves the master enabled.
        UserAccount daria (kType, "daria");
        payInDrops (ledger, master, daria, 1000*xrp);
        daria.setRegKey (ledger, kType, "dar");
        cheri.useRegKey (true);

        // edith disables the master and uses her regular key.
        UserAccount edith (kType, "edith");
        payInDrops (ledger, master, edith, 1000*xrp);
        edith.setRegKey (ledger, kType, "edi");
        edith.disableMaster(ledger, true);
        edith.useRegKey (true);

        SignerList aliceSigners {
            {becky, 1},
            {cheri, 1},
            {daria, 1},
            {edith, 1},
            {ghost, 1},
            {haunt, 0}
        };
        // Attach signers to alice.
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Each type of signer (with weight) should succeed individually.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{alice, becky, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{alice, cheri, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{alice, daria, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{alice, edith, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{alice, ghost, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // Should also be no sweat if all of the signers (with weight) sign.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx},
                {alice, daria, tx},
                {alice, edith, tx},
                {alice, ghost, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // But the transaction should fail if a zero-weight signer is included.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx},
                {alice, daria, tx},
                {alice, edith, tx},
                {alice, ghost, tx},
                {alice, haunt, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Require that all weighted signers sign.
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 5);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        {
            // Make sure that works.
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {alice, cheri, tx},
                {alice, daria, tx},
                {alice, edith, tx},
                {alice, ghost, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
    }

//------------------------------------------------------------------------------

    void test_twoLevel(KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts.
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        // alice uses a regular key with the master disabled.
        payInDrops (ledger, master, alice, 1000*xrp);
        aliceBalanceCheck (0);
        alice.setRegKey (ledger, kType, "alie");
        aliceBalanceCheck (stdFee);
        alice.disableMaster (ledger, true);
        aliceBalanceCheck (stdFee);
        alice.useRegKey (true);

        // becky is master only, without a regular key.
        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);

        // cheri is master, but with a regular key.
        UserAccount cheri (kType, "cheri");
        payInDrops (ledger, master, cheri, 1000*xrp);
        cheri.setRegKey (ledger, kType, "cher");

        // daria uses her regular key, but leaves the master enabled.
        UserAccount daria (kType, "daria");
        payInDrops (ledger, master, daria, 1000*xrp);
        daria.setRegKey (ledger, kType, "dar");
        cheri.useRegKey (true);

        // edith disables the master and uses her regular key.
        UserAccount edith (kType, "edith");
        payInDrops (ledger, master, edith, 1000*xrp);
        edith.setRegKey (ledger, kType, "edi");
        edith.disableMaster(ledger, true);
        edith.useRegKey (true);

        // Fund four more accounts so alice can have 8 in-ledger signers.
        UserAccount freda (kType, "freda");
        payInDrops (ledger, master, freda, 1000*xrp);

        UserAccount ginny (kType, "ginny");
        payInDrops (ledger, master, ginny, 1000*xrp);

        UserAccount helen (kType, "helen");
        payInDrops (ledger, master, helen, 1000*xrp);

        UserAccount irena (kType, "irena");
        payInDrops (ledger, master, irena, 1000*xrp);

        // Attach signers to alice.
        SignerList aliceSigners {
            {becky, 1},
            {cheri, 1},
            {daria, 1},
            {edith, 1},
            {freda, 1},
            {ginny, 1},
            {helen, 1},
            {irena, 1},
        };
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 1);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Attach signers to becky.
        SignerList beckySigners {
            {alice, 1},
            {cheri, 1},
            {daria, 1},
            {edith, 1},
            {freda, 1},
            {ghost, 1},
            {haunt, 0},
            {irena, 1},
        };
        {
            STTx tx = getSignerListSetTx (becky, beckySigners, 1);
            singleSign (tx, becky);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to cheri.
        {
            SignerList cheriSigners {
                {alice, 1},
                {becky, 1},
                {daria, 1},
                {edith, 1},
                {freda, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (cheri, cheriSigners, 8);
            singleSign (tx, cheri);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to daria.
        {
            SignerList dariaSigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {edith, 1},
                {freda, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (daria, dariaSigners, 8);
            singleSign (tx, daria);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to edith.
        {
            SignerList edithSigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {daria, 1},
                {freda, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (edith, edithSigners, 8);
            singleSign (tx, edith);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to freda.
        {
            SignerList fredaSigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {daria, 1},
                {edith, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (freda, fredaSigners, 8);
            singleSign (tx, freda);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to ginny.
        {
            SignerList ginnySigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {daria, 1},
                {edith, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (ginny, ginnySigners, 8);
            singleSign (tx, ginny);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to helen.
        {
            SignerList helenSigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {daria, 1},
                {edith, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (helen, helenSigners, 8);
            singleSign (tx, helen);
            ledger.applyGoodTransaction (tx);
        }
        // Attach signers to irena.
        {
            SignerList irenaSigners {
                {alice, 1},
                {becky, 1},
                {cheri, 1},
                {daria, 1},
                {edith, 1},
                {ghost, 1},
                {haunt, 1},
                {helen, 1},
            };
            STTx tx = getSignerListSetTx (irena, irenaSigners, 8);
            singleSign (tx, irena);
            ledger.applyGoodTransaction (tx);
        }

        // becky signing both directly and through a signer list should fail.
        //
        // This takes a little explanation.  It isn't easy to see in this
        // format, but becky is attempting to sign this transaction twice.
        //
        //  o The first one you can see.  Becky signs for alice on alice's
        //    account.
        //
        //  o The second is harder to see.  cheri is signing for becky.  But
        //    at the end of the day, it is becky who is signing on alice's
        //    account even though cheri is signing *for* becky.
        //
        // If we allow becky to sign both these ways then she would get twice
        // as much weight as she is alloted.  So we must reject this case.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                {becky, cheri, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Each type of signer (with weight) should succeed individually.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, alice, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, cheri, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, daria, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, freda, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, ghost, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // Transaction should fail if becky signs for herself.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, becky, tx}};
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, temINVALID);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Transaction should fail if haunt signs, since haunt has zero weight.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {{becky, haunt, tx}};
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Crank up becky's quorum so she needs all signers.  Just for fun
        // we'll multi-sign it.
        {
            STTx tx = getSignerListSetTx (becky, beckySigners, 7);
            std::vector<MultiSig> multiSigs {{becky, alice, tx}};
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
        }
        // A transaction that's one signature short at the second level fails.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {becky, alice, tx},
                {becky, cheri, tx},
                {becky, daria, tx},
                {becky, edith, tx},
                {becky, freda, tx},
                {becky, ghost, tx}
//              {becky, irena, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyBadTransaction (tx, tefBAD_QUORUM);
            alice.decrSeq (); // Fix up local account sequence number.
            aliceBalanceCheck (0);
        }
        // Add in the necessary signature and succeed.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {becky, alice, tx},
                {becky, cheri, tx},
                {becky, daria, tx},
                {becky, edith, tx},
                {becky, freda, tx},
                {becky, ghost, tx},
                {becky, irena, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // Crank up alice's quorum to try combining 1-level and 2-level signing.
        {
            STTx tx = getSignerListSetTx (alice, aliceSigners, 8);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Mix levels of signing.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, becky, tx},
                    {cheri, alice, tx}, // 2-level signing
                    {cheri, becky, tx},
                    {cheri, daria, tx},
                    {cheri, edith, tx},
                    {cheri, freda, tx},
                    {cheri, ghost, tx},
                    {cheri, haunt, tx},
                    {cheri, irena, tx},
                {alice, daria, tx},
                {alice, edith, tx},
                {alice, freda, tx},
                {alice, ginny, tx},
                {alice, helen, tx},
                {alice, irena, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // Replace becky's signer list so we can go for a worst case signature.
        {
            beckySigners = {
                {alice, 1},
                {cheri, 1},
                {daria, 1},
                {edith, 1},
                {freda, 1},
                {ghost, 1},
                {haunt, 1},
                {irena, 1},
            };
            STTx tx = getSignerListSetTx (becky, beckySigners, 8);
            singleSign (tx, becky);
            ledger.applyGoodTransaction (tx);
        }
        // Make the grandmother of all 2-level signatures.  Should work.
        {
            STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {becky, alice, tx}, // becky
                {becky, cheri, tx},
                {becky, daria, tx},
                {becky, edith, tx},
                {becky, freda, tx},
                {becky, ghost, tx},
                {becky, haunt, tx},
                {becky, irena, tx},

                {cheri, alice, tx}, // cheri
                {cheri, becky, tx},
                {cheri, daria, tx},
                {cheri, edith, tx},
                {cheri, freda, tx},
                {cheri, ghost, tx},
                {cheri, haunt, tx},
                {cheri, irena, tx},

                {daria, alice, tx}, // daria
                {daria, becky, tx},
                {daria, cheri, tx},
                {daria, edith, tx},
                {daria, freda, tx},
                {daria, ghost, tx},
                {daria, haunt, tx},
                {daria, irena, tx},

                {edith, alice, tx}, // edith
                {edith, becky, tx},
                {edith, cheri, tx},
                {edith, daria, tx},
                {edith, freda, tx},
                {edith, ghost, tx},
                {edith, haunt, tx},
                {edith, irena, tx},

                {freda, alice, tx}, // freda
                {freda, becky, tx},
                {freda, cheri, tx},
                {freda, daria, tx},
                {freda, edith, tx},
                {freda, ghost, tx},
                {freda, haunt, tx},
                {freda, irena, tx},

                {ginny, alice, tx}, // ginny
                {ginny, becky, tx},
                {ginny, cheri, tx},
                {ginny, daria, tx},
                {ginny, edith, tx},
                {ginny, ghost, tx},
                {ginny, haunt, tx},
                {ginny, irena, tx},

                {helen, alice, tx}, // helen
                {helen, becky, tx},
                {helen, cheri, tx},
                {helen, daria, tx},
                {helen, edith, tx},
                {helen, ghost, tx},
                {helen, haunt, tx},
                {helen, irena, tx},

                {irena, alice, tx}, // irena
                {irena, becky, tx},
                {irena, cheri, tx},
                {irena, daria, tx},
                {irena, edith, tx},
                {irena, ghost, tx},
                {irena, haunt, tx},
                {irena, helen, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
    }

//------------------------------------------------------------------------------

    // See if every kind of transaction can be successfully multi-signed.
    void test_txTypes(KeyType kType)
    {
        UserAccount master (kType, "masterpassphrase");

        TestLedger ledger (100000*xrp, master, *this);

        // User accounts.
        UserAccount alice (kType, "alice");
        std::uint64_t aliceBalance = 1000*xrp;

        // This lambda makes it easy to check alice's balance.
        auto aliceBalanceCheck =
            [&ledger, &alice, &aliceBalance, this](std::uint64_t balanceChange)
            {
                aliceBalance -= balanceChange;
                this->expect(getNativeBalance (ledger, alice) == aliceBalance);
            };

        // alice uses a regular key with the master enabled.
        payInDrops (ledger, master, alice, 1000*xrp);
        aliceBalanceCheck (0);
        alice.setRegKey (ledger, kType, "alie");
        aliceBalanceCheck (stdFee);
        alice.useRegKey (true);

        // becky uses a regular key with the master disabled.
        UserAccount becky (kType, "becky");
        payInDrops (ledger, master, becky, 1000*xrp);
        becky.setRegKey (ledger, kType, "alie");
        becky.disableMaster (ledger, true);
        becky.useRegKey (true);

        // Attach signers to alice.
        {
            SignerList aliceSigners { {becky, 1}, {bogie, 1}, };
            STTx tx = getSignerListSetTx (alice, aliceSigners, 2);
            singleSign (tx, alice);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (stdFee);
        }
        // Attach signers to becky.
        {
            SignerList beckySigners { {ghost, 1}, {haunt, 1}, };
            STTx tx = getSignerListSetTx (becky, beckySigners, 1);
            singleSign (tx, becky);
            ledger.applyGoodTransaction (tx);
        }
        // 2-level multi-sign a ttPAYMENT.
        {
            STTx tx = getPaymentTx (alice, master, 1000 - stdFee);
            std::vector<MultiSig> multiSigs {
                {alice, bogie, tx},
                {becky, ghost, tx}
            };
            multiSign (tx, multiSigs);
            ledger.applyGoodTransaction (tx);
            aliceBalanceCheck (1000);
        }
        // 2-level multi-sign a ttACCOUNT_SET
        {
            // Multi-sign disable alice's master key.  Should fail.
            {
                STTx tx = getAccountSetTx (alice);
                tx.setFieldU32 (sfSetFlag, asfDisableMaster);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyTecTransaction (tx, tecNEED_MASTER_KEY);
                aliceBalanceCheck (stdFee);
            }
            // Disable alice's master key.
            {
                alice.useRegKey (false);
                STTx tx = getAccountSetTx (alice);
                tx.setFieldU32 (sfSetFlag, asfDisableMaster);
                singleSign (tx, alice);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Make sure the master key was disabled.
            {
                STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
                singleSign (tx, alice);
                ledger.applyBadTransaction (tx, tefMASTER_DISABLED);
                alice.decrSeq (); // Fix up local account sequence number.
                aliceBalanceCheck (0);
            }
            // Re-enable alice's master key.
            {
                STTx tx = getAccountSetTx (alice);
                tx.setFieldU32 (sfClearFlag, asfDisableMaster);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Make sure the master key was enabled.
            {
                alice.useRegKey (false);
                STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
                singleSign (tx, alice);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (1000);
                alice.useRegKey (true);
            }
        }
        // 2-level multi-sign a ttREGULAR_KEY_SET.
        {
            // Multi-sign changing alice's regular key.
            {
                RippleAddress const seed =
                    RippleAddress::createSeedGeneric("BadNewsBears");
                KeyPair regular = generateKeysFromSeed (kType, seed);
                STTx tx = getSetRegularKeyTx (
                    alice, regular.publicKey.getAccountID());

                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Since we didn't tell the local alice that we changed her
            // regular key, she should no longer be able to regular sign.
            {
                alice.useRegKey (true);
                STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
                singleSign (tx, alice);
                ledger.applyBadTransaction (tx, tefBAD_AUTH);
                alice.decrSeq (); // Fix up local account sequence number.
                aliceBalanceCheck (0);
            }
            // Restore alice's regular key.
            {
                RippleAddress const seed =
                    RippleAddress::createSeedGeneric("alie");
                KeyPair regular = generateKeysFromSeed (kType, seed);
                STTx tx = getSetRegularKeyTx (
                    alice, regular.publicKey.getAccountID());

                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Regular signing should work again for alice.
            {
                alice.useRegKey (true);
                STTx tx = getPaymentTx(alice, master, 1000 - stdFee);
                singleSign (tx, alice);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (1000);
            }
        }
        // We need a Currency and Issue.  The next tests use non-XRP.
        ripple::Currency const knuts = to_currency("KNT");
        UserAccount gringots (kType, "Gringots Wizarding Bank");
        payInDrops (ledger, master, gringots, 10000*xrp);
        Issue const gringotsKnuts (knuts, gringots.getID());

        // 2-level multi-sign a ttTRUST_SET transaction.
        {
            // Sending 5 knuts from gringots to alice should fail without a
            // trust line.
            {
                STAmount const payment (gringotsKnuts, 50);
                STTx tx = getPaymentTx (gringots, alice, payment);
                singleSign (tx, gringots);
                ledger.applyTecTransaction (tx, tecPATH_DRY);
            }
            // 2-level multi-sign a ttTRUST_SET.
            {
                STTx tx = getTrustSetTx (alice, gringotsKnuts, 100);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // We should now be able to send 50 knuts from gringots to alice.
            {
                STAmount const payment (gringotsKnuts, 50);
                STTx tx = getPaymentTx (gringots, alice, payment);
                singleSign (tx, gringots);
                ledger.applyGoodTransaction (tx);
            }
            // Make sure alice got her knuts.
            {
                std::vector<RippleState::pointer> states =
                    getRippleStates (ledger, alice, gringots);
                expect (states.size() == 1);
                if (!states.empty())
                {
                    STAmount const balance = states[0]->getBalance();
                    STAmount const expected (gringotsKnuts, 50);
                    expect (balance == expected);
                }
            }
        }
        // 2-level multi-sign ttOFFER_CREATE and ttOFFER_CANCEL transactions.
        {
            // Values shared by subsections:
            STAmount const takerGets (gringotsKnuts, 50);
            STAmount const takerPays (50);
            std::uint32_t offerSeq = 0;

            // alice has 50 knuts.  She'll offer to trade them for 50 XRP.
            {
                STTx tx = getOfferCreateTx (alice, takerGets, takerPays);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Verify that alice has an offer.
            {
                auto offers =
                    getOffersOnAccount (ledger, alice);
                expect (offers.size() == 1);
                if (! offers.empty())
                {
                    auto const& offer = offers[0];
                    expect (takerGets == offer->getFieldAmount(sfTakerGets));
                    expect (takerPays == offer->getFieldAmount(sfTakerPays));
                    offerSeq = offer->getFieldU32 (sfSequence);
                }
            }
            // Cancel alice's offer using a multi-signed transaction.
            {
                STTx tx = getOfferCancelTx (alice, offerSeq);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Make sure alice's offer is really gone from the ledger.
            {
                auto offers =
                    getOffersOnAccount (ledger, alice);
                expect (offers.empty() == true);
            }
        }
        // Multi-sign a ttSIGNER_LIST_SET
        {
            // Give alice a new signer list that bogie can no longer sign.
            {
                SignerList aliceSigners { {becky, 1}, {ghost, 1}, };
                STTx tx = getSignerListSetTx (alice, aliceSigners, 2);
                std::vector<MultiSig> multiSigs {
                    {alice, becky, tx},
                    {alice, bogie, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Makes sure that becky and bogie can no longer sign.
            {
                STTx tx = getPaymentTx (alice, master, 1000 - stdFee);
                std::vector<MultiSig> multiSigs {
                    {alice, becky, tx},
                    {alice, bogie, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyBadTransaction (tx, tefBAD_SIGNATURE);
                alice.decrSeq (); // Fix up local account sequence number.
                aliceBalanceCheck (0);
            }
            // Make sure that becky and ghost can sign.
            {
                STTx tx = getPaymentTx (alice, master, 1000 - stdFee);
                std::vector<MultiSig> multiSigs {
                    {alice, becky, tx},
                    {alice, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (1000);
            }
            // Put alice's account back the way it was.
            {
                SignerList aliceSigners { {becky, 1}, {bogie, 1}, };
                STTx tx = getSignerListSetTx (alice, aliceSigners, 2);
                std::vector<MultiSig> multiSigs {
                    {alice, becky, tx},
                    {alice, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
        }
#if RIPPLE_ENABLE_TICKETS
        // Multi-sign a ttTICKET_CREATE and cancel it using ttTICKET_CANCEL.
        {
            uint256 ticketIndex {7}; // Any non-zero value so we see it change.
            // Multi-sign to give alice an un-targeted ticket
            {
                STTx tx = getCreateTicketTx (alice);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Make sure Alice has the ticket.
            {
                auto
                    getTicketsOnAccount (ledger, alice);
                expect (tickets.size() == 1);
                if (! tickets.empty())
                {
                    SLE::pointer const& ticket = tickets[0];
                    std::uint32_t const ticketSeq =
                        ticket->getFieldU32(sfSequence);
                    // getTicketIndex() hashes the account and seq for the ID.
                    ticketIndex = getTicketIndex (alice.getID(), ticketSeq);
                }
            }
            // Multi-sign to cancel alice's ticket.
            {
                STTx tx = getCancelTicketTx (alice, ticketIndex);
                std::vector<MultiSig> multiSigs {
                    {alice, bogie, tx},
                    {becky, ghost, tx}
                };
                multiSign (tx, multiSigs);
                ledger.applyGoodTransaction (tx);
                aliceBalanceCheck (stdFee);
            }
            // Make sure the ticket is gone.
            {
                auto tickets =
                    getTicketsOnAccount (ledger, alice);
                expect (tickets.size() == 0);
            }
        }
#endif // RIPPLE_ENABLE_TICKETS
    }

public:
    void run ()
    {
        for (auto kType : {KeyType::secp256k1, KeyType::ed25519})
        {
            test_singleSig (kType);
#if RIPPLE_ENABLE_MULTI_SIGN
            test_noReserve(kType);
            test_noReserve(kType);
            test_signerListSet (kType);
            test_phantomSigners (kType);
            test_masterSigners (kType);
            test_regularSigners (kType);
            test_heterogeneousSigners (kType);
            test_twoLevel (kType);
            test_txTypes (kType);
#endif // RIPPLE_ENABLE_MULTI_SIGN
        }
    }
};

BEAST_DEFINE_TESTSUITE(MultiSign,ripple_app,ripple);

} // test
} // ripple
