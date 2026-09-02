#pragma once

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>

#include <functional>
#include <optional>

namespace xrpl {

/**
 * The hash prefix that binds a transaction signature to the role that made it:
 * the transaction itself, its counterparty, or its sponsor.
 *
 * @param sigField Field holding the signature: nullptr (or an unseated
 * optional) for the top level signature, otherwise sfCounterpartySignature or
 * sfSponsorSignature.
 * @param multiSigning Whether the signature is a multi-signature.
 * @param rules The current ledger rules.
 */
[[nodiscard]] HashPrefix
signingPrefix(SField const* sigField, bool multiSigning, Rules const& rules);

[[nodiscard]] inline HashPrefix
signingPrefix(
    std::optional<std::reference_wrapper<SField const>> sigField,
    bool multiSigning,
    Rules const& rules)
{
    return signingPrefix(sigField ? &sigField->get() : nullptr, multiSigning, rules);
}

/**
 * Sign an STObject
 *
 * @param st Object to sign
 * @param prefix Prefix to insert before serialized object when hashing
 * @param type Signing key type used to derive public key
 * @param sk Signing secret key
 * @param sigField Field in which to store the signature on the object.
 * If not specified the value defaults to `sfSignature`.
 *
 * @note If a signature already exists, it is overwritten.
 */
void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField = sfSignature);

/**
 * Returns `true` if STObject contains valid signature
 *
 * @param st Signed object
 * @param prefix Prefix inserted before serialized object when hashing
 * @param pk Public key for verifying signature
 * @param sigField Object's field containing the signature.
 * If not specified the value defaults to `sfSignature`.
 */
bool
verify(
    STObject const& st,
    HashPrefix const& prefix,
    PublicKey const& pk,
    SF_VL const& sigField = sfSignature);

/**
 * Return a Serializer suitable for computing a multisigning TxnSignature.
 *
 * @param prefix Prefix to insert before the serialized object. Use
 * signingPrefix to get the prefix for a signature that goes into an
 * alternate field, such as sfSponsorSignature.
 */
Serializer
buildMultiSigningData(
    STObject const& obj,
    AccountID const& signingID,
    HashPrefix prefix = HashPrefix::TxMultiSign);

/**
 * Break the multi-signing hash computation into 2 parts for optimization.
 *
 * We can optimize verifying multiple multisignatures by splitting the
 * data building into two parts;
 *  o A large part that is shared by all of the computations.
 *  o A small part that is unique to each signer in the multisignature.
 *
 * The following methods support that optimization:
 *  1. startMultiSigningData provides the large part which can be shared.
 *  2. finishMultiSigningData caps the passed in serializer with each
 *     signer's unique data.
 */
Serializer
startMultiSigningData(STObject const& obj, HashPrefix prefix = HashPrefix::TxMultiSign);

inline void
finishMultiSigningData(AccountID const& signingID, Serializer& s)
{
    s.addBitString(signingID);
}

}  // namespace xrpl
