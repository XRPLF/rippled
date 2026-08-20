#include <xrpl/tx/transactors/token/ConfidentialMPTHolderKeyUpdate.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

std::uint32_t
ConfidentialMPTHolderKeyUpdate::getFlagsMask(PreflightContext const& ctx)
{
    return tfConfidentialMPTHolderKeyUpdateMask;
}

NotTEC
ConfidentialMPTHolderKeyUpdate::preflight(PreflightContext const& ctx)
{
    bool const rotation = ctx.tx.isFlag(tfHolderKeyRotation);
    bool const recovery = ctx.tx.isFlag(tfHolderKeyRecovery);

    // Exactly one of the two mode flags must be set.
    if (rotation == recovery)
        return temINVALID_FLAG;

    auto const account = ctx.tx[sfAccount];
    auto const issuer = MPTIssue(ctx.tx[sfMPTokenIssuanceID]).getIssuer();

    // The issuer cannot hold confidential balances.
    if (account == issuer)
        return temMALFORMED;

    if (ctx.tx[sfHolderEncryptionKey].length() != kEcPubKeyLength)
        return temMALFORMED;

    bool const hasSpending = ctx.tx.isFieldPresent(sfConfidentialBalanceSpending);
    bool const hasInbox = ctx.tx.isFieldPresent(sfConfidentialBalanceInbox);

    // Rotation mode requires re-encrypted balances; Recovery mode must not
    // provide them since the holder cannot decrypt the current ones.
    if (rotation && (!hasSpending || !hasInbox))
        return temMALFORMED;

    if (recovery && (hasSpending || hasInbox))
        return temMALFORMED;

    // Check length of fields
    if (hasSpending)
    {
        auto const spending = ctx.tx[sfConfidentialBalanceSpending];
        if (spending.length() != kEcGamalEncryptedTotalLength || !isValidCiphertext(spending))
            return temBAD_CIPHERTEXT;
    }

    if (hasInbox)
    {
        auto const inbox = ctx.tx[sfConfidentialBalanceInbox];
        if (inbox.length() != kEcGamalEncryptedTotalLength || !isValidCiphertext(inbox))
            return temBAD_CIPHERTEXT;
    }

    if (!ctx.tx.isFieldPresent(sfZKProof))
        return temMALFORMED;

    auto const expectedProofLength =
        rotation ? kEcHolderKeyRotationProofLength : kEcHolderKeyRecoveryProofLength;
    if (ctx.tx[sfZKProof].length() != expectedProofLength)
        return temMALFORMED;

    return tesSUCCESS;
}

XRPAmount
ConfidentialMPTHolderKeyUpdate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // TODO: Discussion of higher fee than kConfidentialFeeMultiplier
    return Transactor::calculateBaseFee(view, tx, kConfidentialFeeMultiplier);
}

TER
ConfidentialMPTHolderKeyUpdate::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const mptIssuanceID = ctx.tx[sfMPTokenIssuanceID];

    auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const sleMptoken = ctx.view.read(keylet::mptoken(mptIssuanceID, account));
    if (!sleMptoken)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    if (!sleMptoken->isFieldPresent(sfHolderEncryptionKey) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceSpending) ||
        !sleMptoken->isFieldPresent(sfConfidentialBalanceInbox))
    {
        return tecNO_PERMISSION;
    }

    auto const newPubKey = ctx.tx[sfHolderEncryptionKey];
    if (newPubKey == (*sleMptoken)[sfHolderEncryptionKey])
        return tecNO_PERMISSION;

    bool const rotation = ctx.tx.isFlag(tfHolderKeyRotation);

    // TODO: replace with a real holder-key-update context hash once the
    // crypto side lands (mirrors getSendContextHash / getConvertContextHash
    // used by the other confidential transactors).
    uint256 const contextHash{};

    auto const res = verifyHolderKeyUpdateProof(
        rotation ? HolderKeyUpdateMode::Rotation : HolderKeyUpdateMode::Recovery,
        ctx.tx[sfZKProof],
        newPubKey,
        rotation ? std::optional<Slice>{(*sleMptoken)[sfConfidentialBalanceSpending]}
                 : std::nullopt,
        rotation ? std::optional<Slice>{(*sleMptoken)[sfConfidentialBalanceInbox]} : std::nullopt,
        rotation ? std::optional<Slice>{ctx.tx[sfConfidentialBalanceSpending]} : std::nullopt,
        rotation ? std::optional<Slice>{ctx.tx[sfConfidentialBalanceInbox]} : std::nullopt,
        contextHash);

    if (!isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

TER
ConfidentialMPTHolderKeyUpdate::doApply()
{
    auto const mptIssuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto sleMptoken = view().peek(keylet::mptoken(mptIssuanceID, accountID_));
    if (!sleMptoken)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::ConfidentialMPTHolderKeyUpdate::doApply : preclaim already validated the "
            "MPToken exists");
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const newPubKey = ctx_.tx[sfHolderEncryptionKey];

    if (ctx_.tx.isFlag(tfHolderKeyRotation))
    {
        // The holder still controls the current private key: re-encrypted
        // balances are provided directly and take effect immediately.
        (*sleMptoken)[sfHolderEncryptionKey] = newPubKey;
        (*sleMptoken)[sfConfidentialBalanceSpending] = ctx_.tx[sfConfidentialBalanceSpending];
        (*sleMptoken)[sfConfidentialBalanceInbox] = ctx_.tx[sfConfidentialBalanceInbox];
        incrementConfidentialVersion(*sleMptoken);
    }
    else
    {
        // Recovery mode: the holder cannot decrypt their current balances,
        // so only the pending recovery key is recorded. The balances are
        // rewritten separately by the issuer via ConfidentialMPTRecoverBalance.
        (*sleMptoken)[sfRecoveryKey] = newPubKey;
    }

    view().update(sleMptoken);
    return tesSUCCESS;
}

void
ConfidentialMPTHolderKeyUpdate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
ConfidentialMPTHolderKeyUpdate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
