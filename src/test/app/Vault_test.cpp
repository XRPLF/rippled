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

#include <test/jtx.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {

struct SetArg {

};

class Vault_test : public beast::unit_test::suite
{
    void
    testCreateUpdateDelete()
    {
        using namespace test::jtx;
        testcase("Create / Update / Delete");
        Env env{*this};

        // IOU vault
        {
            // issuer = Account()
            // submit(fund(issuer))
            // asset = issuer[IOU]
            // owner = Account()
            // tx = vault::create(owner=owner, asset=asset)
            // submit(fset(issuer, asfGlobalFreeze))
            // TODO: VaultSet (create) fail: Asset is IOU and issuer.GlobalFreeze
            // submit(tx) => fail
            // tx[sfData] = blob(300)
            // submit(tx) => fail
            // TODO: VaultSet (create) fail: Data too large (>256 bytes)
        }

        // MPT vault
        {
            // represent an issuer account
            // fund the issuer account
            // create issuer.MPT
            // represent an asset for issuer.MPT
            // create a vault with issuer.MPT asset
        }

        // (create) => no sfVaultID
        // (update) => sfVaultID
        // TODO: VaultSet (create) fail: Metadata too large (>1024 bytes)
        // TODO: VaultSet (create) succeed
        // TODO: VaultSet (update) succeed
        // TODO: VaultSet (update) fail: wrong owner
        // TODO: VaultSet (update) fail: Data too large
        // TODO: VaultSet (update) fail: Metadata present
        // TODO: VaultSet (update) fail: AssetMaximum < AssetTotal
        // TODO: VaultSet (update) fail: tfPrivate flag
        // TODO: VaultSet (update) fail: tfShareNonTransferable flag
        // TODO: Payment to VaultSet.PA fail
        // TODO: VaultDelete succeed
        // TODO: VaultDelete fail: missing vault
        // TODO: VaultSet (update) fail: missing vault

        // TODO: VaultSet (create) fail: Asset is MPT but !CanTransfer
        // TODO: VaultSet (create) fail: Asset is MPT but Locked

    }

public:
    void
    run() override
    {
        testCreateUpdateDelete();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple
