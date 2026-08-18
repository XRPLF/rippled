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
 * @brief Updates the encrypted mirror balances of a Confidential MPToken.
 *
 * @details
 * This transaction updates a single holder's mirrored confidential balances
 * (`sfIssuerEncryptedBalance` and/or `sfAuditorEncryptedBalance`) with the latest
 * ElGamal public keys defined on the `MPTokenIssuance`.
 *
 * It supports both issuer and holder self-migration modes, each mode supports multiple flows:
 * - Issuer mode: Submitted by the issuer.
 * 1. Issuer Key Rotation Migration: Re-encrypts the
 *   holder's `sfIssuerEncryptedBalance` under the issuer's new ElGamal public key.
 *
 * 2. Auditor Key Rotation Migration: Re-encrypts the
 *   holder's `sfAuditorEncryptedBalance` under the auditor's new ElGamal public key.
 *
 * 3. Simultaneous Rotation Migration: Updates both the issuer
 *   and auditor encrypted balances in a single transaction to optimize network throughput.
 *
 * 4. Auditor Late-Registration Migration: When the issuer ElGamal
 *   public key is already registered on the `MPTokenIssuance` object, the issuer can
 *   register an auditor key at a later time through `MPTokenIssuanceSet`. Then the issuer uses this
 *   flow to set the holder's initial `sfAuditorEncryptedBalance` on `MPToken` object.
 *
 * - Holder self-migration mode: Submitted by the holder. This is the recovery
 *   path used when the issuer has permanently lost private key and can no longer perform
 *   active re-encryption. The holder decrypts their own
 *   `sfConfidentialBalanceSpending` with holder's private key to recover the balance and
 *   re-encrypts it under the relevant new ElGamal public key(s).
 * @note All holder migration flows strictly require the holder's
 *       `sfConfidentialBalanceInbox` to be canonically zero; the holder must run
 *       `ConfidentialMPTMergeInbox` first so the spending balance reflects the
 *       full balance.
 *
 * 5. Holder Issuer-Mirror Migration: Re-encrypts the holder's
 *    `sfIssuerEncryptedBalance` under the issuer's new ElGamal public key.
 *
 * 6. Holder Auditor-Mirror Migration: Re-encrypts the holder's
 *    `sfAuditorEncryptedBalance` under the auditor's new ElGamal public key, or
 *    sets it for the first time when the auditor key was late-registered. Needed
 *    only because the issuer, having lost sk_I, can no longer perform the auditor
 *    re-encryption or initial registration in issuer mode (flows 2 and 4).
 *
 * 7. Simultaneous Holder Self-Migration: Updates both the issuer and auditor
 *    encrypted balances in a single transaction (both keys have rotated).
 */
class ConfidentialMPTMirrorUpdate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ConfidentialMPTMirrorUpdate(ApplyContext& ctx) : Transactor(ctx)
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
