#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace xrpl {

class Seed_test : public beast::unit_test::Suite
{
    static bool
    equal(Seed const& lhs, Seed const& rhs)
    {
        return std::equal(lhs.data(), lhs.data() + lhs.size(), rhs.data(), rhs.data() + rhs.size());
    }

public:
    void
    testConstruction()
    {
        testcase("construction");

        {
            std::uint8_t src[16];

            for (std::uint8_t i = 0; i < 64; i++)
            {
                beast::rngfill(src, sizeof(src), defaultPrng());
                Seed const seed({src, sizeof(src)});
                BEAST_EXPECT(memcmp(seed.data(), src, sizeof(src)) == 0);
            }
        }

        for (int i = 0; i < 64; i++)
        {
            uint128 src;
            beast::rngfill(src.data(), src.size(), defaultPrng());
            Seed const seed(src);
            BEAST_EXPECT(memcmp(seed.data(), src.data(), src.size()) == 0);
        }
    }

    std::string
    testPassphrase(std::string passphrase)
    {
        auto const seed1 = generateSeed(passphrase);
        auto const seed2 = parseBase58<Seed>(toBase58(seed1));

        BEAST_EXPECT(static_cast<bool>(seed2));
        BEAST_EXPECT(equal(seed1, *seed2));  // NOLINT(bugprone-unchecked-optional-access)
        return toBase58(seed1);
    }

    void
    testPassphrase()
    {
        testcase("generation from passphrase");
        BEAST_EXPECT(testPassphrase("masterpassphrase") == "snoPBrXtMeMyMHUVTgbuqAfg1SUTb");
        BEAST_EXPECT(testPassphrase("Non-Random Passphrase") == "snMKnVku798EnBwUfxeSD8953sLYA");
        BEAST_EXPECT(
            testPassphrase("cookies excitement hand public") == "sspUXGrmjQhq6mgc24jiRuevZiwKT");
    }

    void
    testBase58()
    {
        testcase("base58 operations");

        // Success:
        BEAST_EXPECT(parseBase58<Seed>("snoPBrXtMeMyMHUVTgbuqAfg1SUTb"));
        BEAST_EXPECT(parseBase58<Seed>("snMKnVku798EnBwUfxeSD8953sLYA"));
        BEAST_EXPECT(parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwKT"));

        // Failure:
        BEAST_EXPECT(!parseBase58<Seed>(""));
        BEAST_EXPECT(!parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwK"));
        BEAST_EXPECT(!parseBase58<Seed>("sspUXGrmjQhq6mgc24jiRuevZiwKTT"));
        BEAST_EXPECT(!parseBase58<Seed>("sspOXGrmjQhq6mgc24jiRuevZiwKT"));
        BEAST_EXPECT(!parseBase58<Seed>("ssp/XGrmjQhq6mgc24jiRuevZiwKT"));
    }

    void
    testRandom()
    {
        testcase("random generation");

        for (int i = 0; i < 32; i++)
        {
            auto const seed1 = randomSeed();
            auto const seed2 = parseBase58<Seed>(toBase58(seed1));

            BEAST_EXPECT(static_cast<bool>(seed2));
            BEAST_EXPECT(equal(seed1, *seed2));  // NOLINT(bugprone-unchecked-optional-access)
        }
    }

    void
    testKeypairGenerationAndSigning()
    {
        std::string const message1 = "http://www.xrpl.org";
        std::string const message2 = "https://www.xrpl.org";

        {
            testcase("Node keypair generation & signing (secp256k1)");

            auto const secretKey =
                generateSecretKey(KeyType::Secp256k1, generateSeed("masterpassphrase"));
            auto const publicKey = derivePublicKey(KeyType::Secp256k1, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) == "7E59C17D50F5959C7B158FEC95C8F815BF653DC8");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(!sig.empty());
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::Secp256k1,
                    generateSecretKey(KeyType::Secp256k1, generateSeed("otherpassphrase")));

                BEAST_EXPECT(!verify(otherPublicKey, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(publicKey, makeSlice(message1), sig));
            }
        }

        {
            testcase("Node keypair generation & signing (ed25519)");

            auto const secretKey =
                generateSecretKey(KeyType::Ed25519, generateSeed("masterpassphrase"));
            auto const publicKey = derivePublicKey(KeyType::Ed25519, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) == "AA066C988C712815CC37AF71472B7CBBBD4E2A0A");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(!sig.empty());
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::Ed25519,
                    generateSecretKey(KeyType::Ed25519, generateSeed("otherpassphrase")));

