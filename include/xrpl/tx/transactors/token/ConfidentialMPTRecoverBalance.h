#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/**
 * @brief Completes holder key recovery for Confidential MPT.
 *
 * This transaction is submitted by the issuer to complete a holder key
 * recovery previously authorized via ConfidentialMPTHolderKeyUpdate (Recovery mode).
 *
 * The issuer reveals the holder's balance by decrypting sfIssuerEncryptedBalance
 * with their ElGamal private key (the mirror reflects the holder's total confidential
 * balance), then re-encrypts that balance under the holder's MPToken object's
 * sfRecoveryKey and sets it as the new sfConfidentialBalanceSpending.
 * sfConfidentialBalanceInbox is set to an encrypted zero.
 *
 * @par Cryptographic Operations:
 * - **Chaum-Pedersen Equality Proof**: Verifies that the new spending ciphertext
 *   encrypts the same value as the on-ledger sfIssuerEncryptedBalance.
 *
 * @see ConfidentialMPTHolderKeyUpdate
 */
class ConfidentialMPTRecoverBalance : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ConfidentialMPTRecoverBalance(ApplyContext& ctx) : Transactor(ctx)
    {
    }

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
