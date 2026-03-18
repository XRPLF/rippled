#pragma once

#include <xrpl/tx/Transactor.h>

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
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTMergeInbox(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates transaction format and cryptographic field structure.
     *  @see §10.3.1
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#1031-data-verification
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state and cryptographic proofs.
     *  @see §10.3.2
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#1032-protocol-level-failures
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Updates ledger state.
     *  @see §10.4
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#104-state-changes
     */
    TER
    doApply() override;
};

}  // namespace xrpl
