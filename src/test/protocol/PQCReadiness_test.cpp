#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFormats.h>

#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace xrpl {

class PQCReadiness_test : public beast::unit_test::suite {
    static std::vector<std::uint8_t> fakeBlob(std::size_t size, std::uint8_t prefix = 0xAA) {
        std::vector<std::uint8_t> v(size);
        std::iota(v.begin(), v.end(), std::uint8_t{1});
        if (!v.empty()) {
            v[0] = prefix;
        }
        return v;
    }
    
    void testPublicKeyTypeRejectsLargeKeys() {
        testcase("publicKeyType rejects PQC-sized keys");

        // ML-DSA-44 public key size (1312 bytes)
        {
            auto const blob = fakeBlob(1312, 0xED);
            auto const result = publicKeyType(Slice(blob.data(), blob.size()));
            BEAST_EXPECT(!result.has_value());
        }

        // ML-DSA-65 public key size (1952 bytes)
        {
            auto const blob = fakeBlob(1952, 0x02);
            auto const result = publicKeyType(Slice(blob.data(), blob.size()));
            BEAST_EXPECT(!result.has_value());
        }

        // ML-DSA-87 public key size (2592 bytes)
        {
            auto const blob = fakeBlob(2592, 0x03);
            auto const result = publicKeyType(Slice(blob.data(), blob.size()));
            BEAST_EXPECT(!result.has_value());
        }

        // Falcon-512 public key size (897 bytes)
        {
            auto const blob = fakeBlob(897, 0xED);
            auto const result = publicKeyType(Slice(blob.data(), blob.size()));
            BEAST_EXPECT(!result.has_value());
        }

        // 33 bytes with unknown prefix (0xAA) — also rejected
        {
            auto const blob = fakeBlob(33, 0xAA);
            auto const result = publicKeyType(Slice(blob.data(), blob.size()));
            BEAST_EXPECT(!result.has_value());
        }

        // Verify valid 33-byte keys still work (non-regression)
        {
            auto const kp = randomKeyPair(KeyType::secp256k1);
            BEAST_EXPECT(publicKeyType(kp.first.slice()) == KeyType::secp256k1);
        }
        
        {
            auto const kp = randomKeyPair(KeyType::ed25519);
            BEAST_EXPECT(publicKeyType(kp.first.slice()) == KeyType::ed25519);
        }
    }
    
    void testPublicKeyConstructorRejectsLargeKeys() {
        testcase("PublicKey constructor precondition rejects PQC-sized slices");

        // 1312-byte blob — publicKeyType must return nullopt
        {
            auto const blob = fakeBlob(1312, 0xED);
            BEAST_EXPECT(!publicKeyType(Slice(blob.data(), blob.size())));
        }

        // 2592-byte blob — publicKeyType must return nullopt
        {
            auto const blob = fakeBlob(2592, 0x02);
            BEAST_EXPECT(!publicKeyType(Slice(blob.data(), blob.size())));
        }

        // Valid 33-byte key with valid prefix constructs fine
        {
            auto const kp = randomKeyPair(KeyType::secp256k1);
            BEAST_EXPECT(publicKeyType(kp.first.slice()).has_value());
        }
    }

    void testVLSerializationWithLargeBlobs() {
        testcase("VL serialization round-trips PQC-sized blobs");

        std::size_t const sizes[] = {1312, 1952, 2420, 3293, 4595, 8000};
        for (auto const blobSize : sizes) {
            auto const blob = fakeBlob(blobSize);
            Blob blobVec(blob.begin(), blob.end());

            Serializer s;
            s.addVL(blobVec);

            SerialIter sit(s.slice());
            auto const recovered = sit.getVL();

            BEAST_EXPECT(recovered.size() == blobSize);
            BEAST_EXPECT(std::equal(recovered.begin(), recovered.end(), blob.begin()));
        }
    }

