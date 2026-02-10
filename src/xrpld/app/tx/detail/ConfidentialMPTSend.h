#ifndef XRPL_TX_CONFIDENTIALSEND_H_INCLUDED
#define XRPL_TX_CONFIDENTIALSEND_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

/**
 * @brief Transfers confidential MPT tokens between holders privately.
 *
 * This transaction enables private token transfers where the transfer amount
 * is hidden from public view. Both sender and recipient must have initialized
 * confidential balances. The transaction provides encrypted amounts for all
 * parties (sender, destination, issuer, and optionally auditor) along with
 * zero-knowledge proofs that verify correctness without revealing the amount.
 *
 * @par Cryptographic Operations:
 * - **Multi-Ciphertext Equality Proof**: Verifies that all encrypted amounts
 *   (sender, destination, issuer, auditor) encrypt the same plaintext value.
 * - **Amount Pedersen Linkage Proof**: Verifies that the amount commitment
 *   correctly links to the sender's encrypted amount.
 * - **Balance Pedersen Linkage Proof**: Verifies that the balance commitment
 *   correctly links to the sender's encrypted spending balance.
 * - **Bulletproof Range Proof**: Verifies remaining balance and
 *   transfer amount are non-negative.
 *
 * @note Funds are deposited into the destination's inbox, not spending
 *       balance. The recipient must call ConfidentialMPTMergeInbox to make
 *       received funds spendable.
 *
 * @see ConfidentialMPTMergeInbox, ConfidentialMPTConvert,
 * ConfidentialMPTConvertBack
 */
class ConfidentialMPTSend : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTSend(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl

#endif
