#pragma once

#include <xrpl/crypto/confidential.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <optional>
namespace xrpl {

XRPAmount
confidentialMptBaseFee(ReadView const& view, STTx const& tx);

[[nodiscard]] bool
auditorMigrationPending(SLE const& issuance) noexcept;

NotTEC
preflightCiphertext(Slice blob);

NotTEC
preflightPoint33(Slice blob);

[[nodiscard]] TER
parseCiphertextField(Slice blob, confidential::Ciphertext& out);

[[nodiscard]] bool
sameC1(
    confidential::Ciphertext const& a,
    confidential::Ciphertext const& b) noexcept;

[[nodiscard]] TER
encZeroFor(
    ReadView const& view,
    SLE const& issuance,
    AccountID const& holder,
    confidential::CompressedPoint const& pk,
    confidential::Ciphertext& out);

void
incrementConfidentialVersion(SLE& mpt);

void
setCiphertextField(SLE& sle, SField const& field, confidential::Ciphertext const& ct);

[[nodiscard]] TER
clearConfidentialState(SLE& issuance, SLE& mpt);

[[nodiscard]] TER
checkPlaintextCiphertexts(
    std::uint64_t amount,
    confidential::Scalar const& r,
    confidential::CompressedPoint const& holderPk,
    confidential::CompressedPoint const& issuerPk,
    std::optional<confidential::CompressedPoint> const& auditorPk,
    confidential::Ciphertext const& holderCt,
    confidential::Ciphertext const& issuerCt,
    std::optional<confidential::Ciphertext> const& auditorCt);

}  // namespace xrpl
