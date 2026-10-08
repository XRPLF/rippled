#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * @brief Completes holder key recovery for Confidential MPT.
 *
 * The issuer decrypts the holder's issuer mirror (sfIssuerEncryptedBalance), which holds the
 * holder's total confidential balance, and re-encrypts that amount under the holder's
 * sfRecoveryKey. On success, the ciphertext becomes the new sfConfidentialBalanceSpending,
 * sfRecoveryKey replaces sfHolderEncryptionKey and is then removed, sfConfidentialBalanceInbox is
 * reset to the canonical encrypted zero under the new key, and sfConfidentialBalanceVersion is
 * incremented.
 *
 * @par Cryptographic Operations:
 * - **Equality Proof Verification**: Verifies that the new spending
 *   ciphertext, encrypted under sfRecoveryKey, encrypts the same amount as
 *   the holder's issuer mirror (sfIssuerEncryptedBalance), and that the
 *   issuer knows the private key for its registered sfIssuerEncryptionKey.
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

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
