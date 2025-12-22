#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>

#include <algorithm>

namespace xrpl {

class Seed_test : public beast::unit_test::suite
{
    static bool
    equal(Seed const& lhs, Seed const& rhs)
    {
        return std::equal(
            lhs.data(),
            lhs.data() + lhs.size(),
            rhs.data(),
            rhs.data() + rhs.size());
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
                beast::rngfill(src, sizeof(src), default_prng());
                Seed const seed({src, sizeof(src)});
                BEAST_EXPECT(memcmp(seed.data(), src, sizeof(src)) == 0);
            }
        }

        for (int i = 0; i < 64; i++)
        {
            uint128 src;
            beast::rngfill(src.data(), src.size(), default_prng());
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
        BEAST_EXPECT(equal(seed1, *seed2));
        return toBase58(seed1);
    }

    void
    testPassphrase()
    {
        testcase("generation from passphrase");
        BEAST_EXPECT(
            testPassphrase("masterpassphrase") ==
            "snoPBrXtMeMyMHUVTgbuqAfg1SUTb");
        BEAST_EXPECT(
            testPassphrase("Non-Random Passphrase") ==
            "snMKnVku798EnBwUfxeSD8953sLYA");
        BEAST_EXPECT(
            testPassphrase("cookies excitement hand public") ==
            "sspUXGrmjQhq6mgc24jiRuevZiwKT");
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
            BEAST_EXPECT(equal(seed1, *seed2));
        }
    }

    void
    testKeypairGenerationAndSigning()
    {
        std::string const message1 = "http://www.ripple.com";
        std::string const message2 = "https://www.ripple.com";

        {
            testcase("Node keypair generation & signing (secp256k1)");

            auto const secretKey = generateSecretKey(
                KeyType::secp256k1, generateSeed("masterpassphrase"));
            auto const publicKey =
                derivePublicKey(KeyType::secp256k1, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) ==
                "7E59C17D50F5959C7B158FEC95C8F815BF653DC8");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::secp256k1,
                    generateSecretKey(
                        KeyType::secp256k1, generateSeed("otherpassphrase")));

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

            auto const secretKey = generateSecretKey(
                KeyType::ed25519, generateSeed("masterpassphrase"));
            auto const publicKey = derivePublicKey(KeyType::ed25519, secretKey);

            BEAST_EXPECT(
                toBase58(TokenType::NodePublic, publicKey) ==
                "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf");
            BEAST_EXPECT(
                toBase58(TokenType::NodePrivate, secretKey) ==
                "paKv46LztLqK3GaKz1rG2nQGN6M4JLyRtxFBYFTw4wAVHtGys36");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) ==
                "AA066C988C712815CC37AF71472B7CBBBD4E2A0A");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::ed25519,
                    generateSecretKey(
                        KeyType::ed25519, generateSeed("otherpassphrase")));

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

            auto const secretKey = generateSecretKey(
                KeyType::dilithium, generateSeed("masterpassphrase"));
            auto const publicKey =
                derivePublicKey(KeyType::dilithium, secretKey);

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
                "J7qUP5JLCo6v8SHm1REcWuD25Pe1nu94FLXwRDXE4XvYfiHxbUo79dBc6t9WiB"
                "bKhkPG6KEAnNxEKuHb6orxQwtiSn36YepA3YqHMCZaMUubGvoqGUs7pJixUHPs"
                "4jThaQGstNYW4267e65Pk8Pm7z9Hsb878r9NRHiVs5Eeu27aDzNnEdoK5bzG6u"
                "CHRC72qHQmdxNByFBDJGuXCKkTr7sk36E5ebRtJnCG1dh134xechmavHKPJaSU"
                "DxkK1NvrSAFoqqtoHDWyKcfjfKkkA35UKTjz9zqQKiyPYbiTtw43tA5Ex8pdLZ"
                "B4JVHRHXa1sDhceQ6qbn8rktt2XjQpfH5C4iL6bTdS8vc1yRXfBXu5pf5MuGKm"
                "s2toZJins5yyzWaLxgQWuGUAdmya2wRaA4J2JfkZNLYzq77SrZp5sHotuLdnfB"
                "GRbUUxmRmtomXnb5M9R5QcmvLur4ZT4EQfR2FFVqThziEzDuedE6G1traHBb9G"
                "gMyfqDnj4jWeJmdpTCUZE3JxLihfdv2Vb2Be24JQSQbWS1We6hiwboomQKeUd8"
                "hTxPoGAJ8tcjRpt3icgiRcHR3oKASsquzkWNe1s7xndfJcB6rB7crYvBvkHWah"
                "Lz8CAAJPxXaZvZ3RKmNAi5KiZ9robD5GUZnE5u26Gkq7Xo3B3fzXWtkii8vNsr"
                "nktgEdgnQ6WUpDug3Vetv8Av66rffotWkTUcB7NHYyHM8PmAF95bis8gLVxkqM"
                "XeRPQo5fbx4BHUrmdsXcKX45atdH8ZrkTPCQcm4T1EGiCviM6T7rQzEiRX2fei"
                "3VBgafZAa5DVd3hFrp5KndVN9UqJejmDSJSVaztNGtLzarKvoAtMXpwqSNYRq8"
                "b1S9AJWW1aBKo3tn6cpt1LUKzXJFn41KXa2eZqMNHtaRLpY1kYXhUcEbX3Hqi6"
                "iJMvSuTvT5n847niPuRVAKbVHEhoeXckhyiw7DCZScRP4niGDN8YDUTgGqbvwP"
                "bDA5Q7rmgMZdi4g9m3wipmbFNxDqMpRufNTgnXn6nNpRudPxsY1JUZRZPLKX3u"
                "TqBCsNyYiuS5AFwG6kKCAnGo7UwzGfVw2YQ9qRRtu2JR8nj1tHiuWpK5rPx1Yg"
                "j2tvKrnRH6Kob3uM5VEKWRJx5r1T3n8en2vH9puHMy2tGoDFXYzzmg2KzehzCL"
                "u1EbddTfZmjuKj7u45tPR2DD6gVyy9zH2vX2V17CPmnUYxrXaj3kGu4rJVtJYP"
                "y6kvbSzPivKi2hB4cB21jQPNrNCEPzegDjYJiLjY6dMez3eiPNN3ST2MjyXU3R"
                "mLse1hNiNqMQyEJYqn98QEfve1nD5DYtSZdcBYVGL9MHKqvudyDTq3dziZZ9ua"
                "MTRbPa6oQMi5vc8R1jdWqpQJHMkrbbyX6MiemRxtMcJpT3ELwByR4M7dbbYnES"
                "73pTD7NaCFx6Pj6vrp9fgDU6Y1PPrfYKmeZQnjysUGWhgMSm1dfbPUhpqUpj5k"
                "jiDhGBAKTn8N3Zkzkh3XyWkJwCkeKqamQDWELnhYhGqgD3pPpaLyT2tEV4np6L"
                "ewpXzyGKnDdUhryKY8RJMHV3kEiaC573q5e9tSXAY8PFzB242XGTp1Eesdcbsc"
                "ssXDC68SdXsvGp8EU22UcoVyJHTw13e23pBFpvYp9o5QJ2WaovzZp77jcAKrvu"
                "VHLodnYdLJ1osBL1ez6Bz3WwxABJApzC7oHMWB7WipJiiTUJQrhqiFTTKHtv78"
                "Gj1xhdDLjbhk8V8QEeADzdkx4VDsZnqUm1YachwZNuRYWWpXgoV71SzKnb1NGS"
                "6LBTsDs8KpxVoT88JccWDv3qTRE8Z3qEKid9WVmVS5ojiodakE1KFVh99PvHtj"
                "cNKCL9kLpmFvFiJ7KEE27DsQue1d6Jn77KkfL2usArEDPh8jc2QSUDka2mAd2C"
                "RWUMpd9xnFr8TWNyt3jnqvWo6EamNdkPWbYmXn4Ht8gkBLi73HNA1egx3mn5V7"
                "igX1R8VabkTujvo14qX9j7KSEsgCvadKpYBkD3HQG6b135Ltf8dpr9QpXq8KEs"
                "S6H7vN5bZfUYNQyLhcEyGJj5WU3JLZQet5R8UcV3wtLb4Twcm19ArJ6nRZFutF"
                "n1qCVntBS3d3PQcf2arYBNB6sj1BU67qGoE25nSvSXuWkP3oHN5WjjtZrR9iVq"
                "KkeEv6orMNJMLFbd82qPF3jfL6bSRBvnxb1S9QWZ7dT7wFG2C9vxqssNvrVDYm"
                "CDtpVRrarSCZg7StqGncmBLxpXpmzgX8AnowNwfGycE38rdHWrzRByduvZHgaJ"
                "grwjdPPkjY9LgFwXyjusV93zurTXEAcGQ96xZot468csqJx74jmizrGBWB1TnC"
                "iBTefB2TweZpDMKS1VXkP4aPpug6NLpshXLqpxcX1TWyDujcmvZTYnLQH5WLgG"
                "KpSCKr5ULTs1p2Ee3etL7brWQTHNRFLuUQqRXz7ZKakEVrKoGarVvbrsruWgt5"
                "cJ8s6JVwe6iBedtYAVKTCGkeDo7hKSqGhJjHWwwNycihXnQQ7UzRWr9dvsXQWx"
                "XYoXk5RxK39nY5hdpr964pjAdfj155UjgqVmKx75F9rHjHR2Ffd4WkLoNszU57"
                "kpMmm6PvRCFVKzbaUPgJH9xiz9ypBg1mV1E2JDKps78mWxeudtUEtCrtU7SYM8"
                "2oTcN7L2FYB1u5Q2s4Mc2wJjseyx9bKXAToU8VDBJV1zY4J1nvYAbQyQhYnFpr"
                "9tQQKoLzdQMqSrFGkFVZ4hPLUKiNucmSmBxXvuGaoRFD3GjvtwcXXHSs8g5Vvf"
                "cLLaWqhLGMtRAU3DDRy5ftY8CPDSWTAopRm3ErtmkRUAAT6Mamf6iWWTKetYtm"
                "ZozqprBh1SNkN51Lpc8qtEK6vWz6sTB4AfdqdE3Y2ChuqtyQQXsNQj2fcY5L6g"
                "NMJFEQ3W62QNHYSkPUcUURRFYSxUsC4QHkahEz86U9FoNMyaHNE7Bi1ufq3kcz"
                "fToAkPPkdr5Tg5uXo3BkNbDnFtQWP2UywLCrrxi29yucB8zfq27QA1ATBHKVFG"
                "15sgjksNL3AfgZRoDE2n7TeG3jXvMfHiWevaExkS1Gsht6QJXaVX3NaQMd9Uy4"
                "vqWdyTucNcRLZzrfgzmyvLzbRfBzPGLHdTJxLvB9c4VNhQLSdCDJQHZ8TqxGhH"
                "u5dw1owkV7DjcBxyKLUM86mPG8wVDmkV34bckE6VBcfoERcQsAuQCsF8n34HVu"
                "usxziQgPJ2rYM2Gi9wNdmrBUoq6WiYzRtuG4GSLxV3KoZipYTa4gmLyhFQhaUr"
                "QfxvjFnSVbTqwvgxX6bt1Uo3jYpqzYieu6EBmUWbo4GpXkzcnAK6rymz3F3fka"
                "x2V9LnWMnYjfayq8YsCVmxVCPMbVyB5xS2LWqFaUqdxFDvDD6quBGeL4YP9oGL"
                "sR1TdLRCRqVhD5YcC5nALXhXbwmfqfyMLLsYFsYidEHdzeATF");
            BEAST_EXPECT(
                to_string(calcNodeID(publicKey)) ==
                "8A94F2BC52E94646EC83CD988657FE37A9D5CDB5");

            auto sig = sign(publicKey, secretKey, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(publicKey, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(publicKey, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherPublicKey = derivePublicKey(
                    KeyType::ed25519,
                    generateSecretKey(
                        KeyType::ed25519, generateSeed("otherpassphrase")));

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

            auto const [pk, sk] = generateKeyPair(
                KeyType::secp256k1, generateSeed("masterpassphrase"));

            BEAST_EXPECT(
                toBase58(calcAccountID(pk)) ==
                "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair = generateKeyPair(
                    KeyType::secp256k1, generateSeed("otherpassphrase"));

                BEAST_EXPECT(
                    !verify(otherKeyPair.first, makeSlice(message1), sig));
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

            auto const [pk, sk] = generateKeyPair(
                KeyType::ed25519, generateSeed("masterpassphrase"));

            BEAST_EXPECT(
                to_string(calcAccountID(pk)) ==
                "rGWrZyQqhTp9Xu7G5Pkayo7bXjH4k4QYpf");
            BEAST_EXPECT(
                toBase58(TokenType::AccountPublic, pk) ==
                "aKGheSBjmCsKJVuLNKRAKpZXT6wpk2FCuEZAXJupXgdAxX5THCqR");
            BEAST_EXPECT(
                toBase58(TokenType::AccountSecret, sk) ==
                "pwDQjwEhbUBmPuEjFpEG75bFhv2obkCB7NxQsfFxM7xGHBMVPu9");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair = generateKeyPair(
                    KeyType::ed25519, generateSeed("otherpassphrase"));

                BEAST_EXPECT(
                    !verify(otherKeyPair.first, makeSlice(message1), sig));
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

            auto const [pk, sk] = generateKeyPair(
                KeyType::dilithium, generateSeed("masterpassphrase"));

            BEAST_EXPECT(
                to_string(calcAccountID(pk)) ==
                "rDdkg2HADzqCh6s6CyZ53ExSQ3fRMEycuV");
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
                "KZhAYmWBS8ZU3yfdrr6j1QGw988Dao5RNGxmSBUZAM9ttzDEuXEXP7tVqZ6JzN"
                "cEpm8kgAADqhxhC6Ai27mX3m3PFZWQ9Y1oA8rZfBzKrr1rB6EJEb2Z7z3J214B"
                "m5ZEC7ePiHrgdVuk2VHmKNzkvfu8EDW3Qk6T4M894v5xFsrpshLVmxhpcCwMR6"
                "YyfRNiyKmxFfN6PnHuxUuibqV7ue1gytMspTmjUhupH3LuvqTFKrjWfvdgE4CX"
                "dnCtSvnWChYyLLJftCkHHXMEA1PffhP9r1gaiGebvTinmcEbAHewJGyMgazQZB"
                "4aJzTcnXQKKwgHHzVJ6uX8ifPQZx4e6C2NqSbgmwwpgEMYwcAcNiz3PWqukYqU"
                "Qc85c4eYbcs8tZMf2gxFpYnJ2dFF7vLvM74jVEaZYs8V68gdx6H8gDHfX8VUFT"
                "CSRiBda6serdsod6ZsoENYmLw7FpHvHUreVxnu1XFYZzmzXV3mgCpfpcXtiAKP"
                "ub8BLTxz4ZYR6DXd3orGStMZCVQ1dWHpvHhHXxc9XgKVT4VEt6HAnnYhvbMTXs"
                "mmxgk6nJ1E2QBotymEZhsSrKxwrSZFUskDaMkdabi8ifEhk3FwH8w1NG8EQ17o"
                "UTjwio14V4SHvUsWFapkMUhSxXGFNdvMchWYnP9mAKqkgKm9mSBhZJaHZA1PT5"
                "okHxPNbVNNZQQvtM55Zr5Fyj7V29aGf7D3XJcVD1KzGQDuuzhzQQbxKptQjbGY"
                "Fe9e81diGpnad2qiuevYwyK1YmMxmcos57jGWwL9mpKFBS9nS1TEmuJwHoegPt"
                "QWQfctVGYyJYCKxDDp6vkpAf7EFSjTdqwXSxRAi9iGw35GDg7xkd3Hcs5iJims"
                "AFNBuHdSFPNB78c6NzhTShWQ4ztUAYMkfZmsvoHPw2dRmUznUBTwKSzUAa4bvu"
                "Ew5W2LsaGY5yCaFLfVfPp7JEMZDbt5JDwuCYTkWeGiZaKm5UuFaQzBSmDv9XC5"
                "xMgpfdcm4HDe8PbowmM4x4wLKLPDqVUcQBMTdk7HquTwCCGRKkApPBDHccuDM5"
                "nuPwTJwxSwXuJRRa8MxSSNBSwR7KzQheWjLH1iUbv6fNxJUuFModknUairfmsk"
                "hrbGuLx3rC61jbt7jTW3NkAY3YDsaTzLxiB8URmnDBhCHPYLKmGPKH2gvvXn9C"
                "AusXDW9vpnrvXHpuUvLhZSPX4JrHbFfUmhiVjkN4mDycn3KaGbZK9ncZx6vJC6"
                "9otwdCzefJtWQq7axjnJyzPi7Y2ZimLyjPSociKjb9F19e8gApHtkDvHpmZgcY"
                "Ao2z9SLcyfMsbjXNRStn4iHxN2y3v9YQ6uDucEhJ9yoLEgEWsMLmEgvfBMFy93"
                "xnGeVEyuTgUMPupGZQNEzwS1EesPGx5t6gPSQmsaEcHnGFdjbDfpTK5CZxFHrh"
                "rKk3trav8H9MP24cxzQ1MpNpNfjUQbyV6R3wNZrmzRpGWNXhYNuzbWpochuMf2"
                "CYk8BTKujHw989ULnqsJDzW2mKe7nAaX7Mk1eoByiDcXWUU5Qu61knzuKefyDU"
                "9CQoVzukTAG1VTUugZQYrVXBPa26Z1AcQAbn9viAWpZucyEsaQaeT6to7PpFcg"
                "d2mNUEqcSQnALSzFZKbiKRzZngsAn54Htbfs5MBydatX7CEhUYQuiMfA4n3JCF"
                "7TfH7FeFZLGkTZ6E8giqCRbgPh3DEkygfkcBKrczAy1rv3zD6GPMaZpLG8FE1v"
                "xbdNdgbaimLneEctwuH8x7Vds95xcPoYptbu2PytxLKtAc7Kc23iYiwitPLXL5"
                "EmWoG71quUnJiLdwkXN3sn4eMs5Tp5URGdDgXmFw47XNhwD59Y8Dv6H7QFxdSw"
                "gmewkFWBFgjV8AHg9brWG6HPdV5AZB4krZiXSCXMjvvUAj36ojJMiU2sgYaiUA"
                "HKcoNQzkfr3JpuNfLVJHtAGxBg2nybU9Sb1TptBHvC1NHkqzUQFRXz1D1MeaqV"
                "ooF5qn8VZyPugjCd84AZHKbipacR7VnwHYbFi9uvisraw8CEki8z3sPRUTSyeR"
                "qxQfYZvpKqHwygERm3y7CqQCwFeVvsK9i1aNU5AAUDera91iVDRGYo8gZT9Snb"
                "5FvG3RAVNyETfynxMEQx34w4Dxcs46QZXuyyFedjiJBjXC21d3DjHixos9mv8J"
                "xA1wnBTanDJJc6Hfc1Cu4VCTBK21JXkRhuGeC47XWAWhFvuNcL8tf6HyA2zNVJ"
                "hCyk4yFSYEBy2BPXP7QdFiARJM2bRSmmXWMAbUm685pJZar3fiwxpJuMS394os"
                "J8ouafHw56xBySghcSU5tgvPbeixeWHxh6w1JsLwMWiH1z4ZDWSk62SRVk2Bg8"
                "xQzGd6izQWM3Qy99FqauZqCfNDRtmcHQhJrZmeTWJYNxJ2Swy6bAyZrjKGe379"
                "4UiQ59cskVfbZxsiJhh7D4TVX7PRC6GsxzioZL2p4eutnSz49WxKL92554f7Hf"
                "PWBN3ENoRfF8MvWQUJ4VvrTAHE4GjLo3tdkw4NAFzR2ZPpP7nCP4sHxyKSSAWC"
                "Myk4mwKp4g1mEBBgTnEJaD3ppfft8B62SCdvjwCzvoh1pq3ZrPA578FuBJRcoP"
                "ML3bWddKb2G8p5BGASKMeETE2VmzznM9wDfW4Xq3Aa5VpApDy3BH2MqM3yGEEM"
                "PbAeeL3L4pjSQ8Z8oKq244spHANjuVwi28fbSQTPn1Wq1uuqhvnqiurdqTYDB5"
                "XJ6agh6stcLP6v4sDizLJaENpBQiQ7qjawUzzNwMiJMSq6b1N4DiLcJNwRBjNm"
                "jQPHBexAwkLrxC3nUNUyDtny58Msvknua9wcCRRTCMEg9xUmD6sB9kpdHDUXsg"
                "oUQvMoRhPakzyyNJfUB28YUJ7KDupy9VHdAuTtzGiUNkNXKckACsGZ6nS2iRJh"
                "ooZH5dzEiu99tSBFPUVBQT15Ughrdn1v5n8SwwKAXe3qNRy9WF9HLd8vDsFKyV"
                "tPQJKSYkP76SdyPZxDT1PKVCx7EBm7B5o2S459tyYBX4T7Z9JdtWzsD49nGxNy"
                "SrBwCBLfcvR6aK8EcA9G2ewaqKjAzVFRVw1oDEKnj6DKHtf3n4vNqxx6oPn5G7"
                "riMYCohgBWVXHcv9qWDaUQAhaJjK8ES7nTuAdo7cQ7XY78KWDc1WgiHojAxC8a"
                "tVVspKHFY98HM7rvmzsydMRhFPYaFBhVctLzvg6ok6enMyU9TyjVfEbWRXV3J5"
                "JMGqkABYnx6nriz9YFRDuQh6fwof4KUyjpUXk5eGPxtGAYSAqbNNFpDAmf6n8h"
                "ybHqHGYV1rGeC9dVdGwgzHNjbgrUkDSx6vTHCWP8kvfMMtHtmGxuRCcPCz88yU"
                "LNYo6YSHBp1LKfFBcZiCA45DLac4L69SHeueLdJKZrCHBAwg3Df6J7dDhbzCHK"
                "SUqMDaZzaNTynHcLK79Jya86dLuzKh5Mk2qEoFysGRSr2MhsP");

            auto sig = sign(pk, sk, makeSlice(message1));
            BEAST_EXPECT(sig.size() != 0);
            BEAST_EXPECT(verify(pk, makeSlice(message1), sig));

            // Correct public key but wrong message
            BEAST_EXPECT(!verify(pk, makeSlice(message2), sig));

            // Verify with incorrect public key
            {
                auto const otherKeyPair = generateKeyPair(
                    KeyType::ed25519, generateSeed("otherpassphrase"));

                BEAST_EXPECT(
                    !verify(otherKeyPair.first, makeSlice(message1), sig));
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
        // keys should not be parseable as seeds.

        auto const node1 = randomKeyPair(KeyType::secp256k1);

        BEAST_EXPECT(
            !parseGenericSeed(toBase58(TokenType::NodePublic, node1.first)));
        BEAST_EXPECT(
            !parseGenericSeed(toBase58(TokenType::NodePrivate, node1.second)));

        auto const node2 = randomKeyPair(KeyType::ed25519);

        BEAST_EXPECT(
            !parseGenericSeed(toBase58(TokenType::NodePublic, node2.first)));
        BEAST_EXPECT(
            !parseGenericSeed(toBase58(TokenType::NodePrivate, node2.second)));

        auto const account1 = generateKeyPair(KeyType::secp256k1, randomSeed());

        BEAST_EXPECT(
            !parseGenericSeed(toBase58(calcAccountID(account1.first))));
        BEAST_EXPECT(!parseGenericSeed(
            toBase58(TokenType::AccountPublic, account1.first)));
        BEAST_EXPECT(!parseGenericSeed(
            toBase58(TokenType::AccountSecret, account1.second)));

        auto const account2 = generateKeyPair(KeyType::ed25519, randomSeed());

        BEAST_EXPECT(
            !parseGenericSeed(toBase58(calcAccountID(account2.first))));
        BEAST_EXPECT(!parseGenericSeed(
            toBase58(TokenType::AccountPublic, account2.first)));
        BEAST_EXPECT(!parseGenericSeed(
            toBase58(TokenType::AccountSecret, account2.second)));
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
