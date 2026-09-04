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

#include <optional>

namespace xrpl {

/**
 * The signature slots on a transaction.
 *
 * Each role signs different bytes, so a signature cannot be moved from the
 * role that made it into another role. See signingPrefix.
 */
enum class SignatureRole {
    /**
     * The transaction's own signature, in sfTxnSignature or sfSigners.
     */
    Transaction,
    /**
     * The counterparty's signature, in sfCounterpartySignature.
     */
    Counterparty,
    /**
     * The sponsor's signature, in sfSponsorSignature.
     */
    Sponsor
};

/**
 * The field that holds this role's signature.
 *
 * @return The signature field, or nullptr for SignatureRole::Transaction,
 * whose signature lives at the top level of the transaction.
 */
[[nodiscard]] SField const*
signatureField(SignatureRole role);

/**
 * The role that signs into the given field.
 *
 * @return The role, or an unseated optional if the field does not hold a
 * transaction signature.
 */
[[nodiscard]] std::optional<SignatureRole>
signatureRole(SField const& sigField);

/**
 * The hash prefix that binds a transaction signature to the role that made it.
 *
 * @param role The role making the signature.
 * @param multiSigning Whether the signature is a multi-signature.
 * @param rules The current ledger rules.
 */
[[nodiscard]] HashPrefix
signingPrefix(SignatureRole role, bool multiSigning, Rules const& rules);

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
 * @param prefix Prefix to insert before the serialized object. Get it from
 * signingPrefix, so that the signature is bound to the role making it.
 */
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID, HashPrefix prefix);

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
startMultiSigningData(STObject const& obj, HashPrefix prefix);

inline void
finishMultiSigningData(AccountID const& signingID, Serializer& s)
{
    s.addBitString(signingID);
}

}  // namespace xrpl
