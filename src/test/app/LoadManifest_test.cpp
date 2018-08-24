//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <test/jtx.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/PublicKey.h>

namespace ripple {

static char const* mpk = "nHB2ioiXxPsmtHhyAQKeip73XEgKKPq97M4UM6RfKymcNQKUgweB";

// Seq 1, without domain
static char const* m1 = "24000000017121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C8732103432BA79264C9AE"
                        "2278CC5A3DC22E8ABC078EC57D5105D6B42948E2AEBC22DD49"
                        "76463044022074B1FEA94681F529152F4E30995142DA1D5C13"
                        "F746650BE257DDC47440EF62A002207E08646C2108DA356DB5"
                        "E4A262F5F32C1A37BD60F4182BFB1ED1B1316095A934701240"
                        "6428AC0EE384B2AA50FDD98988A640D33044DD79CBDB70D3ED"
                        "696F7F6FC087BD723184DFBEEAA1EEDE841B6A3A6A83233457"
                        "098E444F567799584E32B41C7006";

// Seq 2, with domain example.com
static char const* m2 = "24000000027121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C87321026880121DAD9E55"
                        "D1C7F6261A8F7F35AC571CBCC25FBF6897E04789C470345A56"
                        "764630440220194A6FD05FE72747D68F8E64E6E3CE88BC0A29"
                        "3E88AE03E9E8D133771CA9A8DF02202492D2C83AED9ADA63B0"
                        "7D5B923346870F8A54CE2150DE1E07098EA7C3227B75770B65"
                        "78616D706C652E636F6D701240728CEF12A532FA9A19FA4016"
                        "69AE736CD5E888F9196F65845702C6C4B3C349D08957D28196"
                        "25A44A02EAE38D3B4FD1B8DC9F2DEEB32B5C7BB1A9B0B734D8"
                        "560B";

// Seq 3, with domain example.com
static char const* m3 = "24000000037121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C8732102D0FA98DC466BDD"
                        "3C463FDF6465384B61E54D3D85FFC12DC8B327759DF6F719A1"
                        "7647304502210081564BE540B3FDD921F642B64342B63D65D8"
                        "08D103AC9905311100C1FF251D1302204A74495AC0F3C54376"
                        "E9CBD3F8B727D2BA0D8526149C91AA9ACFB6FFB3A63037770B"
                        "6578616D706C652E636F6D701240FFEC33E1220DA8BD3E72A2"
                        "22EC16FC38E116C6DE0D51B9EF654C5A3497F591B9516604C1"
                        "9ED3C60B65D5F1DEB50FA1911F8F0B775FFD7EAA918C64593E"
                        "D7D909";

// Seq 4, without domain
static char const* m4 = "24000000047121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C8732103A9AA08DCB5E07E"
                        "8E8ECA6CF4BF27FD2BCD1A303EF1890D61B032B82E9CDF2A4C"
                        "7646304402204F4574E949A34A17519DE081D8FA9357F995A4"
                        "2075EB52C060AEB574FB9559DA02207BDB65BE233565B389B8"
                        "0E73D03BCC76337BB4942B31D9484BDA049196EACED2701240"
                        "31B173EE00645CD53F0177C0B7CDF41B1229BF26BBC059A4E9"
                        "177836E9A5CE0138A54919466C83036420703201706EC94D95"
                        "3355C44719B7CF10DE6B40F35209";

// Seq 5, without domain
static char const* m5 = "24000000057121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C87321030E470C026F7EB5"
                        "4A592F4C20D0825E31E4DDC14A4235B3BC256FF9245DAE5540"
                        "76473045022100D05DC928B3826E131B4E44FC7E427DB44E4C"
                        "E0CE91806017C8195DB58BEDD65B02207307A2C61CE4D5D7A9"
                        "14C8DD151E471A3A60EC3BC7BBB13E23AF7B794A3547837012"
                        "4048D9187F7EC58FB85262123BC574D31E245F73D747D67C71"
                        "E4E106FFCC2011301C1E84FE0B952C94E84EECE251844E15ED"
                        "358D68FE98F7B3932808846B4E2504";

// Seq 6, with domain example.net
static char const* m6 = "24000000067121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C873210228D8A4C7FCA656"
                        "7DA6BB0532FA56B5D761E9FDB92723CCA63641357F58C2E933"
                        "76463044022059979E116A61DE2CB0D9C5982F8D96E0A26AE9"
                        "29526F851BF1949732186E089902204D03D8AA496D941F53DC"
                        "51E9174E202264E5941EB72DDB3B45ED7116A226D00A770B65"
                        "78616D706C652E6E6574701240527BCF45AB5496C80934A3E8"
                        "4AE8E6DB9779F446B32CF00EF615DAAB41F692C31A044E9EE8"
                        "D8F5D6F285624B86724EBE1E91CFC7CDA9BCBD61BAD2ED5086"
                        "900F";

// Seq 7, with different domain example.com
static char const* m7 = "24000000077121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C87321033DE86C9CAD8DB9"
                        "D0230305BCE9D074FB5337CFA51F5C175857331F378E5F22C2"
                        "76473045022100839FC18FF980A64F13C5A07BFE429C7ADCD8"
                        "6E671810F8BA066B3301A45263D202201E080E4B4C250A6D15"
                        "C1C6BD6645ABF91B5E4CD6CE50E194B38E5FE50A50FC85770B"
                        "6578616D706C652E6F726770124068268B6E6EE73CE2AFBCD7"
                        "5D9C02D35361A19CCF71BFE6FA8DC890D454E49494A7C0D263"
                        "B1FE43B3CD6DC2BC40141F4D5F3FCCF028D26530AADDCBA0C1"
                        "024807";

// Revocation
static char const* mR = "24FFFFFFFF7121ED1E79F744118D2602B8603EA6272DF04162"
                        "495C785D3CEE2C4657D15E1CBEF8C870124018F8520DC76445"
                        "E17DC9232BC2575932574E0DC01C67A9305132B59E85AE46B0"
                        "4312BAD3D7758F23EB918A847E7017A3DDCFB75AC6727A1DD7"
                        "D67F5566907803";

class LoadManifest_test : public beast::unit_test::suite
{
    void
    testWithoutAmendment()
    {
        testcase("Without 'On Ledger Manifest' support");

        using namespace test::jtx;
        Env env(*this, supported_amendments() - featureOnLedgerManifests);

        env(createManifest(env.master, m1), fee(env.current()->fees().reserve), ter(temUNKNOWN));
        env.close();
        env(updateManifest(env.master, m1), ter(temUNKNOWN));
        env.close();
        env(createManifest(env.master, mR), fee(env.current()->fees().reserve), ter(temUNKNOWN));
        env.close();
        env(updateManifest(env.master, mR), ter(temUNKNOWN));
    }

