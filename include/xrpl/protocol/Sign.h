/** @file
 *  Signing and verification API for XRPL serialized protocol objects.
 *
 *  Every function here follows the same pipeline: prepend the 4-byte
 *  `HashPrefix` domain-separation constant, serialize the object via
 *  `STObject::addWithoutSigningFields()` (which omits signature-carrying
 *  fields to break the circular dependency), then delegate to the raw
 *  cryptographic primitives in `SecretKey.h` and `PublicKey.h`.
 *
 *  The `HashPrefix` guarantees that a valid signature in one protocol
 *  context (e.g. a single-signed transaction via `HashPrefix::TxSign`)
 *  cannot be replayed as a valid signature in another (e.g. a ledger
 *  validation via `HashPrefix::Validation`), even if both objects happen
 *  to share identical serialized bytes.
 */

#pragma once

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>

namespace xrpl {

/** Sign an STObject and store the resulting signature in the object.
 *
 *  Serializes `st` via `addWithoutSigningFields()` (excluding all
 *  signing-related fields to avoid circularity), prepends `prefix` as a
 *  4-byte domain-separation constant, then computes an asymmetric signature
 *  over the resulting bytes using `type` and `sk`. The produced signature is
 *  written into `st` at `sigField`, overwriting any pre-existing value.
 *
 *  @param st       The object to sign. Modified in place: `sigField` is set
 *      to the computed signature.
 *  @param prefix   Domain-separation prefix prepended to the serialized
 *      payload before hashing. Must match the prefix used by callers of
 *      `verify()` for the same signing context (e.g. `HashPrefix::TxSign`
 *      for single-signed transactions, `HashPrefix::Manifest` for validator
 *      manifests).
 *  @param type     Key algorithm (`secp256k1` or `ed25519`) used to sign.
 *      Must be consistent with the algorithm of `sk`.
 *  @param sk       Secret key used to compute the signature. The key material
 *      is never copied or retained beyond the duration of this call.
 *  @param sigField Field in `st` that receives the signature blob. Defaults
 *      to `sfSignature` for standard single-signed transactions; pass an
 *      alternative field (e.g. `sfMasterSignature`) for other signing
 *      contexts such as validator manifests.
 *
 *  @note Any existing value in `sigField` is unconditionally overwritten.
 */
void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField = sfSignature);

/** Verify that an STObject carries a valid signature.
 *
 *  Reads the signature blob from `sigField`, regenerates the identical
 *  serialized payload used by `sign()` (prefix prepended to
 *  `addWithoutSigningFields()` output), and verifies the blob against `pk`.
 *
 *  @param st       The signed object to verify.
 *  @param prefix   Domain-separation prefix that was prepended during signing.
 *      Must be the same value that was passed to `sign()`.
 *  @param pk       Public key corresponding to the secret key used to sign.
 *  @param sigField Field in `st` from which to read the signature blob.
 *      Defaults to `sfSignature`; pass an alternative field (e.g.
 *      `sfMasterSignature`) to verify other signing contexts.
 *  @return `true` if the signature in `sigField` is cryptographically valid
 *      for the serialized payload and `pk`; `false` if `sigField` is absent
 *      or the signature does not verify.
 */
bool
verify(
    STObject const& st,
    HashPrefix const& prefix,
    PublicKey const& pk,
    SF_VL const& sigField = sfSignature);

/** Build the complete multi-signing payload for a single signer.
 *
 *  Prepends `HashPrefix::TxMultiSign`, serializes `obj` without signing
 *  fields, then appends `signingID` as a raw 160-bit account identifier.
 *  The result is equivalent to calling `startMultiSigningData` followed
 *  immediately by `finishMultiSigningData`.
 *
 *  The `signingID` **must** be incorporated in the payload. Without it an
 *  attacker could substitute one signer slot for another account that shares
 *  the same `RegularKey` — a realistic threat when a custodial service
 *  provides a single signing key across many accounts. Binding the account
 *  identity into the signed data makes each authorization cryptographically
 *  specific to that signer slot.
 *
 *  Use this function for single-signer contexts. For batch multi-sig
 *  verification, prefer `startMultiSigningData` + `finishMultiSigningData`
 *  to avoid redundant serialization of the shared transaction body.
 *
 *  @param obj       The transaction or object being authorized.
 *  @param signingID The `AccountID` of the signer authorizing `obj`.
 *  @return A `Serializer` containing the complete signing payload, ready
 *      for hashing and signing.
 *  @see startMultiSigningData, finishMultiSigningData
 */
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID);

/** Build the shared prefix of a multi-signing payload.
 *
 *  Prepends `HashPrefix::TxMultiSign` and serializes `obj` without signing
 *  fields. The returned `Serializer` is identical for every signer of the
 *  same transaction; pass it to `finishMultiSigningData` once per signer to
 *  append only the small, signer-specific `AccountID` tail. This split avoids
 *  re-serializing the (potentially large) transaction body for each signer
 *  during batch verification.
 *
 *  @param obj The transaction or object being authorized.
 *  @return A `Serializer` holding the shared signing prefix. The returned
 *      value must be completed with `finishMultiSigningData` before use.
 *  @see finishMultiSigningData, buildMultiSigningData
 */
Serializer
startMultiSigningData(STObject const& obj);

/** Append the per-signer suffix to a multi-signing payload in place.
 *
 *  Writes `signingID` as a raw 160-bit bit-string onto the end of `s`,
 *  completing the payload started by `startMultiSigningData`. After this
 *  call, `s.slice()` is ready to be passed to the cryptographic sign or
 *  verify functions.
 *
 *  @param signingID The `AccountID` of the signer being authorized.
 *  @param s         The in-progress `Serializer` returned by
 *      `startMultiSigningData`. Modified in place.
 *  @see startMultiSigningData, buildMultiSigningData
 */
inline void
finishMultiSigningData(AccountID const& signingID, Serializer& s)
{
    s.addBitString(signingID);
}

}  // namespace xrpl
