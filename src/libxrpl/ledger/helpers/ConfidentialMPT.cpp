#include <xrpl/ledger/helpers/ConfidentialMPT.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <limits>

namespace xrpl {

XRPAmount
confidentialMptBaseFee(ReadView const& view, STTx const& tx)
{
    // Mirror Transactor::calculateBaseFee, then apply confidential multiplier.
    // Kept here (not via Transactor.h) to avoid ledger→tx module dependency.
    XRPAmount const baseFee = view.fees().base;
    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 0;
    return (baseFee + (signerCount * baseFee)) * kConfidentialTransferFeeMultiplier;
}

bool
auditorMigrationPending(SLE const& issuance) noexcept
{
    return issuance.isFieldPresent(sfPendingAuditorEncryptionKey);
}

NotTEC
preflightCiphertext(Slice blob)
{
    confidential::Ciphertext ct{};
    if (!confidential::parseCiphertext(blob, ct))
        return temBAD_CIPHERTEXT;
    return tesSUCCESS;
}

NotTEC
preflightPoint33(Slice blob)
{
    confidential::CompressedPoint p{};
    if (!confidential::parseCompressedPoint(blob, p))
        return temMALFORMED;
    return tesSUCCESS;
}

TER
parseCiphertextField(Slice blob, confidential::Ciphertext& out)
{
    // Apply-time parse: tem* is preflight-only. Malformed ledger or
    // post-preflight ciphertext is a claimed failure, not a local malformation.
    if (!confidential::parseCiphertext(blob, out))
        return tecBAD_PROOF;
    return tesSUCCESS;
}

bool
sameC1(confidential::Ciphertext const& a, confidential::Ciphertext const& b) noexcept
{
    return a.c1 == b.c1;
}

TER
encZeroFor(
    ReadView const&,
    SLE const& issuance,
    AccountID const& holder,
    confidential::CompressedPoint const& pk,
    confidential::Ciphertext& out)
{
    auto const issuer = issuance[sfIssuer];
    auto const mptId = makeMptID(issuance[sfSequence], issuer);
    if (!confidential::encZero(
            pk,
            Slice(holder.data(), holder.size()),
            Slice(issuer.data(), issuer.size()),
            Slice(mptId.data(), mptId.size()),
            out))
        return tecINTERNAL;
    return tesSUCCESS;
}

void
incrementConfidentialVersion(SLE& mpt)
{
    auto const v = mpt[~sfConfidentialBalanceVersion].valueOr(0);
    if (v == std::numeric_limits<std::uint32_t>::max())
        mpt[sfConfidentialBalanceVersion] = 0;
    else
        mpt[sfConfidentialBalanceVersion] = v + 1;
}

void
setCiphertextField(SLE& sle, SField const& field, confidential::Ciphertext const& ct)
{
    confidential::CiphertextBytes raw{};
    confidential::serializeCiphertext(ct, Slice(raw.data(), raw.size()));
    sle.setFieldVL(field, Slice(raw.data(), raw.size()));
}

TER
clearConfidentialState(SLE& issuance, SLE& mpt)
{
    auto const holderCount = issuance[sfConfidentialHolderCount];
    if (holderCount == 0)
        return tecINTERNAL;

    issuance[sfConfidentialHolderCount] = holderCount - 1;
    mpt.makeFieldAbsent(sfHolderEncryptionKey);
    mpt.makeFieldAbsent(sfConfidentialBalanceSpending);
    mpt.makeFieldAbsent(sfConfidentialBalanceInbox);
    mpt.makeFieldAbsent(sfIssuerEncryptedBalance);
    mpt.makeFieldAbsent(sfAuditorEncryptedBalance);
    mpt.makeFieldAbsent(sfAuditorKeyVersion);
    mpt.makeFieldAbsent(sfConfidentialBalanceVersion);
    return tesSUCCESS;
}

TER
checkPlaintextCiphertexts(
    std::uint64_t amount,
    confidential::Scalar const& r,
    confidential::CompressedPoint const& holderPk,
    confidential::CompressedPoint const& issuerPk,
    std::optional<confidential::CompressedPoint> const& auditorPk,
    confidential::Ciphertext const& holderCt,
    confidential::Ciphertext const& issuerCt,
    std::optional<confidential::Ciphertext> const& auditorCt)
{
    confidential::Ciphertext expectH{};
    confidential::Ciphertext expectI{};
    if (!confidential::elgamalEncrypt(holderPk, amount, r, expectH) ||
        !confidential::elgamalEncrypt(issuerPk, amount, r, expectI))
        return tecBAD_PROOF;
    // SPEC INCONSISTENCY: Updated size tables count a shared C1 once; XLS and
    // field tables keep 66-byte ciphertexts. Canonical wire is 66 bytes with
    // identical C1 values.
    if (holderCt.c1 != expectH.c1 || holderCt.c2 != expectH.c2)
        return tecBAD_PROOF;
    if (issuerCt.c1 != expectI.c1 || issuerCt.c2 != expectI.c2)
        return tecBAD_PROOF;
    if (holderCt.c1 != issuerCt.c1)
        return tecBAD_PROOF;
    if (auditorPk)
    {
        if (!auditorCt)
            return tecNO_PERMISSION;
        confidential::Ciphertext expectA{};
        if (!confidential::elgamalEncrypt(*auditorPk, amount, r, expectA))
            return tecBAD_PROOF;
        if (auditorCt->c1 != expectA.c1 || auditorCt->c2 != expectA.c2 ||
            auditorCt->c1 != holderCt.c1)
            return tecBAD_PROOF;
    }
    return tesSUCCESS;
}

}  // namespace xrpl