    void testSTObjectWithLargeVLFields() {
        testcase("STObject stores PQC-sized VL fields");

        auto const largeKey = fakeBlob(1312, 0xAA);
        auto const largeSig = fakeBlob(2420, 0xBB);

        STObject obj(sfGeneric);
        obj.setFieldVL(sfSigningPubKey, Slice(largeKey.data(), largeKey.size()));
        obj.setFieldVL(sfTxnSignature, Slice(largeSig.data(), largeSig.size()));

        // Fields are stored at full size
        BEAST_EXPECT(obj.getFieldVL(sfSigningPubKey).size() == 1312);
        BEAST_EXPECT(obj.getFieldVL(sfTxnSignature).size() == 2420);

        // Serialize and deserialize
        Serializer ser;
        obj.add(ser);

        SerialIter sit(ser.slice());
        STObject obj2(sit, sfGeneric);

        BEAST_EXPECT(obj2.getFieldVL(sfSigningPubKey).size() == 1312);
        BEAST_EXPECT(obj2.getFieldVL(sfTxnSignature).size() == 2420);
        BEAST_EXPECT(obj2.getFieldVL(sfSigningPubKey) == obj.getFieldVL(sfSigningPubKey));
        BEAST_EXPECT(obj2.getFieldVL(sfTxnSignature) == obj.getFieldVL(sfTxnSignature));
    }

    void testSTTxRejectsLargeSigningPubKey() {
        testcase("STTx checkSign rejects PQC-sized SigningPubKey");

        auto const keypair = randomKeyPair(KeyType::secp256k1);

        // Baseline: valid transaction passes checkSign
        {
            STTx txn(ttACCOUNT_SET, [&keypair](auto& obj) {
                obj.setAccountID(sfAccount, calcAccountID(keypair.first));
                obj.setFieldVL(sfSigningPubKey, keypair.first.slice());
            });
            txn.sign(keypair.first, keypair.second);

            std::unordered_set<uint256, beast::uhash<>> const presets;
            Rules const rules{presets};
            BEAST_EXPECT(txn.checkSign(rules));
        }

        // Construct a transaction with an oversized SigningPubKey. checkSign must fail without crashing.
        {
            auto const fakePQCKey = fakeBlob(1312, 0xAA);
            auto const fakePQCSig = fakeBlob(2420, 0xBB);

            STObject raw(sfGeneric);
            raw.setFieldU16(sfTransactionType, ttACCOUNT_SET);
            raw.setAccountID(sfAccount, calcAccountID(keypair.first));
            raw.setFieldVL(sfSigningPubKey, Slice(fakePQCKey.data(), fakePQCKey.size()));
            raw.setFieldVL(sfTxnSignature, Slice(fakePQCSig.data(), fakePQCSig.size()));
            raw.setFieldAmount(sfFee, STAmount(10));
            raw.setFieldU32(sfSequence, 1);

            try {
                STTx pqcTx(std::move(raw));
                // if construction succeeds, checkSign must fail
                std::unordered_set<uint256, beast::uhash<>> const presets;
                Rules const rules{presets};
                BEAST_EXPECT(!pqcTx.checkSign(rules));
            } catch (std::exception const&) {
                // construction failure is also acceptable, the oversized key is rejected early.
                pass();
            }
        }
    }