    void
    testWithAmendment()
    {
        testcase("With 'On Ledger Manifest' support");

        auto pk = parseBase58<PublicKey>(TokenType::NodePublic, mpk);
        BEAST_EXPECT(pk);

        using namespace test::jtx;
        Env env(*this, supported_amendments());

        auto checkSequence = [&](int expected)
        {
            auto sle = env.le(keylet::manifest(*pk));
            return sle->getFieldU32(sfSequence) == expected;
        };

        // Updating manifest entry which isn't present fails
        env(updateManifest(env.master, m1), ter(tecNO_ENTRY));
        env.close();

        // Creating manifest entry for the first time without adequate fee fails
        env(createManifest(env.master, m1), ter(telINSUF_FEE_P));
        env.close();

        // Creating manifest entry for the first time with adequate fee succeeds
        env(createManifest(env.master, m1), fee(env.current()->fees().reserve));
        env.close();
        checkSequence(1);

        // Creating manifest entry is OK if entry already exists
        env(createManifest(env.master, m2), fee(env.current()->fees().reserve));
        env.close();
        checkSequence(2);

        // As is updating manifest entry to a newer sequence
        env(updateManifest(env.master, m3));
        env.close();
        checkSequence(3);

        // Even if we skip manifest sequences
        env(updateManifest(env.master, m5));
        env.close();
        checkSequence(5);

        // As long as the manifest sequence strictly monotonically increases
        env(updateManifest(env.master, m5), ter(tecMANIFEST_BAD_SEQUENCE));
        env.close();
        checkSequence(5);

        env(updateManifest(env.master, m4), ter(tecMANIFEST_BAD_SEQUENCE));
        env.close();
        checkSequence(5);

        env(updateManifest(env.master, m6));
        env.close();
        checkSequence(6);

        // Corrupted manifests shouldn't work
        for (int i = 0; m7[i] != 0; ++i)
        {
            std::string mx = m7;

            if ((mx[i] >= '0' && mx[i] < '9') || (mx[i] >= 'A' && mx[i] < 'F'))
                mx[i]++;
            else if (mx[i] == '9')
                mx[i] = 'A';
            else if (mx[i] == 'F')
                mx[i] = '0';

            env(createManifest(env.master, mx), fee(env.current()->fees().reserve), ter(std::ignore));
            BEAST_EXPECT(env.ter() != tesSUCCESS);
            env.close();
            checkSequence(6);
        }

        // Revocation also works
        env(updateManifest(env.master, mR));
        env.close();
        checkSequence(std::numeric_limits<std::uint32_t>::max());

        // And once a manifest is revoked, nothing else can work
        env(updateManifest(env.master, m7), ter(tecMANIFEST_BAD_SEQUENCE));
        env.close();
        checkSequence(std::numeric_limits<std::uint32_t>::max());

        env(updateManifest(env.master, mR), ter(tecMANIFEST_BAD_SEQUENCE));
        env.close();
        checkSequence(std::numeric_limits<std::uint32_t>::max());
    }

public:

    void run() override
    {
        testWithoutAmendment();
        testWithAmendment();
    }
};

BEAST_DEFINE_TESTSUITE(LoadManifest, app, ripple);
} // ripple
