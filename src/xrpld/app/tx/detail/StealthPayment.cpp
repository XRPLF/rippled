#include <xrpld/app/tx/detail/StealthPayment.h>
#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>
#include <secp256k1.h>

namespace ripple {

TxConsequences
StealthPayment::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfAmount].xrp()};
}

NotTEC
StealthPayment::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureStealthAddresses))
        return temDISABLED;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    // Validate destination exists
    if (!tx.isFieldPresent(sfDestination))
    {
        JLOG(j.trace()) << "Malformed transaction: Missing destination.";
        return temDST_NEEDED;
    }

    // Get and validate amount
    auto const amount = tx.getFieldAmount(sfAmount);
    if (!amount.native())
    {
        JLOG(j.trace()) << "Malformed transaction: Non-XRP amount not "
                           "supported for stealth payments.";
        return temBAD_AMOUNT;
    }

    if (amount <= beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: Amount must be positive.";
        return temBAD_AMOUNT;
    }

    // Validate ephemeral public key field exists
    if (!tx.isFieldPresent(sfEphemeralPublicKey))
    {
        JLOG(j.trace())
            << "Malformed transaction: Missing ephemeral public key.";
        return temMALFORMED;
    }

    // Validate ephemeral public key is correct length (33 bytes compressed)
    auto const ephemeralKey = tx.getFieldVL(sfEphemeralPublicKey);
    if (ephemeralKey.size() != 33)
    {
        JLOG(j.trace()) << "Malformed transaction: Ephemeral public key must "
                           "be 33 bytes (compressed).";
        return temBAD_EPHEMERAL_KEY;
    }

    // Validate it's a valid secp256k1 public key
    secp256k1_context* ctx_secp =
        secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    secp256k1_pubkey pubkey;
    int result = secp256k1_ec_pubkey_parse(
        ctx_secp, &pubkey, ephemeralKey.data(), ephemeralKey.size());
    secp256k1_context_destroy(ctx_secp);

    if (result != 1)
    {
        JLOG(j.trace())
            << "Malformed transaction: Invalid secp256k1 public key.";
        return temBAD_EPHEMERAL_KEY;
    }

    // Check for self-payment
    auto const srcID = tx.getAccountID(sfAccount);
    auto const dstID = tx.getAccountID(sfDestination);
    if (srcID == dstID)
    {
        JLOG(j.trace()) << "Malformed transaction: Cannot send to self.";
        return temDST_IS_SRC;
    }

    return tesSUCCESS;
}

TER
StealthPayment::preclaim(PreclaimContext const& ctx)
{
    auto const dstID = ctx.tx.getAccountID(sfDestination);

    // Check if destination exists
    auto const sleDst = ctx.view.read(keylet::account(dstID));

    // If destination doesn't exist, check minimum amount for account creation
    if (!sleDst)
    {
        auto const amount = ctx.tx.getFieldAmount(sfAmount);
        auto const reserve = ctx.view.fees().accountReserve(0);

        if (amount.xrp() < reserve)
        {
            JLOG(ctx.j.trace())
                << "Insufficient XRP: Account requires reserve of " << reserve;
            return tecNO_DST_INSUF_XRP;
        }
    }
    else
    {
        // If destination exists, verify it's not already a stealth address
        if (sleDst->isFlag(lsfStealthAddress))
        {
            JLOG(ctx.j.trace())
                << "Destination is already a stealth address and cannot "
                   "receive additional payments.";
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
StealthPayment::doApply()
{
    auto const dstID = ctx_.tx.getAccountID(sfDestination);
    auto const srcID = account_;
    auto const amount = ctx_.tx.getFieldAmount(sfAmount);

    // Check if destination exists
    auto sleDst = view().peek(keylet::account(dstID));

    if (!sleDst)
    {
        // Create new stealth account
        auto const reserve = ctx_.view().fees().accountReserve(0);

        // Double-check we have enough (should have been caught in preclaim)
        if (amount.xrp() < reserve)
            return tecNO_DST_INSUF_XRP;

        // Create the account
        sleDst = std::make_shared<SLE>(keylet::account(dstID));
        sleDst->setAccountID(sfAccount, dstID);
        sleDst->setFieldU32(sfSequence, ctx_.view().seq());

        // CRITICAL: Mark as stealth address - ONLY AccountDelete allowed
        sleDst->setFlag(lsfStealthAddress);

        // Set initial balance to zero (will be funded below)
        sleDst->setFieldAmount(sfBalance, STAmount{});

        view().insert(sleDst);

        JLOG(ctx_.journal.trace())
            << "Created new stealth account: " << to_string(dstID);
    }

    // Transfer XRP using the standard accountSend function
    auto const result =
        accountSend(view(), srcID, dstID, amount, ctx_.journal);

    if (result == tesSUCCESS)
    {
        JLOG(ctx_.journal.trace())
            << "StealthPayment: Transferred " << amount.getFullText()
            << " from " << to_string(srcID) << " to " << to_string(dstID);
    }

    return result;
}

}  // namespace ripple