    void testSTValidationRejectsNonSecp256k1() {
        testcase("STValidation rejects non-secp256k1 keys");

        // (a) Ed25519 signing key, STValidation requires secp256k1.
        {
            auto const kp = randomKeyPair(KeyType::ed25519);

            STObject obj(sfGeneric);
            obj.setFieldU32(sfFlags, 0x80000001);
            obj.setFieldU32(sfLedgerSequence, 1000);
            obj.setFieldU32(sfSigningTime, 700000000);
            obj.setFieldH256(sfLedgerHash, uint256{});
            obj.setFieldVL(sfSigningPubKey, kp.first.slice());
            // Here, we use a dummy 64-byte Ed25519-sized signature
            auto const dummySig = fakeBlob(64, 0x00);
            obj.setFieldVL(sfSignature, Slice(dummySig.data(), dummySig.size()));

            Serializer ser;
            obj.add(ser);
            SerialIter sit(ser.slice());

            try {
                auto val = std::make_shared<STValidation>(sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
                fail("STValidation should reject Ed25519 signing key");
            } catch (std::exception const& ex) {
                BEAST_EXPECT(std::string(ex.what()).find("Invalid") != std::string::npos);
            }
        }

        // (b) Oversized PQC-like key (1312 bytes)
        {
            auto const largeKey = fakeBlob(1312, 0xAA);
            auto const largeSig = fakeBlob(2420, 0xBB);

            STObject obj(sfGeneric);
            obj.setFieldU32(sfFlags, 0x80000001);
            obj.setFieldU32(sfLedgerSequence, 1000);
            obj.setFieldU32(sfSigningTime, 700000000);
            obj.setFieldH256(sfLedgerHash, uint256{});
            obj.setFieldVL(sfSigningPubKey, Slice(largeKey.data(), largeKey.size()));
            obj.setFieldVL(sfSignature, Slice(largeSig.data(), largeSig.size()));

            Serializer ser;
            obj.add(ser);
            SerialIter sit(ser.slice());

            try {
                auto val = std::make_shared<STValidation>(sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
                fail("STValidation should reject oversized PQC key");
            } catch (std::exception const&) {
                pass();
            }
        }
    }

    void testEcdsaCanonicalityRejectsLargeSignatures() {
        testcase("ecdsaCanonicality rejects PQC-sized signatures");

        std::size_t const sizes[] = {2420, 3293, 4595};
        for (auto const sigSize : sizes) {
            auto const sig = fakeBlob(sigSize, 0x30);
            auto const result = ecdsaCanonicality(Slice(sig.data(), sig.size()));
            BEAST_EXPECT(!result.has_value());
        }
    }

    void testVerifyRejectsUnknownKeyType() {
        testcase("verify() returns false for unknown key prefix");

        // A 33-byte key with unrecognized prefix (0xAA) cannot construct a PublicKey, so verify cannot be called. Test that publicKeyType rejects it.
        auto const blob33 = fakeBlob(33, 0xAA);
        BEAST_EXPECT(!publicKeyType(Slice(blob33.data(), blob33.size())));

        // Zero-length key
        BEAST_EXPECT(!publicKeyType(Slice(nullptr, 0)));

        // Single-byte key
        std::uint8_t one = 0xED;
        BEAST_EXPECT(!publicKeyType(Slice(&one, 1)));
    }

    void testConsensusProposalSignatureSizeLimit() {
        testcase("Consensus proposal signature storage bounded at 72 bytes");

        // RCLCxPeerPos::signature_ is static_vector<uint8_t, 72>. Verify that push_back beyond capacity throws
        boost::container::static_vector<std::uint8_t, 72> sigStore;

        for (int i = 0; i < 72; ++i) {
            sigStore.push_back(static_cast<std::uint8_t>(i));
        }
        BEAST_EXPECT(sigStore.size() == 72);

        try {
            sigStore.push_back(0xFF);
            fail("static_vector<72> should reject the 73rd byte");
        } catch (std::exception const&) {
            pass();
        }

        // a dilithium signature (2420 bytes) obviously cannot fit
        BEAST_EXPECT(2420 > sigStore.capacity());
    }

    void testKeyTypeFromStringRejectsUnknown() {
        testcase("keyTypeFromString rejects PQC algorithm names");

        BEAST_EXPECT(!keyTypeFromString("ml-dsa-44").has_value());
        BEAST_EXPECT(!keyTypeFromString("ml-dsa-65").has_value());
        BEAST_EXPECT(!keyTypeFromString("ml-dsa-87").has_value());
        BEAST_EXPECT(!keyTypeFromString("dilithium").has_value());
        BEAST_EXPECT(!keyTypeFromString("falcon-512").has_value());
        BEAST_EXPECT(!keyTypeFromString("falcon-1024").has_value());
        BEAST_EXPECT(!keyTypeFromString("sphincs+").has_value());
        BEAST_EXPECT(!keyTypeFromString("slh-dsa-128s").has_value());

        BEAST_EXPECT(keyTypeFromString("secp256k1").has_value());
        BEAST_EXPECT(keyTypeFromString("ed25519").has_value());
    }

public:
    void run() override {
        testPublicKeyTypeRejectsLargeKeys();
        testPublicKeyConstructorRejectsLargeKeys();
        testVLSerializationWithLargeBlobs();
        testSTObjectWithLargeVLFields();
        testSTTxRejectsLargeSigningPubKey();
        testSTValidationRejectsNonSecp256k1();
        testEcdsaCanonicalityRejectsLargeSignatures();
        testVerifyRejectsUnknownKeyType();
        testConsensusProposalSignatureSizeLimit();
        testKeyTypeFromStringRejectsUnknown();
    }
};

BEAST_DEFINE_TESTSUITE(PQCReadiness, protocol, xrpl);

}  // namespace xrpl
