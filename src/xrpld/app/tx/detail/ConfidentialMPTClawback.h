#ifndef XRPL_TX_CONFIDENTIALCLAWSBACK_H_INCLUDED
#define XRPL_TX_CONFIDENTIALCLAWSBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

/**
 * @brief Allows an MPT issuer to clawback confidential balances from a holder.
 *
 * This transaction enables the issuer of an MPToken Issuance (with clawback
 * enabled) to reclaim confidential tokens from a holder's account. Unlike
 * regular clawback, the issuer cannot see the holder's balance directly.
 * Instead, the issuer must provide a zero-knowledge proof that demonstrates
 * they know the exact encrypted balance amount.
 *
 * @par Cryptographic Operations:
 * - **Equality Proof Verification**: Verifies that the issuer's revealed
 *   amount matches the holder's encrypted balance using the issuer's
 *   ElGamal private key.
 *
 * @par Balance Updates:
 * - Resets holder's confidential balances (inbox, spending, issuer-encrypted,
 *   and auditor-encrypted if applicable) to encrypted zeros.
 * - Decreases the global `ConfidentialOutstandingAmount` and
 *   `OutstandingAmount` on the MPTokenIssuance.
 *
 * @par Requirements:
 * - Only the issuer can execute this transaction.
 * - The MPTokenIssuance must have `lsfMPTCanClawback` flag set.
 * - The issuer must have registered their ElGamal public key.
 * - The holder must have confidential balances to claw back.
 *
 * @see ConfidentialMPTSend, ConfidentialMPTConvert
 */
class ConfidentialMPTClawback : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * @brief Validates transaction fields before any ledger access.
     *
     * Checks:
     * - Feature `featureConfidentialTransfer` is enabled.
     * - Sender is the MPT issuer.
     * - Holder is not the issuer (cannot clawback from self).
     * - Clawback amount is valid (non-zero and within max bounds).
     * - ZKProof length matches expected equality proof size.
     *
     * @param ctx The preflight context containing transaction and rules.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * @brief Validates transaction against current ledger state.
     *
     * Checks:
     * - Sender and holder accounts exist.
     * - MPTokenIssuance exists and has clawback permission.
     * - Issuer has registered ElGamal public key.
     * - Holder has confidential balances.
     * - Clawback amount doesn't exceed confidential outstanding amount.
     * - Verifies the equality proof linking revealed amount to encrypted balance.
     *
     * @param ctx The preclaim context containing view and transaction.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * @brief Applies the clawback to the ledger.
     *
     * - Sets holder's all confidential balances to encrypted zeros.
     * - Decreases global confidential and total outstanding amounts.
     *
     * @return tesSUCCESS on success, or an appropriate error code.
     */
    TER
    doApply() override;
};

}  // namespace xrpl

#endif
