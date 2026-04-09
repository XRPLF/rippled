#pragma once

#include <xrpl/tx/Transactor.h>

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
