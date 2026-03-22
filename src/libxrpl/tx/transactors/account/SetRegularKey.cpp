#include <xrpl/basics/Log.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/account/SetRegularKey.h>

namespace xrpl {

XRPAmount
SetRegularKey::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const id = tx.getAccountID(sfAccount);
    auto const spk = tx.getSigningPubKey();

    if (publicKeyType(makeSlice(spk)))
    {
        if (calcAccountID(PublicKey(makeSlice(spk))) == id)
        {
            AccountRoot const acct(id, view);

            if (acct && (!(acct->getFlags() & lsfPasswordSpent)))
            {
                // flag is armed and they signed with the right account
                return XRPAmount{0};
            }
        }
    }

    return Transactor::calculateBaseFee(view, tx);
}

NotTEC
SetRegularKey::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfRegularKey) &&
        (ctx.tx.getAccountID(sfRegularKey) == ctx.tx.getAccountID(sfAccount)))
    {
        return temBAD_REGKEY;
    }

    return tesSUCCESS;
}

TER
SetRegularKey::doApply()
{
    WritableAccountRoot acct(accountID_, view());
    if (!acct)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!minimumFee(ctx_.registry, ctx_.baseFee, view().fees(), view().flags()))
        acct->setFlag(lsfPasswordSpent);

    if (ctx_.tx.isFieldPresent(sfRegularKey))
    {
        acct->setAccountID(sfRegularKey, ctx_.tx.getAccountID(sfRegularKey));
    }
    else
    {
        // Account has disabled master key and no multi-signer signer list.
        if (acct->isFlag(lsfDisableMaster) && !view().peek(keylet::signers(accountID_)))
            return tecNO_ALTERNATIVE_KEY;

        acct->makeFieldAbsent(sfRegularKey);
    }

    acct.update();

    return tesSUCCESS;
}

}  // namespace xrpl
