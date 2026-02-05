#ifndef XRPL_TX_CONFIDENTIALMERGEINBOX_H_INCLUDED
#define XRPL_TX_CONFIDENTIALMERGEINBOX_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

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
 * @par Balance Updates:
 * - Homomorphically adds inbox balance to spending balance.
 * - Resets inbox balance to encrypted zero (canonical zero encryption).
 * - Increments the `ConfidentialBalanceVersion` to prevent replay attacks.
 *
 * @par Requirements:
 * - The MPTokenIssuance must have `lsfMPTCanPrivacy` flag set.
 * - Holder must have initialized confidential balances (inbox, spending,
 *   and ElGamal public key).
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
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTMergeInbox(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * @brief Validates transaction fields before any ledger access.
     *
     * Checks:
     * - Feature `featureConfidentialTransfer` is enabled.
     * - Sender is not the MPT issuer.
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
     * - MPTokenIssuance exists and has privacy enabled.
     * - Holder's MPToken exists with all required confidential fields
     *   (inbox, spending, and ElGamal public key).
     *
     * @param ctx The preclaim context containing view and transaction.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * @brief Applies the inbox merge to the ledger.
     *
     * - Homomorphically adds inbox balance to spending balance.
     * - Resets inbox to canonical zero encryption.
     * - Increments confidential balance version.
     *
     * @return tesSUCCESS on success, or an appropriate error code.
     */
    TER
    doApply() override;
};

}  // namespace xrpl

#endif
