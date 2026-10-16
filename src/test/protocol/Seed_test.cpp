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

            auto const secretKey =
                generateSecretKey(KeyType::Dilithium, generateSeed("masterpassphrase"));
            auto const publicKey = derivePublicKey(KeyType::Dilithium, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "p9pow2SA5t1GpXJCZiWeWiKjZ4xav57jYgDzAhesVequQtwp2UMQ1ezUUE81t7"
                "QY7zyvWqsRCTuxDAyZikit8Qwrr3Gcq5nNdW9DzbqjiY18Ze5ZZCdpNmkcsye6"
                "MajNjTrjbr7VzcH2HksZnRB6gTiy8Ktm3s6jwU9wiDHo3kbZdY1UbV9MZXSweg"
                "abP9s1oFRMSCZ3UQJAPHBVeCyd9LCp4oV3kj3TVSo7VF8D5xzgwFvtiwXxcAae"
                "sKnG5u1NgyYPFNqXFZA48ezBjmsYTt5ZKcKcCShkKa5tN4dME4NCagDa9G8U24"
                "8HShgFkVHCwjpRcyAEPehN9TUowySQZZFNQu8887sQ1M22BJ6rmSkfLAbV6jpm"
                "HinSjURu67SZ1vvNm5rmEEcLvpMwHibwJQkurVD7LYXUJrL2uXUf5AiuZycJEi"
                "1XZcd6sQaNAp64FPecRzWMnLM9eqg2nJQGt9YP7Gv6S2JV5m3AULsebDDZ1hYR"
                "7CUNfAeo3Dj6SoatPJaqED5GfQK2Z451QHQr9FQXsLMJ9bfXhQDJJYTa3kKyo9"
                "1wtaZataHyhDaGpJ3WwuDV1adkfgcrMLbK52mi6Qqznm22WQRxVtp2heuEees9"
                "zGgSnfWhFSQ7QNaQStQ9jdPErqQFmscN6Evq1b283ABuJk22EKMPz1JWJmPXvy"
                "rUKFwqf5u5AXkZVZZ8zEPmoLzsNoFVBDneAt2FbAh5n45rfUQs1BFeFEn6wcfY"
                "mh61t4xdzcSZpcJHHZWqRXBaPFWbgHKqWmxnnwdMZik565WuPmQhD7BpcmbMsx"
                "ffS8QEqPgwKAHNSnrGw7ZZf2nsb6C37ydmVecswmjRVosCTsBniBxLvDzzcGpa"
                "oyyG7RD6X8bAKmSyD9VJFtwqXWj8hi9d6P729cwuthmzUhmTsYYiSCy4aoPM8o"
                "U3pHheiydExXeifbXubwvS3LKAk48dyVCN1LhcwELn8hzUqnZ79whzFR72CPhv"
                "DQgZU9NmG7SBZteV3P5oTEdWeqFwGf9umZQbMAGoaCfjE27zgjzd6GfFsZ6AUn"
                "2xCrEfwoEg4uhB9PC9QsturSWWV88S1YTtR7pbEbGtEVBgTkcNMSk2XijUJBGC"
                "r83964fMtsbuj9DyqJgQSg1za6cHfgJt9pzmVoCNBRoBLGcsWn8CZAcxUGZkhm"
                "k2nwERPsCci56kV2UEfTKpq4PqUYWmxi2fvbvamqCWv6kQgBA3FLWXu8rXtsa9"
                "vUx2fV1jiVogFRewsxZ3YCdmAVNmt89jb2ECQX4rdEqDXwfWJPxRUY2JD3JSZi"
                "jWpHVqMJiHGu6KgeGvCcGmiqBvYom3D9ACQxA9QGTCdQh4AYYYjSwDabA7nB4A"
                "EVw2v5L7Z2uz76BHXMcWCHu1B3q57kVihrNfKYGDLcscw9TebPKTimZEqV2PB3"
                "h5YA9P9VKgnRKmkFr8qMQRrVR2ekGafMB2PVRmrMLkZkYNx9exE1mvkN7VoD4t"
                "QyRzQNsSh3xJq6PdBYX5KTm5ouw7DVxev4PQUNiHtZVExZGfjykgzoRojvD7s7"
                "64BpWNyyRAU4zyMT5UU1BdtBQQzdMxxZWMsH6LHkX7dahTPkGAEtQn1gPrJdLP"
                "uPtryue9ps2wNpVWT8T1Riw5fXgZ4NHfJb5hRwX95vg1WKXYMttHMBL6Zi7QEf"
                "9VSdrt2qTw3MTG3F4A3uxJhyapzv1XBfxdKUTRbEh3h59CTmxLZauPBKE2QW5a"
                "jDUwoZKv9cV6isLfNe49XYSWa4ger9X3Gfga8xxPrmaxnWeST2qjfUkhuqi44F"
                "v");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "aUwuEvQW6qvdeS3kLWCPXHAtutaJFzzfKBVrFDrWNGAYCYkWfw8efA6upnq8q2"
                "srNjpqc19njaxYD3hDr9ynLzGBtEUzb2brTXDuqHBqAGY2waHQgj72dBGu9UnR"
                "xFRYJVZ5ES1Poe1V91xpuzfMv1QcFcV4BpgfFY6DDAEbuxtALntM6xvRraEy7W"
                "5tdesbJTQyeJhBU3W7AXhSugcJBjCsNdbRPQ6cqHDbaA3ieMWXiTPkR8WmGvmi"
                "tXLdBtNCsfayGEQ4PphKwT894ushRgifMHVLdsfUgstcK1Q3LVoPT435o421aZ"
                "GDmprViiXL1rKDU6cgKLiDwhDRcZT2WexvqXA3UT7qYH6CH1wwnGNJpDyDGsBh"
                "wTkQyG6CTwPDapcWL7Ra83rwwKVyWhfc4mvGwzJUu7jC3ShBAn2it2HJyh8mXQ"
                "zqAaVsfRDZRnoVJZMWQXZxPpCzQbtYNPzRys9z3rCXjwKxiEwuUUPvhPpQZDMn"
                "3TjEdCKU6jBN8vAxbzhBfCLKfcKyDEhep2MLrzTtsBfi7u34wCdKsxRL3rog69"
                "8CXPYQZEzSYVzt93873EMR6xoccYFB4QLQxdVkMrGx4JGXn9vFA76i6M6rwTcG"
                "VN68kxXc11156TPsEbxu8yxCNYqTZvCXuoMYXcj4kMg9wvxwMYnvRVWDRHhPpr"
                "bEvhJLmboNJUTpUdSQbSMbyC91perLaC2Nn4fMWzHg3nNnveRzwxd1jBwVApzk"
                "DUsqq3pSGRyzff1pJEgQ8cFRyv2mQ49qtL5YbMGuSZbbLc65qEfTrBJnPAYejH"
                "Uk6Nyvj8SuG5Keov3rGiVNm3zBnm7tNz67WTqReA3Z4dVonudKkLHC9E14dsf1"
                "27Bb7bw2gPkQWc8qn5oe1MqWYzFxxYnwWrtwMGpiGHgtKKRF14y82bibeZYAJE"
                "943juPEN2aAZpFJrPw9RA1WRrrgYyU1sbXrhLmbFT521vKTpQZwPoP42yrpfRM"
                "Usd4an1M2TGvMCqnbdYotcKC3mS6axxkYg3MgBCs2ZzYaU5jP7oUsT1RMeyFHZ"
                "YhuSRMYyG8vhfFF6G7voBgBphzimFSf6fy7oQNd7ZpKQYm6NGgQG9u23ZSxLKG"
                "hjW5RKNLFAYvMvb1W9iqdvg4F1g7UYV3hL71qdJiMNdtyXfrCr16wzECw2FX8h"
                "xtBqz4L8gE9HGi2HhEzdegofzPWHiBFzERE5d7EAoPodZ9q7KThGVjJWB4rF69"
                "g4JFLEM2RtU2zfb4fggXdq2hFrhohCD6zoW8KAEoGKX5aBvcx3Vns7EHfYrBGg"
                "BSTM1knzAytCcEVHj5pSYFdCFD575JxB2gsWoYPXqW6EmqbmUkts8vEd1S7Hia"
                "p2whka34Rjvuc9nbh11sQFtTqi9eewHC2jDJwBXCWQ5KNPJBQEMNn4eQ9gKW4U"
                "Ww3s3VEanEP12eD478mvCJd9zdZE3WM8S67B1jMz2KmJ24p436n6VVz1bpwVyt"
                "mkAXd1xm3nZ5xUHQLFLVFu7a32Lm6y3Kgzoaqng6vLgbQBDx3Qagx5bbspW3T1"
                "EFgvQqnbMUwTgBciBAFTgV9jAUkYaXeqessEcvY1Jm7M8Yt2WZ1k5jZSVt6fKk"
                "bS8M2vAhhBUQ5S3XT7SLUFTo3o4QR3zpFUCGkhqoo8CKVjk3vzAmexwdLR3xKn"
                "rSErnSRznR26gzArS2EdNtedRMaiW5t4tEuZ7UEVDmoX1Ksz7xEFiiRHSztcTW"
                "CYXaquRzhj4qcoZTzPbokjuL5CqADsgMz2XUfpTgNhWz7RvxenQBeBZrbyV6sh"
                "16DhX6FCF39tXm1GihVoQaJ5MMzvewmNZfxUvrmwvZjRS8wo3u3Ebpw5TdkGzT"
                "U1r1E7SXTbF9tRosKK9skuL4E1AtZmA4msvYVcg9UrMJzi5FitRa3j2uqa9nVT"
                "TQ1x4YfPgNyGNQW3YENq2rePZQrv3zHvN86TmfKMFm6SfZotPLvUukqWdGQhBt"
                "Z7ddnozD7uHFTRpoySpNhaHSuNZWednu4exFKVHz6yF6N1YbroUi4XgrjEVFuR"
                "sniWtx2fX464pUh98QyNGQjmMPfHVxt68t3X28JnJuzPwysmj4FHjTr33w7RjS"
                "F7uWRT9aw2pkStfFQsdj2MBKMoiYqp3vMATcCDuuHoJ4VguQXqwGDcrMUMbrBc"
                "AJaeLPHw8qGwe3vgNUUVVR751syoAmpQQhh1uhXsUX3FkCoR8PEgESKGdhABto"
                "NoiMPkQKAXGp6AyCMMfymR39QSjvD2jRyVBe2HFcKnUPK2QoXUD98SVzj7hVM5"
                "h8NeC3VE4CgNyAA8hDvKJvfCVcVbiirHSeCSA28n3GHdLBEzpcv7C5AXWxmCY2"
                "ZhLg5HAqiU4gAWNoYnjHjLub9j8YdRdd582giB4RkTfCjfGpDrSb2d8WFSaB5b"
                "DhU86HukuY8XSG7a273EgH96eXU8ahteckBCsnq9UPqTYv3zCB1rTYbuHoeBjw"
                "wWznmQLgB1MqTQAKUDSrE2sn91oLj5wmqcmqU8Ps9x2r7vBS9RqKjATs8nYWX4"
                "okvh18FNqH9hvPgf5qEM9zPyxqDtgXofR9CVAvzqyzb42v3xHzyNNasEgtL8tc"
                "qCykjDkNmLbvNoCfKHAdJaUJvsxyCPfzKz8BuUQKHidPTw2CmqPJqXLVHPBPnD"
                "ftfNdPVWYUsfCDMuN4xRq1ESHA2t92977PobFR5UWoq9JDDP4Ht6p3Ad9N6N53"
                "AcHtTDa25LjcymjBJTq2csq2qK865nwHwefcZ1z2r2EXVmFDwJtUHpgQgm636u"
                "dKNMKtjJjSLnyuuu1UJMERJuf3DQH6gig9GyPsc4ySMrGtDz7yqkNMH9DK2Ja4"
                "yUgqq6dkU4aGt2WgNaLE3d3zXFWb6gyCTt4UvzqtoK4fZ6qYGZ52kqZxyP8ixy"
                "R5kqwyS5tYqnxDGWMpxvMhWAgNQwYvFLhVPKbcDwnJU8PKdc48ZGDw3QZt8cYC"
                "b9Qjkd5WoRtdawutGNGDJJdM7VgBXDkKQmXkFgKUogFxQNitKdYdWCDmMJcFVQ"
                "4Z8HMeeYGU7oNQtUpeeKQbPkfUsz5xciF6ihUF78WnZWiMBNv6rJikfQnCHqby"
                "JwmMrAcB6aDMWccmQqxNecBQAcf1VEwiYhTkahDuDVfGytsfK9tvFH2Sjsn63k"
                "f4wwEt2wJhQQb88GDughq1HrTQZrqwcJHAge5B4uXr7yY1PAY9dqyNyPGmGkb2"
                "5NCqWtRws75koNS1HniW8vhuvLPaGq96YTg8ifzQgA7Yt6Sua6emHYVUbshncZ"
                "Lisfrmnv6RUMKcmA6zB3R9uPWdH9atGUsbN6Y7en9dMMo3iNYkkDtCzZmoKYH8"
                "9Q8TvyH6bGg7wEQ6cgTmrVTMHme5PntN2Z8qeGr12XKmaDiyKiyCBxm3gVpTD4"
                "n1mFuJLwFVnc6LPdpMtHfURUfhDqyKBmzFY49scJZ92hhSPSX1KQvmWLqbqhSW"
                "CM5E4xwWtMxkQ5vs2Yn5EnLaESjswzm");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) == "8A94F2BC52E94646EC83CD988657FE37A9D5CDB5");

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

            auto const [pk, sk] =
                generateKeyPair(KeyType::Dilithium, generateSeed("masterpassphrase"));

            BEAST_EXPECT(to_string(calcAccountID(pk)) == "rDdkg2HADzqCh6s6CyZ53ExSQ3fRMEycuV");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "pRUFoiSyVkDrDDrZekFKmqdYfDznimyyHb6XzWVPRqgpniKgQeNKenTpvr3Y4V"
                "Sbdx2rSdUzi52f4wnQ9UqctLsuP7VzpmLcj5gSnzZC4rSu4QNmEDtYxUswp3d9"
                "7UiDdbUjWikPHrWmuyi8n1VpmbwHKWmXppKuxNLDLX73cP8MBGTmP5JdcrFrtm"
                "kNMxp9Q81GbyvseaVHPYexjzpHwt6zm2og4hCNegehnBNqcDJcyRyh39cCkKao"
                "Y1MK5iAhEXpXAwc8Wf9bVTouA15pbCmz7GL8SXFNmHD7xJRxbTmSpMAZSuhE6Y"
                "e9dZAeAeJNpcjsmFWUuz8yhuH5s9PE9idKERYrLExV1FXTDhDA6F2ZR5duff2Z"
                "aveYacowDHtQya3gErmq5ccJqZVFu2qWM5gwUrJ1BLD85jWeL25aMW1U6YDvJz"
                "V4cvzo2oHdnS6wQMwN3vH56QEKta19BoGkntYhyUZipx7e1Nhp5NNccSCqR4uB"
                "LB2dAgteUcCvwvkXFwy91xhGa7WqDRBzCVnEDVzaEZY1P4MjN1iguvtPhUXY8R"
                "7drgMYzPUZr5yik6DKRnNS1Ub9aaxfvj8NCQWcUXbSQsFkN5AJYbrkLW7kAjVm"
                "J1xuY9E2L8vFKfywJ4Dy6dkZ7raetT1BEQWKtCa2pP8Hq3nCjJdpjS9VM3CtgN"
                "doxEg1tnd5G3qk658UGPb3hLkushX4q5nNuB9oXtfey3fPNguYSKqcusQqSw6y"
                "2DqN91Bu6Q15PCgaCjrCBjRFCqVxsiybj6SVNDCcUEVD9JXVKCCA3ZijKdvEmJ"
                "ESwLJe7qd7hmKuiMWy3yUqt9LrtJE5fb6tqzr6qC2n8YRenbgBEGVr1jt7CCad"
                "2qwLQKg8BuuvSTRyHCKuvWDQ15YB5aKQdLrQWhEE8uDWP4jPTjovAZBsVunzBC"
                "UTEnNfMyfgBGCK5fqa1i2tYqMMKJgkHVg2uPPCet3Xi37AVJMDnMsd8LQhH2X7"
                "r52FCUFGinGyN7yNNMFGSQ4XVxY2V8611fRfg8M9MgVjXxvdVPwBZXGiWAr927"
                "Gy2GfwG6TPeZprMmRNaZA22LzPbg7iQoHYs2JSGpWzMtDAsUZjt7QPUE5hiKpU"
                "p7YHW5MSgGv3m49FL3s3fCBjQFCwR6oKErtMjNvRRMRzdSxJA3J1FbYvejbEXP"
                "uJMeBN6mUabqFqjfyfZBCR424J4maY8zz4rHsj96Hp4Ya7y5fcx8uMuw3SiMoB"
                "MYRVY3kQqvvEnGtfonjxwNLtnsnJs2t1JS7t9asUnQQr9U3zdACGnkMAYfdKGh"
                "uY3UufNrnKHwtj524Vmr3Yi7W9YUcpKw8PSySty1GDn8Q25dS3Ja2hQuxj8WVp"
                "8RTa9U9LcgpkF51u1Ao8XufqSN5dwmEaY3bocGW46sARCWEUeqYLhEKeQotSEQ"
                "L6JJCxsrL8zYGC3hgNsT5gHMmu6SWjQEPe7ep6CrHiMDJ2QEZkucRh6mUigKf2"
                "wb5pJ1c3mR84CcXkHCwWRHi5vVk8aER3bUr1MpioJriDbpU8MSGA5LfpfyqeQa"
                "8QrKnprCzsXEGmKvjF7ciUpw98h9rZj3gAAVGJth7Zsqtwt4CpFbqxbLw6566z"
                "jvwaJtcrQ9y4jHkfxqRDP8Ezcr3We7k3s8EhxPnB2MZiu7rKJJbXRJgRSpSnmA"
                "gWvVn11DazifjgkSUcGtybuNaAr7t9Eno3ATzyQMQuAUWySX8YTF2bYwjSLEaX"
                "Jf9ynQnBqbQu7f1BJCkKX88fv8ad8yCVJTcVtCvg6PpQsUzHVKxysptENQSTFq"
                "C");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "aWCD62d1DB3V3RiU32obmNRHsppbTgJm9KbueiniUiBNRTBzV9wurUnbRhKyaN"
                "GtmJXCH2Qj89TtTeczsLvgcPh2RM1fdNWGp3hiSidn3tcYCkJh4Y4vRJqDSUcm"
                "M6KgXbuqNnjxWnmVsMnkn7URb9RBZMMf61mezdu1VyvAy21AUAoG9NPH7y9SKZ"
                "jM8qmAZMKGG4uLEJaQVCh8Z8sLcsUPJPPRCBoyjkvYJFN7wavYm9SrqRVPFmQH"
                "BuhK9q5US482uw5pihp7vMhbpUNuscnpApgBEAqArFvoKa1RcTZRP8Ksu62Qtq"
                "9QSXExGxKjbEnzUixcLcc6hdefUXhQJx1GuJimDnEszv9kAgdrbvAEuPQzcaUM"
                "YhhScuHwk5V39cyGYieAQo4FdhxP1mTkuUXChNZb1Qz5QqubyeF3xtJH7HegyN"
                "eNt8dGEnvVqYvJyKtoCijFhe2FCNNjo9aLCrtvmApqBPBZS3q6ogC9nVzryN4F"
                "D9iWxAtU6u9bBMqHoiA6zFLMZQiPkkK17VgmcAbVhnsfdvLDoHGTymVaT26yPX"
                "j3gVqT9rzPTvyFTKRd5v4mzEF3KKrDPL81LdmnBqceeVUrgsp3XiC6xgkZhym8"
                "pmQnd8ApFtYLNzPPvsMrjLw6e8QsaKoe6JPqVg9X91wwP6fmPyHsn7vqTBwWqw"
                "Kw2bxr8TxH3CmDTx5PoqiQaww3J9b9ozW8QZjjzDYxTL8gPxBMkBAf43QZRoAo"
                "s4TFd1gSETuY3cyYCti3oYvxsTS6ZDHLHVZPUmiGCAubaykNh1cb1zbayfA7ik"
                "iv6bLYehFN9qtGt9oJj1Fwz3t33Hhi3secXfgMtZVpFpjmCNyAAswUvz1ef91X"
                "s3DMQ6gooYWbNmvUgjPw14BDw3Tkvq4vLo5rHzN6yw2XiaeF3vQ4eibffpDA3p"
                "dZD8BJAaAJMN5rapr6egbxVp8RVrS59KtDW7bGEPS79af7ox3Sx5kP3qNaKoy3"
                "DWrCAaETxAuAZcQ5XN46HRZ88miz1ydQZohdP8arWvSB7LNQCBq1frjj8SRRFc"
                "EHLGX7UE9zwF9RCs158VRpXjhWh2wAN5Z2bH8DEsGuPT1r9kGW1XADuWLnX3rp"
                "1FWFm7xQwXUJDP12asNu1vEoS5mM5vZPfyrDWhbgfQo4WUVz1h9pdu2vWvMRsA"
                "eUNZ1C34fxVdB4JDsBj1evk9qDmVCBnimxiyawrQD3x4FrNFUQTHwBrcXxMswY"
                "w4B3TE8wt5eg9cL9Eo1DLgpTTKRFHE7FsRUGxw6iw7r6xsbGerCZ1n2Mifa7db"
                "DWhNq7AbJvpcpTqbri9ueNLWudmfTrL9ejLe8FcBmZAoijgk5JB5g9ohNTy4VN"
                "U5XhseUSs6auajzVgCMHFHyNbdbrtJxCykFmEk8e9Wp91HbBujjxj5NQZP2TQu"
                "cCgZfVMXVZ7aFZYDbqx8BgQaWA3urwsJZJ6DjzPrm4vj9EbzS9iTkuGqKzD1yE"
                "9yqxAcqFbWNvMHXuL9U4DtdAADp644C26tM2u3kshAtZhzqdXFmnsb768ovm7j"
                "N77Cbxf43GrmcjntfsCoPc1peWntyTs7LrDD1L41bHLN18Eq9SH6YjKXirHSmY"
                "N2dRzNr7hZ8fjYknEiEd4FEoaLKgsAHeSWnLzHC6Ftku4shVmzexCLAKNZJs3T"
                "vgq7nGeTePJByJTeFjUDDF53ESk8hRa4oi26Duc8VsaeCQXXyCHUDziyRvyE9d"
                "nRgdjdjnpMj2ViZgyPB2b3Pdoin5gkoQqMJQJ3kHa7AhVLgpX8F5WsRX3Cx8qP"
                "e3RhadgAqyDZ3EsRvq7opomi8mFPsMB9oSwW6GtHoov9iaMuuamnCQ2YFd5AoV"
                "fcCAjkLfKxQFF4YWGnYs78uGDtp1y2XKKyWfo2DRC5RSjZTGcJTVqg821xXdvR"
                "SQTcD7m2wWsr1erHf1SktJQLhMtKBVoSpDeKXBDK51hwYSHXjZyfRyRvGDyjkE"
                "PdmCuZfwd6oBvSXBVCWApaQTcZrw5bDHrWxo9jeRBEfq5TSCSzQQLUM1ua1S6F"
                "LiZuoE5choeXSfSkpciuDTSYzpbxwAdA3rr8kwsbDTVqVdteJHZsKvPQkX4PGU"
                "joH6pqaUV7mLb35HDbExf7up6NdBHbqMDt8k6U2HzoVbVkPBwbwjKvwRrWjshB"
                "To6Ab59S5gVebPJ4MT938nd9tCZSjQQtCkC7oHa9MUstL6RhMCSeMwcnivemnS"
                "513cckk6P5xsBhmNjDtcypq6UmsWPgogPERzFszFjeyySq3XFJMFhEzbcBGhpx"
                "UEoZz971ZTTFs2TpW2ntsPtDpM4rYmR1qxFwGpJgRByp7yp3DmGpHQ1sYG2G8M"
                "vekRvqjivvSYHhWDMv5GxwAy6u87NK94vER3fWq5e3wSF6HQVeEaeM7MekoWtN"
                "rDnFWJoUZnSEJUr6Hd2tAbxAfDZmztKkLCMiEAzHRJPFgKfRMuS8zJ8Bd2yzyQ"
                "2ynaRfyHfUxD8voy9Fk9EWy2Nsw16jTT7hBs8tsPaL3i6Lg6UGVU5EJYXPtajk"
                "6pr3kFRuoqYUYP1kbEm33aiYSqJka5mYYmXtpQHCNABcgLYeW2BvbPmZb9Pbnp"
                "Tq8RnMX5B6vrFosdYLH5nYLfjZTTGxtd9Aemo4ojhVqYHmL1iad7c9XaoYihKi"
                "m1nWu6Rk18twETDZXBWWvSfduptatkra955ognVtGEdNwW41PUovnFiXoscqmE"
                "HtkLVAPJZYcuB4yA6xh9HbiaLeiTfc1MgVeWnSfVuUBteV9WufLR52To6cLZmj"
                "WbRd56xJga91aiTqSvR59jPMfC95tp3GmHifU5Gbmh6YQuoS3qzSwxaPSpw4Wt"
                "zup8vDTuPtainyW931sWrKgkupSa7imWL1rbpeeKxbF1yupSLRSZoqtjgWNn9P"
                "uny5DpEYk4RT5xmYz7FFyF4of8dq8khywZfGiT2GEvfzxoTAJ9PzL6xCfch2y3"
                "xgFhHXWJfe7gZ8uG443N8sQPRD1RqYv6J3aTmSMzkwDCbiqZm6AGpyYZaxZr32"
                "sStmmVeRTCnGUTX9oxwmDspt9UkcReTxpAjrrvLL1CHrmKEzKs1ARgjrutPzYQ"
                "9DRAQZbDQ7hqBnKFiuii8C4jB7g7K3NABYwo7mCYuC93FPXkCf5Y3JUi3N8761"
                "2NVECQvhtqbDRA2iqMX4yyP96QfgoTQvnD96L9hfbonv9oi4PiFEXSWGA2T24c"
                "a8bjEGqHUJAMSCDqrkhm6NrhK5AeNi31s2uwurCfQvZLTP5WWShZcMTCgyJf7p"
                "1gWTRrJpbAixL5nHn4XbecwMHbbLit59m3XE6YCrxZetMxiVNebiuFNJxn2q6P"
                "PtHhxYR2SB8YZcHs9sZQtEauMCyvjvt7aEVWL5YBFQx2zoHDzWpoLMWCaumQWR"
                "tUAPf7DFNJzDyZ2DzWyjGFXZ22jC3rU1D7yAebE2BNb96JXwSJpteLenNEm4n6"
                "Qyqf6HPdb6rXa1gCqrXwrRm5GQDfjdc");

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
