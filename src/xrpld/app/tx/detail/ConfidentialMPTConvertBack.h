#ifndef XRPL_TX_CONFIDENTIALCONVERTBACK_H_INCLUDED
#define XRPL_TX_CONFIDENTIALCONVERTBACK_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

/**
 * @brief Converts confidential (encrypted) MPT balance back to public
 * (plaintext) balance.
 *
 * This transaction allows a token holder to convert their encrypted
 * confidential balance back into a publicly visible MPToken balance. The
 * holder must prove they have sufficient confidential balance without
 * revealing the actual balance amount.
 *
 * @par Cryptographic Operations:
 * - **Revealed Amount Verification**: Verifies that the provided encrypted
 *   amounts correctly encrypt the conversion amount.
 * - **Pedersen Linkage Proof**: Verifies that the provided balance commitment
 *   correctly links to the holder's encrypted spending balance.
 * - **Bulletproof Range Proof**: Verifies that the remaining balance (after
 *   conversion) is non-negative, ensuring the holder has sufficient funds.
 *
 * @par Balance Updates:
 * - Increases the holder's public `MPTAmount` balance.
 * - Homomorphically subtracts the encrypted amounts from confidential balances.
 * - Decreases the global `ConfidentialOutstandingAmount` on the MPTokenIssuance.
 * - Increments the holder's `ConfidentialBalanceVersion` to prevent replay.
 *
 * @par Requirements:
 * - The MPTokenIssuance must have `lsfMPTCanPrivacy` flag set.
 * - Holder must have sufficient confidential spending balance.
 * - If auditing is enabled, auditor ciphertext must be provided.
 * - The holder's account must not be frozen or unauthorized.
 *
 * @see ConfidentialMPTConvert, ConfidentialMPTSend
 */
class ConfidentialMPTConvertBack : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTConvertBack(ApplyContext& ctx) : Transactor(ctx)
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
