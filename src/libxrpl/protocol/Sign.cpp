/** @file
 *  Protocol-level signing and verification for XRPL serialized objects.
 *
 *  Every function here follows the same three-step composition: prepend the
 *  domain-separation `HashPrefix`, serialize the object via
 *  `addWithoutSigningFields()` (which excludes signature-carrying fields to
 *  break circularity), then delegate to the raw cryptographic primitives in
 *  `SecretKey.h`.  The `HashPrefix` guarantees that a valid signature in one
 *  protocol context (e.g., a single-signed transaction) cannot be replayed as
 *  a valid signature in another (e.g., a ledger validation).
 */

#include <xrpl/protocol/Sign.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STExchange.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField)
{
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    set(st, sigField, sign(type, sk, ss.slice()));
}

bool
verify(STObject const& st, HashPrefix const& prefix, PublicKey const& pk, SF_VL const& sigField)
{
    auto const sig = get(st, sigField);
    if (!sig)
        return false;
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    return verify(pk, Slice(ss.data(), ss.size()), Slice(sig->data(), sig->size()));
}

/** Build the full multi-signing payload for a single signer.
 *
 *  Serializes `obj` under `HashPrefix::TxMultiSign` (excluding signature
 *  fields), then appends `signingID` as a raw 160-bit bit-string.  The
 *  result is equivalent to calling `startMultiSigningData` followed by
 *  `finishMultiSigningData`.
 *
 *  The `signingID` (the signer's `AccountID`) **must** be included in the
 *  blob.  Without it an attacker could substitute any other `SignerList`
 *  entry whose `RegularKey` happens to match the same third-party key —
 *  a realistic threat when custodial services share a single signing key
 *  across many accounts.  Including the account ID makes each authorization
 *  cryptographically specific to that signer slot.
 *
 *  @note If XRPL ever adds nested multi-signing (Carol signs for Bob who
 *      signs for Alice), each intermediate "signing-for" account ID would
 *      also need to be incorporated here.  The current scheme already
 *      carries the transaction's `Account` field implicitly via the
 *      serialized `STObject`, with the signer's own identity appended by
 *      this function.
 *
 *  @param obj       The transaction or object being authorized.
 *  @param signingID The `AccountID` of the signer authorizing `obj`.
 *  @return A `Serializer` containing the complete signing payload ready
 *      for hashing and signing.
 *  @see startMultiSigningData, finishMultiSigningData
 */
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID)
{
    Serializer s{startMultiSigningData(obj)};
    finishMultiSigningData(signingID, s);
    return s;
}

/** Build the shared prefix of a multi-signing payload.
 *
 *  Serializes `obj` under `HashPrefix::TxMultiSign`, omitting all signing
 *  fields.  The returned `Serializer` is identical for every signer of the
 *  same transaction; callers pass it to `finishMultiSigningData` once per
 *  signer to append only the small, signer-specific tail.  This split
 *  avoids re-serializing the (potentially large) transaction body for each
 *  signer during batch verification.
 *
 *  @param obj The transaction or object being authorized.
 *  @return A `Serializer` containing the shared signing prefix; must be
 *      completed with `finishMultiSigningData` before use.
 *  @see finishMultiSigningData, buildMultiSigningData
 */
Serializer
startMultiSigningData(STObject const& obj)
{
    Serializer s;
    s.add32(HashPrefix::TxMultiSign);
    obj.addWithoutSigningFields(s);
    return s;
}

}  // namespace xrpl
