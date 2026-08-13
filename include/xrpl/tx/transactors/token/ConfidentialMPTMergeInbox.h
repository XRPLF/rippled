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
 * @brief Merges the confidential inbox balance into the spending balance.
 *
 * In the confidential transfer system, incoming funds are deposited into an
 * "inbox" balance that the recipient cannot immediately spend. This prevents
 * front-running attacks where an attacker could invalidate a pending
 * transaction by sending funds to the sender. This transaction merges the
 * inbox into the spending balance, making those funds available for spending.
 *
 * @par Cryptographic Operations:
 * - **Homomorphic Addition**: Adds the encrypted inbox balance to the
 *   encrypted spending balance using ElGamal homomorphic properties.
 * - **Zero Encryption**: Resets the inbox to an encryption of zero.
 *
 * @note This transaction requires no zero-knowledge proofs because it only
 *       combines encrypted values that the holder already owns. The
 *       homomorphic properties of ElGamal encryption ensure correctness.
 *
 * @see ConfidentialMPTSend, ConfidentialMPTConvert
 */
class ConfidentialMPTMergeInbox : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ConfidentialMPTMergeInbox(ApplyContext& ctx) : Transactor(ctx)
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
