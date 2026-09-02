#include <xrpl/protocol/Sign.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STExchange.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>

#include <optional>

namespace xrpl {

SField const*
signatureField(SignatureRole role)
{
    switch (role)
    {
        case SignatureRole::Counterparty:
            return &sfCounterpartySignature;
        case SignatureRole::Sponsor:
            return &sfSponsorSignature;
        case SignatureRole::Transaction:
            break;
    }
    return nullptr;
}

std::optional<SignatureRole>
signatureRole(SField const& sigField)
{
    if (sigField == sfCounterpartySignature)
        return SignatureRole::Counterparty;
    if (sigField == sfSponsorSignature)
        return SignatureRole::Sponsor;
    return std::nullopt;
}

// Signature validity depends on fixCleanup3_4_0, while checkValidity caches
// its result by transaction ID alone (see kSfSiggood in tx/apply.cpp, held for
// 300 seconds). Only a transaction that carries sfCounterpartySignature or
// sfSponsorSignature is affected, and such a transaction is temDISABLED until
// featureLendingProtocol or featureSponsor activates (Transactor::preflight1),
// which must happen after this amendment. If that order ever changes, the
// cached flags must record which prefixes verified the signature.
HashPrefix
signingPrefix(SignatureRole role, bool multiSigning, Rules const& rules)
{
    // Before fixCleanup3_4_0 every signature on a transaction covered the same
    // bytes, so a signature could be moved from one role to another.
    if (rules.enabled(fixCleanup3_4_0))
    {
        switch (role)
        {
            case SignatureRole::Counterparty:
                return multiSigning ? HashPrefix::CounterpartyTxMultiSign
                                    : HashPrefix::CounterpartyTxSign;
            case SignatureRole::Sponsor:
                return multiSigning ? HashPrefix::SponsorTxMultiSign : HashPrefix::SponsorTxSign;
            case SignatureRole::Transaction:
                break;
        }
    }

    return multiSigning ? HashPrefix::TxMultiSign : HashPrefix::TxSign;
}

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

// Questions regarding buildMultiSigningData:
//
// Why do we include the Signer.Account in the blob to be signed?
//
// Unless you include the Account which is signing in the signing blob,
// you could swap out any Signer.Account for any other, which may also
// be on the SignerList and have a RegularKey matching the
// Signer.SigningPubKey.
//
// That RegularKey may be set to allow some 3rd party to sign transactions
// on the account's behalf, and that RegularKey could be common amongst all
// users of the 3rd party. That's just one example of sharing the same
// RegularKey amongst various accounts and just one vulnerability.
//
//   "When you have something that's easy to do that makes entire classes of
//    attacks clearly and obviously impossible, you need a damn good reason
//    not to do it."  --  David Schwartz
//
// Why would we include the signingFor account in the blob to be signed?
//
// In the current signing scheme, the account that a signer is `signing
// for/on behalf of` is the tx_json.Account.
//
// Later we might support more levels of signing.  Suppose Bob is a signer
// for Alice, and Carol is a signer for Bob, so Carol can sign for Bob who
// signs for Alice.  But suppose Alice has two signers: Bob and Dave.  If
// Carol is a signer for both Bob and Dave, then the signature needs to
// distinguish between Carol signing for Bob and Carol signing for Dave.
//
// So, if we support multiple levels of signing, then we'll need to
// incorporate the "signing for" accounts into the signing data as well.
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID, HashPrefix prefix)
{
    Serializer s{startMultiSigningData(obj, prefix)};
    finishMultiSigningData(signingID, s);
    return s;
}

Serializer
startMultiSigningData(STObject const& obj, HashPrefix prefix)
{
    Serializer s;
    s.add32(prefix);
    obj.addWithoutSigningFields(s);
    return s;
}

}  // namespace xrpl
