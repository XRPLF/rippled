//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 XRPL-Labs.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/digest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_set>

namespace xrpl {

class PassKey_test : public beast::unit_test::Suite
{
    void
    testP256KeyTypeDetection()
    {
        testcase("P256 key type requires 0xF6 prefix");

        using namespace test::jtx;

        // Valid P-256 key from the test framework
        Account const p256acct{"p256acct", KeyType::P256};
        auto const keyType = publicKeyType(p256acct.pk());
        BEAST_EXPECT(keyType == KeyType::P256);

        // A 65-byte buffer with 0x04 prefix (standard uncompressed EC)
        // must NOT be accepted as P-256 on XRPL
        std::array<uint8_t, 65> badKey{};
        badKey[0] = 0x04;
        auto const badType = publicKeyType(makeSlice(badKey));
        BEAST_EXPECT(!badType.has_value());

        // A 65-byte buffer with 0xF6 prefix should be accepted
        std::array<uint8_t, 65> goodKey{};
        goodKey[0] = 0xF6;
        auto const goodType = publicKeyType(makeSlice(goodKey));
        BEAST_EXPECT(goodType == KeyType::P256);

        // Wrong size keys should not be detected as P-256
        std::array<uint8_t, 33> shortKey{};
        shortKey[0] = 0xF6;
        auto const shortType = publicKeyType(makeSlice(shortKey));
        BEAST_EXPECT(!shortType.has_value() || *shortType != KeyType::P256);

        std::array<uint8_t, 66> longKey{};
        longKey[0] = 0xF6;
        auto const longType = publicKeyType(makeSlice(longKey));
        BEAST_EXPECT(!longType.has_value());

        // The key type names itself, and keys of different lengths never compare equal.
        BEAST_EXPECT(std::string(to_string(KeyType::P256)) == "p256");
        BEAST_EXPECT(std::string(to_string(static_cast<KeyType>(3))) == "INVALID");
        Account const secp{"secp"};
        BEAST_EXPECT(p256acct.pk() != secp.pk());
        BEAST_EXPECT(p256acct.pk() == p256acct.pk());
    }

    void
    testP256SingleSign(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("P256 single sign");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};
        env.fund(XRP(1000), alice, bob);
        env.close();

        env(pay(alice, bob, XRP(100)));
        env.close();

        // Verify the payment went through
        BEAST_EXPECT(env.balance(bob) == XRP(1100));
    }

