//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/Account.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STNumber.h>

namespace ripple {

class Vault_test : public beast::unit_test::suite
{
    TEST_CASE(CreateUpdateDelete)
    {
        using namespace test::jtx;
        Env env{*this};

        Account issuer{"issuer"};
        Account owner{"owner"};
        env.fund(XRP(1000), issuer, owner);
        env.close();
        auto vault = env.vault();

        SUBCASE("IOU")
        {
            Asset asset = issuer["IOU"];
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            SUBCASE("nothing to delete")
            {
                tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, ter(tecOBJECT_NOT_FOUND));
                env.close();
            }

            SUBCASE("create")
            {
                env(tx);
                {
                    auto meta = env.meta()->getJson();
                    // JLOG(env.journal.error()) << meta;
                    auto n = 0;
                    for (auto const& affected : meta[sfAffectedNodes])
                    {
                        if (!affected[sfCreatedNode])
                            continue;
                        ++n;
                    }
                }
                BEAST_EXPECT(env.le(keylet));

                tx = vault.del({.owner = issuer, .id = keylet.key});
                env(tx, ter(tecNO_PERMISSION));
                env.close();

                tx = vault.del({.owner = owner, .id = keylet.key});
                env(tx);
                {
                    auto n = 0;
                    auto meta = env.meta()->getJson();
                    for (auto const& affected : meta[sfAffectedNodes])
                    {
                        if (!affected[sfDeletedNode])
                            continue;
                        // JLOG(env.journal.error())
                        //     << affected[sfDeletedNode][sfLedgerEntryType];
                        ++n;
                    }
                }
                BEAST_EXPECT(!env.le(keylet));
                // TODO: Assert all the entries created earlier are deleted.
            }

            // The vault owner is the transaction submitter.
            // If that account is missing,
            // then `preclaim` throws an exception.

            SUBCASE("insufficient fee")
            {
                env(tx, fee(env.current()->fees().base), ter(telINSUF_FEE_P));
                env.close();
            }

            SUBCASE("insufficient reserve")
            {
                // It is possible to construct a complicated mathematical
                // expression for this amount, but it is sadly not easy.
                env(pay(owner, issuer, XRP(775)));
                env.close();
                env(tx, ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }

            SUBCASE("global freeze")
            {
                env(fset(issuer, asfGlobalFreeze));
                env.close();
                env(tx, ter(tecFROZEN));
                env.close();
            }

            SUBCASE("data too large")
            {
                tx[sfData] = blob257;
                env(tx, ter(temSTRING_TOO_LARGE));
                env.close();
            }

            SUBCASE("metadata too large")
            {
                // This metadata is for the share token.
                tx[sfMPTokenMetadata] = blob1025;
                env(tx, ter(temSTRING_TOO_LARGE));
                env.close();
            }
        }

        SUBCASE("MPT")
        {
            MPTTester mptt{env, issuer, {.fund = false}};
            mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            Asset asset = mptt.issuanceID();
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});

            SUBCASE("create")
            {
                env(tx);
                env.close();

                SUBCASE("update")
                {
                    auto tx = vault.set({.owner = owner, .id = keylet.key});

                    SUBCASE("happy path")
                    {
                        tx[sfData] = "ABCD";
                        tx[sfAssetMaximum] = 123;
                        env(tx);
                        env.close();
                    }

                    SUBCASE("not the owner")
                    {
                        tx[sfAccount] = issuer.human();
                        env(tx, ter(tecNO_PERMISSION));
                        env.close();
                    }

                    SUBCASE("data too large")
                    {
                        tx[sfData] = blob257;
                        env(tx, ter(temSTRING_TOO_LARGE));
                        env.close();
                    }

                    SUBCASE("shrinking assets")
                    {
                        // TODO: VaultSet (update) fail: AssetMaximum <
                        // AssetTotal
                    }

                    SUBCASE("immutable flags")
                    {
                        tx[sfFlags] = tfVaultPrivate;
                        env(tx, ter(temINVALID_FLAG));
                        env.close();
                    }
                }
            }

            SUBCASE("global lock")
            {
                mptt.set({.account = issuer, .flags = tfMPTLock});
                env(tx, ter(tecLOCKED));
            }
        }

        SUBCASE("MPT cannot transfer")
        {
            MPTTester mptt{env, issuer, {.fund = false}};
            mptt.create();
            Asset asset = mptt.issuanceID();
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, ter(tecLOCKED));
        }

        // TODO: VaultSet (update) succeed
        // TODO: VaultSet (update) fail: wrong owner
        // TODO: VaultSet (update) fail: Data too large
        // TODO: VaultSet (update) fail: tfPrivate flag
        // TODO: VaultSet (update) fail: tfShareNonTransferable flag
        // TODO: Payment to VaultSet.PA fail
        // TODO: VaultSet (update) fail: missing vault

        BEAST_EXPECT(true);
    }

public:
    void
    run() override
    {
        EXECUTE(CreateUpdateDelete);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple
