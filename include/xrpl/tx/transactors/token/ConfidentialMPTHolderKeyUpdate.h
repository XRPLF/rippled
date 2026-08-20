#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/**
 * @brief Allows a confidential MPT holder to rotate or recover their ElGamal
 * encryption key.
 *
 * Submitted by the holder. Exactly one of two modes must be selected via
 * the transaction flags:
 *
 * - Rotation (tfHolderKeyRotation): the holder still has their current
 *   ElGamal private key. They provide a new public key, along with their
 *   current spending/inbox balances re-encrypted under that new key, a
 *   Compact Pedersen proof that the re-encryption preserves the
 *   encrypted amounts, and a Schnorr proof of knowledge of the new private
 *   key. The holder's encryption key and confidential balances are updated
 *   immediately.
 *
 * - Recovery (tfHolderKeyRecovery): the holder has lost their current
 *   private key. They provide a new public key and a Schnorr proof of
 *   knowledge of the corresponding new private key, but cannot provide
 *   re-encrypted balances. The new key is recorded as a pending
 *   sfRecoveryKey; the confidential balances are left untouched. Completing
 *   the recovery (rewriting the balances) is done separately by the issuer
 *   via ConfidentialMPTRecoverBalance.
 *
 * @note The zero-knowledge proof verification itself
 * (verifyHolderKeyUpdateProof) is currently a placeholder pending crypto-side
 * work; see ConfidentialTransfer.h.
 */
class ConfidentialMPTHolderKeyUpdate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ConfidentialMPTHolderKeyUpdate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