    void
    testP256WithOtherKeyTypes(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("P256 alongside other key types");

        Env env{*this, envconfig(), features};
        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};  // secp256k1
        Account const carol{"carol", KeyType::Ed25519};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // All key types should work for payments
        env(pay(alice, bob, XRP(10)));
        env(pay(bob, carol, XRP(10)));
        env(pay(carol, alice, XRP(10)));
        env.close();
    }

    // Encode bytes as unpadded base64url, the form WebAuthn puts in clientDataJSON.
    static std::string
    toBase64url(Slice const& data)
    {
        auto b64 = base64Encode(data.data(), data.size());
        for (auto& c : b64)
        {
            if (c == '+')
            {
                c = '-';
            }
            else if (c == '/')
            {
                c = '_';
            }
        }
        while (!b64.empty() && b64.back() == '=')
            b64.pop_back();
        return b64;
    }

    // An unsigned payment from `from` whose SigningPubKey is `from`'s P-256 key.
    static STTx
    makePayment(test::jtx::Account const& from, test::jtx::Account const& to)
    {
        return STTx(ttPAYMENT, [&](STObject& obj) {
            obj.setAccountID(sfAccount, from.id());
            obj.setAccountID(sfDestination, to.id());
            obj.setFieldAmount(sfAmount, STAmount(XRPAmount(1'000)));
            obj.setFieldAmount(sfFee, STAmount(XRPAmount(10)));
            obj.setFieldU32(sfSequence, 1);
            obj.setFieldVL(sfSigningPubKey, from.pk().slice());
        });
    }

    // The bytes a single signature on `tx` covers.
    static Blob
    signingData(STTx const& tx)
    {
        Serializer s;
        s.add32(HashPrefix::TxSign);
        tx.addWithoutSigningFields(s);
        return s.getData();
    }

    // Attach a PasskeySignature whose ECDSA signature covers authData || SHA-256(clientDataJSON).
    static void
    attachPasskeySignature(
        STTx& tx,
        test::jtx::Account const& signer,
        std::string const& clientDataJSON,
        Blob const& authData)
    {
        Blob message(authData);
        auto const cdjHash = sha256(makeSlice(clientDataJSON));
        message.insert(message.end(), cdjHash.begin(), cdjHash.end());
        auto const sig = sign(signer.pk(), signer.sk(), makeSlice(message));

        STObject passkeySig(sfPasskeySignature);
        passkeySig.setFieldVL(sfPasskeyID, Blob{0xDE, 0xAD, 0xBE, 0xEF});
        passkeySig.setFieldVL(sfAuthenticatorData, authData);
        passkeySig.setFieldVL(sfClientDataJSON, makeSlice(clientDataJSON));
        passkeySig.setFieldVL(sfSignature, sig);
        tx.setFieldObject(sfPasskeySignature, passkeySig);
    }

    static std::string
    clientData(std::string const& type, std::string const& challenge)
    {
        return R"({"type":")" + type + R"(","challenge":")" + challenge +
            R"(","origin":"https://example.com"})";
    }

    void
    testPasskeySignature()
    {
        testcase("WebAuthn passkey signature");

        using namespace test::jtx;

        Account const alice{"alice", KeyType::P256};
        Account const bob{"bob"};
        // Rules keeps a reference to its presets, so the set outlives it.
        std::unordered_set<UInt256, beast::Uhash<>> const presets;
        Rules const rules{presets};
        Blob const authData(37, 0x05);

        auto const check = [&](std::string const& cdj) {
            auto tx = makePayment(alice, bob);
            attachPasskeySignature(tx, alice, cdj, authData);
            return tx.checkSign(rules);
        };
        auto const challenge = [&] {
            auto const tx = makePayment(alice, bob);
            return toBase64url(makeSlice(signingData(tx)));
        }();

        // A challenge equal to the signing data, with type webauthn.get, verifies.
        BEAST_EXPECT(check(clientData("webauthn.get", challenge)).has_value());

        // getSignature reports the passkey's signature, not TxnSignature.
        {
            auto tx = makePayment(alice, bob);
            attachPasskeySignature(tx, alice, clientData("webauthn.get", challenge), authData);
            auto const& passkeySig = tx.getFieldObject(sfPasskeySignature);
            BEAST_EXPECT(STTx::getSignature(tx) == passkeySig.getFieldVL(sfSignature));
        }

        // Without a PasskeySignature a P-256 key's signature is its TxnSignature.
        {
            auto tx = makePayment(alice, bob);
            Blob const txnSignature{0x01, 0x02, 0x03};
            tx.setFieldVL(sfTxnSignature, txnSignature);
            BEAST_EXPECT(STTx::getSignature(tx) == txnSignature);
        }

        // A challenge over other bytes is refused, whatever its length.
        BEAST_EXPECT(!check(clientData("webauthn.get", toBase64url(makeSlice(authData)))));
        {
            auto other = signingData(makePayment(alice, bob));
            other.back() ^= 0x01;
            BEAST_EXPECT(!check(clientData("webauthn.get", toBase64url(makeSlice(other)))));
        }

        // A registration assertion is not a signing assertion.
        BEAST_EXPECT(!check(clientData("webauthn.create", challenge)));

        // clientDataJSON must be a JSON object with a string challenge.
        BEAST_EXPECT(!check("not json"));
        BEAST_EXPECT(!check(R"(["webauthn.get"])"));
        BEAST_EXPECT(!check(R"({"challenge":")" + challenge + R"("})"));
        BEAST_EXPECT(!check(R"({"type":"webauthn.get"})"));
        // A type that is not a string makes the JSON reader throw; that is refused too.
        BEAST_EXPECT(!check(R"({"type":{},"challenge":")" + challenge + R"("})"));
        BEAST_EXPECT(!check(R"({"type":"webauthn.get","challenge":7})"));

        // A signature over other authenticator data is refused.
        {
            auto tx = makePayment(alice, bob);
            attachPasskeySignature(tx, alice, clientData("webauthn.get", challenge), authData);
            auto passkeySig = tx.getFieldObject(sfPasskeySignature);
            passkeySig.setFieldVL(sfAuthenticatorData, Blob(37, 0x06));
            tx.setFieldObject(sfPasskeySignature, passkeySig);
            BEAST_EXPECT(!tx.checkSign(rules));
        }

        // A SigningPubKey of no known key type is refused before any signature is read.
        {
            auto tx = makePayment(alice, bob);
            tx.setFieldVL(sfSigningPubKey, Blob(33, 0x00));
            BEAST_EXPECT(!tx.checkSign(rules));
        }

        // A PasskeySignature missing its signature fails closed.
        {
            auto tx = makePayment(alice, bob);
            STObject passkeySig(sfPasskeySignature);
            passkeySig.setFieldVL(sfAuthenticatorData, authData);
            passkeySig.setFieldVL(
                sfClientDataJSON, makeSlice(clientData("webauthn.get", challenge)));
            tx.setFieldObject(sfPasskeySignature, passkeySig);
            BEAST_EXPECT(!tx.checkSign(rules));
            BEAST_EXPECT(STTx::getSignature(tx).empty());
        }
    }

    // DER-encode (r, s) as INTEGERs; a pad adds a leading zero byte to that component.
    static Blob
    derSignature(Blob const& r, Blob const& s, bool padR, bool padS)
    {
        Blob body;
        auto const addInt = [&body](Blob const& v, bool pad) {
            body.push_back(0x02);
            body.push_back(static_cast<std::uint8_t>(v.size() + (pad ? 1 : 0)));
            if (pad)
                body.push_back(0x00);
            body.insert(body.end(), v.begin(), v.end());
        };
        addInt(r, padR);
        addInt(s, padS);
        Blob der{0x30, static_cast<std::uint8_t>(body.size())};
        der.insert(der.end(), body.begin(), body.end());
        return der;
    }

    // The 32-byte big-endian value of the DER INTEGER at `offset`; advances past it.
    static Blob
    readInteger(Blob const& der, std::size_t& offset)
    {
        std::size_t const len = der[offset + 1];
        Blob v(der.begin() + offset + 2, der.begin() + offset + 2 + len);
        offset += 2 + len;
        while (v.size() > 32 && v.front() == 0)
            v.erase(v.begin());
        v.insert(v.begin(), 32 - v.size(), 0);
        return v;
    }

    void
    testP256Verify()
    {
        testcase("P256 signature parsing and verification");

        using namespace test::jtx;

        Account const alice{"alice", KeyType::P256};
        std::string const message = "passkey message";
        auto const m = makeSlice(message);
        auto const good = sign(alice.pk(), alice.sk(), m);
        Blob const der(good.data(), good.data() + good.size());
        BEAST_EXPECT(verify(alice.pk(), m, makeSlice(der)));
        BEAST_EXPECT(!verify(alice.pk(), makeSlice(std::string("other")), makeSlice(der)));

        std::size_t offset = 2;
        auto const r = readInteger(der, offset);
        auto const s = readInteger(der, offset);

        // Components with and without a leading zero byte decode to the same values.
        BEAST_EXPECT(verify(alice.pk(), m, makeSlice(derSignature(r, s, false, false))));
        BEAST_EXPECT(verify(alice.pk(), m, makeSlice(derSignature(r, s, true, true))));

        auto const mutated = [&](std::size_t index, std::uint8_t value) {
            auto copy = derSignature(r, s, false, false);
            copy[index] = value;
            return copy;
        };
        // Layout: 30 len 02 20 <r:32> 02 20 <s:32>.
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(mutated(0, 0x31))));
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(mutated(2, 0x03))));
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(mutated(3, 0xFF))));
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(mutated(36, 0x03))));
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(mutated(37, 0xFF))));

        // An INTEGER longer than 32 bytes without a leading zero keeps its first 32 bytes.
        {
            Blob longR{0x01};
            longR.insert(longR.end(), r.begin(), r.end());
            Blob der33{0x02, static_cast<std::uint8_t>(longR.size())};
            der33.insert(der33.end(), longR.begin(), longR.end());
            der33.push_back(0x02);
            der33.push_back(static_cast<std::uint8_t>(s.size()));
            der33.insert(der33.end(), s.begin(), s.end());
            der33.insert(der33.begin(), {0x30, static_cast<std::uint8_t>(der33.size())});
            BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(der33)));
        }

        // Too short to hold two INTEGERs.
        Blob const tooShort{0x30, 0x05, 0x02, 0x01, 0x01, 0x02, 0x00};
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(tooShort)));

        // Ends at the S tag, with no S length byte to read.
        Blob const noSLength{0x30, 0x06, 0x02, 0x03, 0x01, 0x01, 0x01, 0x02};
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(noSLength)));

        // R's length reaches the end of the buffer, leaving no room for S.
        Blob const rToEnd{0x30, 0x06, 0x02, 0x04, 0x01, 0x01, 0x01, 0x01};
        BEAST_EXPECT(!verify(alice.pk(), m, makeSlice(rToEnd)));

        // A 0xF6 key whose coordinates are not a point on the curve verifies nothing.
        std::array<std::uint8_t, 65> offCurve{};
        offCurve[0] = 0xF6;
        offCurve[1] = 0x01;
        BEAST_EXPECT(!verify(PublicKey(makeSlice(offCurve)), m, makeSlice(der)));
    }

    // `jv` from `signer`'s P-256 key with a WebAuthn PasskeySignature and no TxnSignature.
    static json::Value
    passkeySigned(test::jtx::Env& env, test::jtx::Account const& signer, json::Value jv)
    {
        using namespace test::jtx;
        jv[sfSigningPubKey.jsonName] = strHex(signer.pk().slice());
        auto const jt = env.jt(jv, Sig(kNone));
        auto const data = signingData(*jt.stx);
        Blob const authData(37, 0x05);
        auto const cdj = clientData("webauthn.get", toBase64url(makeSlice(data)));
        Blob message(authData);
        auto const cdjHash = sha256(makeSlice(cdj));
        message.insert(message.end(), cdjHash.begin(), cdjHash.end());
        auto const sig = sign(signer.pk(), signer.sk(), makeSlice(message));

        json::Value out = jt.jv;
        auto& passkeySig = out[sfPasskeySignature.jsonName];
        passkeySig[sfPasskeyID.jsonName] = "DEADBEEF";
        passkeySig[sfAuthenticatorData.jsonName] = strHex(authData);
        passkeySig[sfClientDataJSON.jsonName] = strHex(cdj);
        passkeySig[sfSignature.jsonName] = strHex(sig);
        return out;
    }

    // A PasskeySignature object whose contents no verifier would accept.
    static json::Value
    junkPasskeySignature()
    {
        json::Value passkeySig;
        passkeySig[sfPasskeyID.jsonName] = "DEADBEEF";
        passkeySig[sfAuthenticatorData.jsonName] = "00";
        passkeySig[sfClientDataJSON.jsonName] = "00";
        passkeySig[sfSignature.jsonName] = "00";
        return passkeySig;
    }

    void
    testAmendmentGate(FeatureBitset all)
    {
        using namespace test::jtx;

        for (bool const passkey : {false, true})
        {
            testcase(
                std::string("P256 signing keys need the Passkey amendment: ") +
                (passkey ? "enabled" : "disabled"));

            Env env{*this, envconfig(), passkey ? all : all - featurePasskey};
            Account const alice{"alice", KeyType::P256};
            Account const dave{"dave", KeyType::P256};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10'000), noripple(alice, dave));
            env.fund(XRP(10'000), bob, carol);
            env.close();
            auto const baseFee = env.current()->fees().base;
            TER const gated = passkey ? TER{tesSUCCESS} : TER{temDISABLED};

            // Single-signed by a P-256 key.
            env(pay(alice, bob, XRP(10)), Ter(gated));
            env.close();

            // Multi-signed with a P-256 signer.
            env(signers(carol, 1, {{dave, 1}}));
            env.close();
            env(noop(carol), Msig(dave), Fee(2 * baseFee), Ter(gated));
            env.close();

            // Fee sponsored and co-signed by a P-256 sponsor.
            env(noop(bob),
                Fee(XRP(1)),
                sponsor::As(dave, spfSponsorFee),
                Sig(sfSponsorSignature, dave),
                Ter(gated));
            env.close();

            // A batch co-signed by a P-256 batch signer.
            {
                auto const seq = env.seq(bob);
                env(batch::outer(bob, seq, batch::calcBatchFee(env, 1, 2), tfAllOrNothing),
                    batch::Inner(pay(bob, carol, XRP(1)), seq + 1),
                    batch::Inner(pay(dave, bob, XRP(1)), env.seq(dave)),
                    batch::Sig(dave),
                    Ter(gated));
                env.close();
            }

            // A LoanSet co-signed by a P-256 counterparty. The broker does not exist, so a
            // secp256k1 counterparty passes preflight and the transaction fails later.
            {
                env(loan::set(bob, UInt256{1}, 1'000),
                    Sig(sfCounterpartySignature, carol),
                    Fee(XRP(1)),
                    Ter(std::ignore));
                BEAST_EXPECT(env.ter() != temDISABLED);
                env(loan::set(bob, UInt256{1}, 1'000),
                    Sig(sfCounterpartySignature, dave),
                    Fee(XRP(1)),
                    Ter(std::ignore));
                BEAST_EXPECT((env.ter() == temDISABLED) != passkey);
                env.close();
            }

            // Signed only by a WebAuthn PasskeySignature.
            env(passkeySigned(env, alice, pay(alice, bob, XRP(10))), Sig(kNone), Ter(gated));
            env.close();

            // A PasskeySignature is the signature of a P-256 key alone: never beside a
            // TxnSignature, never on another key type.
            TER const malformed = passkey ? TER{temMALFORMED} : TER{temDISABLED};
            {
                auto jv = passkeySigned(env, alice, pay(alice, bob, XRP(10)));
                jv[sfTxnSignature.jsonName] = "DEADBEEF";
                env(jv, Sig(kNone), Ter(malformed));
            }
            {
                auto jv = noop(bob);
                jv[sfPasskeySignature.jsonName] = junkPasskeySignature();
                env(jv, Ter(malformed));
            }
            env.close();
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testP256SingleSign(features);
        testP256WithOtherKeyTypes(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();

        // Protocol-level tests (no env needed)
        testP256KeyTypeDetection();
        testPasskeySignature();
        testP256Verify();

        // Integration tests with env
        testWithFeats(sa);
        testAmendmentGate(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PassKey, protocol, xrpl);

}  // namespace xrpl