                BEAST_EXPECT(!verify(otherPublicKey, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(publicKey, makeSlice(message1), sig));
            }
        }

        {
            testcase("Node keypair generation & signing (dilithium)");
            // Derived by OpenSSL 3.6 ML-DSA-44 KeyGen from xi = sha512Half(seed), independently of
            // xrpld.

            auto const secretKey =
                generateSecretKey(KeyType::Dilithium, generateSeed("masterpassphrase"));
            auto const publicKey = derivePublicKey(KeyType::Dilithium, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "p9V3Cbimc6b8wDeVhkRseniKe4Z7Y3yBBAXZUsVebH8Hr4tzB1uY5gRJFUC7KF"
                "nTbHtFWghFkmdNMDQZWzvTujhQ5JDX3zLpiGBbwhaSCY24ik1BmwpxQJQtbfsJ"
                "ozoeyofm7tB6ejcgBmcW4r1Qxz4tndNUD962NZpmhw45aPFiVXwGZxEdGNZmUj"
                "gVm8TMoPG6tmBfkAiHimsXwnAeAEWN96BRr9DZU5rxx98eQcBakPY3ekjtgiRt"
                "49vCgXuxnkXpYKuLnNAxrPGUuGGwoNqhcEm4HCNLWKiSBEvRArK5KzanPjynKF"
                "SmCyf8Q89T38HVP8aQCksPaB5WhGN5mm8tE1govXCSEgmRZt5nkKKJT5ZU6TM7"
                "Gdfp1ES8Wxt5VYvWGAMscX7dam99mC9nkNrtmw83a6yZQjXVqWQpcUJs97p6DC"
                "ErtynQ28E5hNGka99drQr9KJpvBKYFbNNg3bPRvMVeXpKovoevn3Msx9sLY84y"
                "DP6pNkjKFaE8qfHQCACRBYGBTS45ZtVLmq5fepuAYQockWqRmu4dXt8ntmJVKd"
                "C9HWn1S24L97yZ8QqUnrfM1owGcuf6rFdeuaD6SJvJjQVaS2mP3igmAL3vZmw5"
                "CqqEih68fM5aZbmiynf24BiPpD3V2oKe38ByChB1FmuuqamwqLbNAC7rrmVDuL"
                "vg5dBMCyPcMGJ3bUV12DqnxZYuuMchxUFFu7fmtqiXBeEhhsT8DZYhfpEpYndL"
                "34pNft2dbUZBmRWwqa3kkuMY7QKe1YeZsumHBUG8osNKBijahUke9K74mkCpZy"
                "WkPfxY5XcGaDzkwZDaCRTYqgRqsU2dJGUkPJXV1Bnc3ptkcUZkNAPuv6egmDcq"
                "uqTz1b46EvRuQ3vqgiyRaGhjxsUnUx51jvjp32Qyvhqm4dcHfLeibDehVpz4W9"
                "mLJ2KmYaZeXNaa92qTzCGK83EdVVahb5WwMouNUeQHy1FvuopoNMwgzPigH2Br"
                "KsmYabd8CoyLfqyD3Am2in9xSQFxy9hnwBZ744FUnFCHc4b9FCFctHUJsiZ6Pt"
                "Drwy2MqqK2KQpcKYd4ZwJbWiuKD9qTXsLtU1r7ha9txyKW2H7KSDXRduhGF5vC"
                "2vnTTCm6xzUyRL6jrgJkw6SqFABUW8e2EHGwjCrLnb942u4e5XHmgBZd3A1aEh"
                "jQcK7821HpNNHRwtTbppnCUxxrcLg9RPGGaQVDpATFjUxsYgnFBxfUTDXhNJ17"
                "QPHDeKae7rX65st2hqaAQa9jy1JdwnBrvDMK9UzQ4n2RR6WGHEhwRhVncSEchk"
                "8z88BvXxioFGdpaaLfWtjHSsSQZ35ueitkBFdPwjkMHbAnnQHRwtv2miyczwU5"
                "6cVkxqyCc5BBK9VGqre1eYp7mayLUM2TUzovoUX8ExSZCacSkg5CEsCELcuUhj"
                "3vkrPUHWdCrGsPtvo5hgPCvUVNkvdydXXLDbt9tNqaZAxZJSEKkULc4jzy9yuS"
                "DxWHurztRAcHtia86ASYCxBjER576m31BBcnsnYSVPPY61D2gPyRnHhk9Sn7p5"
                "pPQY3rwgrWTKSRad1kJ7JsSLg27sjvZp7yVjY4KeEHVi4AjJeSkwmq9PTKZSd6"
                "rgqPj6VAqf99AddbQBkNdWfyxbqTKa91DdqzHjFucHVNEm8brX1P6X6hAwjyzr"
                "TvoejSqJZgVPy16p5k9C86hgxeHbxbfu5nEiTjcfdPur5aDEkiPJqtv7A467RY"
                "mX4LEt4DpoZGHrZksxe1xJQdcA2QAyJbTpPRkcK7fnPfvAZk9pLja9Z45hWVH9"
                "d");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "aDzGtRvt4q5UmeckLAvmzH4kg2t8j169P3QbRhmVQR23k9Vv7PF8smAs1hVXP9"
                "CbipRyqrUzPqiaJrrxLhxydWzMrpawq65TJoU3msXZRb67iNFZxg2owNhrDz6F"
                "TB6xz2vDXi2iW5T5xjfveMiCD5YVf1m19ee4p7ZagiKkutM9iDMbKFGTLSY3ET"
                "pZitJL18j4HSC5bJCCBoHsgPHruxhReiy1CyW8phcshv4emxxS8s7edGrjhPxo"
                "6KFymqBGfAzXWcxWcKcHJWgKirZUw2497oNmb6xRgrkTjG9F8dAShSpgkpDLwZ"
                "WumPPcPe5c9GwdURLpRh4yhrNbnYrR4A8C6Dnu2WYH8K9cxT6DJrgcf4FWAy9e"
                "GAwVXBynBAAfafP1Qot4G8kgdPdFYZb5KaNYRFjRzdmgDBsBN3BAVQKVPQmUqy"
                "7E1A6ub5iZcc67X6g8wCjniFqe8eHaAi21Ek5JtPAQAkFKbG2yywtHNkuPumWC"
                "8Qvx6Qg6CBRA9JLHwvw3PLdA1SKMhfWPopXzQem1CUqkLk25HXTYWRytdeb8HW"
                "g7d1RKpaAoP4HVBayySCCFeWmLxc5h8PpH8sUvHT7b4g5x1CStsg9naFEFbG2V"
                "BG4QZY3bf5HBmLe3S6vrBQZxSqCxYmn3qj1LHNGrefv3zW22ifyYEJEq2fZtwa"
                "nHMWdRsRKvoEhFm9VSHzdGJcpY38DSPh3cWMSFMtZtw7QnKXuesanLkxkE2EoZ"
                "yaMEEg2kdAGmPv5YgQAbMjaiz6TtELMAyWP7SMaJi2sbipYwCRqU4rvG4BP9te"
                "xEHXv7dtskJoi5Vyrh9o5WeX8uui9K2G7py3T9uovuvUPumqQwd6ZL1neZaqdC"
                "mPkTzgvfBV9p4XU2NhPBTSdncxtsHr6Vr7GnM8gRGDpyxdgYmzDzsCy2xTjK15"
                "6H1dxN9Tp6TYaQynjh2bUhLkj7Q5urq4jNFfS3pW7zZyoF7mJKgnGC38JSWGdr"
                "Da1KobNePKT3bZot2AhFdWNdsPXfE3U5AtVtCwdZEY1QAJ7kVwQPxfDdAx9rYn"
                "grQqzb6vjSZhJiUedXNjRy81STfVTD4GG9XzJyq6XqnTaqVEAsgrvmSigsMPGw"
                "87ccwgvzN1Fuf2TfcLXincpQQPz8AU1KLD3BVGr4hJhJnym3tVMebpxe1GKue8"
                "fuzhktK8m2mNukD5V2xjaHZFxoYQZSRtJUepNoFPhHDSPX3Skg5HrKikoUkapo"
                "ojv46RGZu7HukS1mfrf6v82GVQggr2Dp6UtxJNikNjKFQ9sK3HwLjK7zRNi6bh"
                "WeaFovKKH8BRS2HxSUvY7j9qyn8kYM67Cc6CJybGyDPBkYYNuDwehsYDTNRZUq"
                "P5PQFSpkN4WhYsNiSEJzZWpL5SCYwxmARXWZ4KyrdNvoE2Qa759FoynBte7z2A"
                "BwbTHveRA6P6rFoNZkTM97Qk68NdTKkj3ze3tQzyQGPyoQVWp3LuRCspFML81u"
                "XHSTNyjTXkx2jy3UvQXJVXeuAJXWEdprxCxCdMAPriozQvdGT3C2MVMdYTBkhM"
                "BuChaq9ZhdomERaubWt7Q6DPmrUDKHP5faGSB4zYH9aH9yPseXoaLLiccciTLS"
                "jAeWRmVCrFVhyZd3Wjd4ZFCMxhhmaAVrAfBVWSFgbz62ubZTX5Ypnq4jbDpzVw"
                "4Ftt6rH1nNaoWpnswr4LLRcmRTjfeK2rTdZ88dKGajU99MYmzeewVq1RW84hkp"
                "UjtPaVPG1XJXngbqMZF423dzwGJFGiNQ8fG1iWVtFF5WaAa88Eqv1LhgB9FkGP"
                "Y1HBdmdde95C54cUJeUpRnwP5sc6rW5UWcgLqvkbucUoeZHx2VMwvDt8dLnq4p"
                "rUUeDX31YGbogn4ijdCGE7KW5BvKsamGmQSwZ6FpqYLaHEJGjyu9GNM1pXd7tW"
                "XQAC3JytjxCqe9a2Duno4Y3txtYdaTvQLquVfgetUNeoWq54cJAKjn3oYrBUHy"
                "JdobJUZCwejXuVteYz8VUPvRvA8wjx9x7eDQ2Zayav8eguJCU8xzMVn3p2hJzu"
                "g3EV32ZBwe9Tsy4YZ2asRjXnvsUbirt1WwZe6cr15DULsNjzxGiMqGCxXsrP8E"
                "x7DST28iVErLVBTU77rRuaDsegxgtKwyWkwQczx2aDwf8NCtmjkL8dKGEXKfRW"
                "Lka7Fv3nZQ5fmCDgVoSPCMXK4mBHQxzQhXXE1RMrVzj7k1U2ycfJi5cimmC65g"
                "SnQGWQ9zQdqpHEMGMe2BJUJTLAV2GN4j1c8jbdixdCH3G8TkrxvbiKbmmYu514"
                "mdBiMdVa1NXNw1dsyaHZQp1keQxZPYzWkJ9g55Qqud5pNXsckzprscSAA8QrVX"
                "2q2yvWGv4paGqdN8zZX2vRqyufN1nYWgkAUMESckHHTAgDRYgD2Q8bo3dvJgef"
                "wJaUFok3sRWSsNZaXSou9pN7Cm8USd9UFWxmpNpAQubmnBbxcR2JbBwpLYvk24"
                "CV6bG6yeoKcPX4sM3k6zcjLPjVweywjUnHRaMZfg1cf69za9WUGw3aWMVkJQJK"
                "fbcWD8gPcbJdMpEJHvabr8J6cuAuTApQ5BDn2Bt3oTxNpTPRCnqx6Vmo4yuXbu"
                "x1aTaLu4BVYSH3eHCda5oGpPxzQPCMHyRTra92CZg6Smuxz2Wq7Z6TQ9kut5tG"
                "jjTMTKiAZUD1fKXTXro5PrMrTfuT5A5jTcP5z4x6jaaTqNeF91ApSscd7cDZr5"
                "JTRs4K59LZsABpnjEAMJCNckXqWvioMnD56EW8CLt9GXMUCVgyAoKogfgdRvUm"
                "4nNjWEaigyWZdHiokPDoJbeb5gpAvxP6w9zi9ac2yYk53EUeFyBDQoDAQguo64"
                "K9JAjF1EaXyDYnCy7yzrV4ZkmMJk4KcE6VGbjiX79Z65XJkDPzFJxcU7bHBzrA"
                "3GPUjhPbX994KJAVcPpmF4UUReQjhugiKtH3zCkv4obkRPHe7HF7MHYAF4Dyd9"
                "zxPxMaJSdhRXMPiikGzXdKHDUo9DovnCj3PdadpZNtaT43r94dE1QnDiPkaP2A"
                "rBsv9yfu89X5fHJVUtYq7KuuAbqkv4QMHxpi2DBcmDLmNXkAoDAThQzyWkhirZ"
                "1GMtoH3M8KHTC1Bq5iyMmF36icoYkpe2XwoLDMb8Mi71Yz1X11d11RekML3R4C"
                "dkZ5KLLCwmjYiej5akjRZrRmc813e6X9CJLgya5V5W8wcPsS7FJ2zyGDVWymxt"
                "wEVbu9gPcNEeRQR53tHjs1VgfU6vSXckT1EKKCvgfnUwFvfw9kenGHaB23ZDMw"
                "kH2ENg8kx3jbbQbP7Ze7f5Y36mVEwKMY7HMqfBZdd3xTuA3ETqTHoLKGxGgF1F"
                "AyurS4C3yorYcbE4HSEku3tnMkP51pU4BeaHr2fDmaoiWucVJbhFm9X1ML8nwm"
                "8VBpwk5SmConttvGEQtpi4ka4LfenZMB941SFy8vYQdRb5xsoJu4LXciNjRVoU"
                "pUc7XDJdCPGmvDPW1bW571mWTDijsG9");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) == "3EDA753241F28CFC45ADC1E40D905A70206D8AAC");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::Ed25519,
                    generateSecretKey(KeyType::Ed25519, generateSeed("otherpassphrase")));

                BEAST_EXPECT(!verify(otherPublicKey, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(publicKey, makeSlice(message1), sig));
            }
        }

        {
            testcase("Account keypair generation & signing (secp256k1)");

            auto const [pk, sk] =
                generateKeyPair(KeyType::Secp256k1, generateSeed("masterpassphrase"));

            BEAST_EXPECT(toBase58(calcAccountID(pk)) == "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(!sig.empty());
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair =
                    generateKeyPair(KeyType::Secp256k1, generateSeed("otherpassphrase"));

                BEAST_EXPECT(!verify(otherKeyPair.first, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(pk, makeSlice(message1), sig));
            }
        }

        {
            testcase("Account keypair generation & signing (ed25519)");

            auto const [pk, sk] =
                generateKeyPair(KeyType::Ed25519, generateSeed("masterpassphrase"));

            BEAST_EXPECT(to_string(calcAccountID(pk)) == "rGWrZyQqhTp9Xu7G5Pkayo7bXjH4k4QYpf");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "aKGheSBjmCsKJVuLNKRAKpZXT6wpk2FCuEZAXJupXgdAxX5THCqR");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "pwDQjwEhbUBmPuEjFpEG75bFhv2obkCB7NxQsfFxM7xGHBMVPu9");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(!sig.empty());
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair =
                    generateKeyPair(KeyType::Ed25519, generateSeed("otherpassphrase"));

                BEAST_EXPECT(!verify(otherKeyPair.first, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(pk, makeSlice(message1), sig));
            }
        }

        {
            testcase("Account keypair generation & signing (dilithium)");
            // Derived by OpenSSL 3.6 ML-DSA-44 KeyGen from xi = sha512Half(seed), independently of
            // xrpld.

            auto const [pk, sk] =
                generateKeyPair(KeyType::Dilithium, generateSeed("masterpassphrase"));

            BEAST_EXPECT(to_string(calcAccountID(pk)) == "rajLbcNJhGJGb8BsGMaaGBzRMvhgja9oZB");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "pRCwJ1ioPYAXLuMP9e55unw3UDbT7U7Qv1raEVLzX7cPgMhepK8TVat9Xxr3oq"
                "fWkE7uS7NPxpHPVRWPoSuUrexKaeStLsbQx75oESF4Br9VCWPPS5JVf56SZGrk"
                "Aavd1AJmQjT2A3q1FrRj47wkhfgBcFaxf3KmpbDoY7Zs2sresNkq4BQuJ5AcSq"
                "L3kbRdQM5dhnYsUepd1pWzEPzJxoZUY7Vxc7kLMAiYXWEGHiVsYz2HvjFbFNSs"
                "15U5g7ymeJrwCEg2k8ww5UnAf4veArtVku8YhkyFxWMpuBjsHaiVECFY64E3Sz"
                "Jd174cbAwrzCzfYcC4Ua83rdoQvSBZ4hbyaPSp7K2XG95JbeErg69kYVbcaquG"
                "n6g3NCFsp8LZ253AkEf3nt7bpJHFPPPRoXA9Ag1WAVg9qT6ZGZvrNmJLHpeKG8"
                "iixHSX3BTaUeGXgaCN5K8WAMAaQ2jP6jf9LLPfRH3ofQVynG2rbpYry4Vcfajj"
                "hvnShVywtVUrZNAhaFXjFGcBSWZyDvx7ssPM65VWkerBzJndxPpp9HD4FDWhJu"
                "GbRntKY1Gvaz1wWovrKB3J1ufCbKTobDFZquxSARCmsgjGwuGjBZCxDB5RiidK"
                "zTfjKaTHd5NS7VecFYAQLUD674FK5D22gvmTgf6C5NWJM7GGKKi1gNJzh3m2ec"
                "ZJMh6QSLfbC1PKoM5Gi4pSDZKm6cJxc6FQikgkF8u7nPeSBXyG86ZpBF2woeDk"
                "AX92P4ouHzzM371zDEd2XfcGkRKrE3JKGqE6LEKsRRxodSjCzfpXhRwn7cyAHK"
                "CnaCiAMyZZqhsR2q5odvaqcEgoKoBUvtG1jdAwYhvGQ8GjNmhqerv2tDYwFc64"
                "ggChoVqmvsjtWFuqm4FpBtfKJNz7iUteC1EiVpVKFuHHZA2DXXuAaaqjt3gzur"
                "mCX4VNsahhjyaZkf3CHLv8UdHshQzwu5XWP1NH6HUksAKC2rFYwY9KxJ89umdZ"
                "fMCoZCDJEQFf8SHxNu1sqDtyHXZPUFcsEjZz6DtYLisHtmp8dJzwkYMLXDiu8G"
                "upaNSQAjXHbszSXvPTEgMHnmPUKsX1hJwXFyHKhXQrn4Mm9jRFyEBNJJs94igU"
                "bcVbHpr8mDXUTZdZWVCTqCkb9hNnFF9rMG9kyaGXKW8Btn5v9ZoyvqVbbQtb6g"
                "t3toQbu2oVyMQWQSNTTkSskwv78ZEvEXgWBncqQonLBsJfiUGJLQWpSbdyUd2V"
                "q5k9hAyKTSeExu3XoxHmreiifR99J1v9dn2X1mC2FN9DsjXkc8mrJGG5AR27kx"
                "ypt6bUQCnqFXFS3pRgcHbhRgkdYXZok1j7Dq93kLYtjzHGiVwzjZLSawzt6VJF"
                "NY8GBK8XCoHACBBuBbi9X9NTpUiNt4K663BdnvmAcQLPwTPzF3AXSXRduADE9r"
                "QuV4umBpemv4zHGB79G86AUhqZoctcvRjQJkVaXt8UMQ5tkYw3PvykLGjLsnef"
                "y28KoCZb9NSM5mNuFhnLm8Gj4pJ82WbLirNYB1YAtqkWLLwEHiVC8ojxXV6ePL"
                "fRNFg8U4Zc8B5Dnau42Dxb2KRAXZPXKbC2PAfLuxUMvcUCDApYJtD61sEryXrH"
                "iDf7hgTFDDY4SttUuN62bSDGTnHQbLc1xF4zFx4AT54DevGMAA2dmCp8oL6CWq"
                "zxHWoSfgDXDwGJJeYRHeoztAyYJ9yDB1Y2t3rHRDzBmWnQUrbhJpXfHLRNdUoy"
                "LRL3Xjf7587TuaaFfzssxYkETLzQjoSxV4KFiKHm86JZtdubwmTyry6ZpeP6RD"
                "g");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "aY7bLawNtwvhEeLU9phzNDi3o5qn4ZroDGXVE6qq4zoFWsAQi4DpApJkGpy6do"
                "uV4bvoV57eStMSYX2j4Ev2tu8jY3kH1Md61Qsn4cx8dDmTMtqDeVV3xMcKXrE9"
                "ihcfD6GLWVTRD9M5iUEdoqA51zZsicdcgVjuUZmkxurayWL9jnGVmnjKFKRbaW"
                "sspap63gdeuXW1e7E2jAHPL75hLEbm5cm1Akd3vYJzGo7GH54TgjWvptzMcNbD"
                "z6YCj8eoXDg99YGBnM2ThPxmVJmp8VbnhYRcNc6wr88e5Vd3hb6TziDriMU1xq"
                "QaStehokWz1VAQU2CxSL9Sx2CGCVcEBE6rkzrYb3JWNAb5cHwhFgPZzYGJJpwA"
                "eX7XwFY2VWsp96kddU3Uu73Ma9aNvGAiqWqAKhzFx2pjV2tbG6RDZ5LTtdd3Ho"
                "feqdi3jPZWsnFJU7NnRztAYSkVsGdJWbgmAjTEYKJ5PyCvBwE82C9WE11rK8YQ"
                "1fhNp6E6CMQHXjDc4osCGVdzKNqmbcmdNA1Srbu7gamrVm8Zv9a6SEseyDWhyt"
                "cA8y5MbiAkJoGRVHd9sL6wvomiC4uaoK7kws7E3HFHe1eT7YYBehav49K4V8jz"
                "5oh6S4Yrh5AoJ1eVf9Jffkmz1nNYnwDEdNudEHgwsdQwDGkBtZweuHgTcrgryN"
                "oUphHahH7yd5Ppp7mRWQ3hRfpoQHoGaPnwqZnUqfqwVjxC8qCrcK42PuzKnpkc"
                "oGmNhSNkcyUKRRqnhhfK4CDPhNKFQBVS4CNKsmCgnxMbcP2CuhLMNqDHAMmzp3"
                "cRHtHaYwRnBARL2UgMcqsS1XgixMjHTKRwy8Ja9SHPMbdtMwk8FQRteFeF2xPK"
                "Ns8DvoSLJduDagKAqhx6TSx8NaWqEKrNiGnvJWtGynvdVQuYsXe2fv5aQvQK6W"
                "B7xgN9qKPt1DmbmaMcnqi9K4WtDB4XfCrAKXgcgnTk3Bzyq6pD7QDeupZXoyw8"
                "NZEdebckmFa9oWPXWXt5pK7k6PARp314yrSw4mhXBuKhKRzRk1TnrCnx4j2dn3"
                "PDqga5MUcdoFLTsrNmaRCL92RxKHo5VH6ke7gxTGE59WRhAmvt9H5n8NSCvwAA"
                "bZc5WiCs2T7Gys5FUDuvKbbqpEngT2A2PqeX96JBF7DmcCw3aXczsxHvDbi8ve"
                "oNp8eBf4DJf2FEAe7W6yaXWiodoc4RgkFkEG6VZq7p4Be4X8defHCmZ19onJWU"
                "Hj8gFR6w4JjzuAcYE3JPddpC3js6Nw73jF2Sv7Jo31oFEz6xFwLqYvFn9oAzAy"
                "Y666mCwue5KMi8eE5Mhr23i6hVqPHhnSjCzoJEovuGdjqSdNQk7JAHndFEvdqy"
                "ZikQfzaoiKghpe9mhpoNxyff7NauRfSAFZ3pTuDzGLNdt76azUYZ7JCUJMjAde"
                "Hg9yGvk14rTvEk3ngoeUCiB6NnFHKySEEb1wkgrzbKZQ4Jn3KbfC6hJzZKQw1E"
                "uWLtY5cXrBd89h46ujPwtWFmjADRvpZGyZX96Qz5w7rxnkNAiZKxS4NkJNcPnD"
                "wZSoHxBKq6CnBxZAWcjFfFvg7JX2UU8LohSQR8FYZexwEUkJHQDswLc6xjS88N"
                "PnV2qh2npDhmzgNgKfSCjFTMubppjj8dZQbZkCe2daowmt1F45EDaN9RVMUnJZ"
                "JnVMgqWLeBFH8KbgYwJvKMgGEZPsUr8rPNNvYLgZMzVEATpKVMCXizKfV5S6S3"
                "cdsuzDgMq9CExjchLZQ9QX3HESYbkZMTcthasc8V6E32fqqiCyGTjBAB6iHih5"
                "KJVBU6VuCz8LJq8MPRBps8dY9TS9Q63fkvtedB158iPYs1VnLgyzzcJczLhWBs"
                "uAi56opTQdU7cp3KgQhEyMtQgUrJSuMDLLrj1CMKKEQNrxp5VPAYyBZYnQpsK7"
                "WQXJyEenNDjbH4b5oXH5gFtqTiRp3yRv3GiL1kYiaLrXA5UrxXzNEHjDwxk1iL"
                "38AefAgcKTNu4XPpnNHUwQsSeGDFovHLfWDPQoTP2UnowVUjdzwYK9kzURRvoJ"
                "ytxtp16C5nacTtm3omy548D1Yg6GWzeaqHSuiymPck68Mu21XBbfRkhE9PuMKr"
                "18bdhxjxF9jvdSsCJrdeSDvkzfjKLt1Q46VYeycxoDML8J9CQVkotA7LhgT9Jh"
                "1E68aNzMVEAb5ub4dhPpxrCkQ4kvUc4tLNfimdvfeCeYLtFDYtJGxSugrdPGuK"
                "v3gXjQWMiqXpk43qjXLFVFpr9EowSLSzHPsnqQsBhEh5QsB7KnnG39a4eGfHYc"
                "uakmge7jhpJoNW3JRST33WeJB9Fyn7Rf9cJQs9ATUZPRPJokx3MYCJ4TyaUcjJ"
                "xRzj9hhASkw3BBWUFQW29Et4THN8gSpRxQp8W8aswxW5moSvxi4uiK7y7Eproj"
                "xSyNdFdZgxw9uwa6NcGYQMs5379UMS2JYS3xEaUndFw3U22PK9wafALJgLGZFd"
                "nA8bvRcE5CUmi1vra4p3cgGtoeYBAUr165FJAe8poygY4QbQZpvsaQMiEhdzWg"
                "zi62aFJhb96kypSAwLcMytmGavpktfzqqonTeCwxhQY7f1of1ChChoWfHNxK8K"
                "bpufS7gqbiiX13potCSo25wU8Vt1GAhcEP1gsmhyTLevkZk8ba5j7n2mHnZQ3v"
                "P5RdjdoBtohu6ZPfoFMoT2DDhDUg7f2M3AJvipkP7XtY6XFJw8iEVjXY3R9whF"
                "ej1VRsz1BkAD4dLVCRZ1L8dGsw2gGMh9j2N9pL8GmmUtVuzoR8QpmMTW67nMGb"
                "EMSruhKiddLLkNQjLfMwUujsjUyGXGqfEJZyxmGomBfEGG8a7qKuMQsBdPVtft"
                "LqetFL6QJsy8j7CSPk6G4nfX3ZNE9VQa2TuyqWKuJiw7AVAfTiYJrcXEJPdUUM"
                "bdcnmfUR4xjkahSYEFtgT3hs1UeQJ5AVRxraEX3pZRWxz1fyPeCU7qPU4nVxsd"
                "4VoG1zaDAHr2qriNiQiZfhhE9XL73EGsc7xLsPnMBMVwZjyFF6eVvPYe9Qp2Na"
                "ghwQZFf8yc3BnKASzUnHGmZsycY4dhESWBuoPKsB3ohG9Vq6B2FndMcbLywzAz"
                "5KpVBufPV63ALKtuwv1yu1LpaV49Zu5CweWPcnsShpWjPW4BDqTVBSH9kXqUAo"
                "acto5igZmYvMgoYJ5U2j6fXui8RoczKmSZojbhP6oJPfDKV1LX7zZwofwLwbju"
                "YoRVdYaWmh7EhqUC1YVyxBRFWbG7QYbuxzT64Zb4qhGxPc4mZVs1baheghoEUe"
                "H3sbdvziiCQUcVt5qAzAMwoa7joSvKoWBFePGE22SnEz7m3W9jJvF7Zp3Yx3HS"
                "UTPcTAorF5fdrxf6ooMQmtXdRe5AD1Ub6K8A5pdXzzSgRVpjyNfxuYHPmmXpTq"
                "2FL24vcaBV66B3a1DZyVrziTqidTNE4G4sKpLXSUwVeXNbnm6jcihak2dPMoS4"
                "mFjLqYXk45KYc4w93hxwEND37wTLbLK");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair =
                    generateKeyPair(KeyType::Ed25519, generateSeed("otherpassphrase"));

                BEAST_EXPECT(!verify(otherKeyPair.first, makeSlice(message1), sig));
            }

            // Correct public key but wrong signature
            {
                // Slightly change the signature:
                if (auto ptr = sig.data())
                    ptr[sig.size() / 2]++;

                BEAST_EXPECT(!verify(pk, makeSlice(message1), sig));
            }
        }
    }

    void
    testSeedParsing()
    {
        testcase("Parsing");

        // account IDs and node and account public and private
        // keys should not be parsable as seeds.

        auto const node1 = randomKeyPair(KeyType::Secp256k1);

        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::NodePublic, node1.first)));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::NodePrivate, node1.second)));

        auto const node2 = randomKeyPair(KeyType::Ed25519);

        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::NodePublic, node2.first)));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::NodePrivate, node2.second)));

        auto const account1 = generateKeyPair(KeyType::Secp256k1, randomSeed());

        BEAST_EXPECT(!parseGenericSeed(toBase58(calcAccountID(account1.first))));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::AccountPublic, account1.first)));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::AccountSecret, account1.second)));

        auto const account2 = generateKeyPair(KeyType::Ed25519, randomSeed());

        BEAST_EXPECT(!parseGenericSeed(toBase58(calcAccountID(account2.first))));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::AccountPublic, account2.first)));
        BEAST_EXPECT(!parseGenericSeed(toBase58(TokenType::AccountSecret, account2.second)));
    }

    void
    run() override
    {
        testConstruction();
        testPassphrase();
        testBase58();
        testRandom();
        testKeypairGenerationAndSigning();
        testSeedParsing();
    }
};

BEAST_DEFINE_TESTSUITE(Seed, protocol, xrpl);

}  // namespace xrpl
