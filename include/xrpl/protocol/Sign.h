#ifndef XRPL_PROTOCOL_SIGN_H_INCLUDED
#define XRPL_PROTOCOL_SIGN_H_INCLUDED

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>

namespace ripple {

/** Sign an STObject

    @param st Object to sign
    @param prefix Prefix to insert before serialized object when hashing
    @param type Signing key type used to derive public key
    @param sk Signing secret key
    @param sigField Field in which to store the signature on the object.
    If not specified the value defaults to `sfSignature`.

    @note If a signature already exists, it is overwritten.
*/
void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField = sfSignature);

/** Returns `true` if STObject contains valid signature

    @param st Signed object
    @param prefix Prefix inserted before serialized object when hashing
    @param pk Public key for verifying signature
    @param sigField Object's field containing the signature.
    If not specified the value defaults to `sfSignature`.
*/
bool
verify(
    STObject const& st,
    HashPrefix const& prefix,
    PublicKey const& pk,
    SF_VL const& sigField = sfSignature);

/** Return a Serializer suitable for computing a multisigning TxnSignature. */
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID);

/** Break the multi-signing hash computation into 2 parts for optimization.

    We can optimize verifying multiple multisignatures by splitting the
    data building into two parts;
     o A large part that is shared by all of the computations.
     o A small part that is unique to each signer in the multisignature.

    The following methods support that optimization:
     1. startMultiSigningData provides the large part which can be shared.
     2. finishMultiSigningData caps the passed in serializer with each
        signer's unique data.
*/
Serializer
startMultiSigningData(STObject const& obj);

inline void
finishMultiSigningData(AccountID const& signingID, Serializer& s)
{
    s.addBitString(signingID);
}

}  // namespace ripple

#endif
