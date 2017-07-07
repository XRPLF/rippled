//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/strHex.h>
#include <ripple/basics/Slice.h>
#include <ripple/beast/unit_test.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/PreimageSha256.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ripple {
namespace cryptoconditions {

class PreimageSha256_test : public beast::unit_test::suite
{
    inline
    Buffer
    hexblob(std::string const& s)
    {
        std::vector<std::uint8_t> x;
        x.reserve(s.size() / 2);

        auto iter = s.cbegin();

        while (iter != s.cend())
        {
            int cHigh = charUnHex(*iter++);

            if (cHigh < 0)
                return {};

            int cLow = charUnHex(*iter++);

            if (cLow < 0)
                return {};

            x.push_back(
                static_cast<std::uint8_t>(cHigh << 4) |
                static_cast<std::uint8_t>(cLow));
        }

        return { x.data(), x.size() };
    }

    void
    testKnownVectors()
    {
        testcase("Known Vectors");

        std::pair<std::string, std::string> const known[] =
        {
            { "A0028000",
                "A0258020E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855810100" },
            { "A0058003616161",
                "A02580209834876DCFB05CB167A5C24953EBA58C4AC89B1ADF57F28F2F9D09AF107EE8F0810103" },
        };

        std::error_code ec;

        auto f1 = Fulfillment::deserialize (hexblob(known[0].first), ec);
        BEAST_EXPECT (f1);
        BEAST_EXPECT (!ec);

        auto c1 = Condition::deserialize (hexblob(known[0].second), ec);
        BEAST_EXPECT (!ec);

        auto f2 = Fulfillment::deserialize(hexblob(known[1].first), ec);
        BEAST_EXPECT(f2);
        BEAST_EXPECT(!ec);

        auto c2 = Condition::deserialize(hexblob(known[1].second), ec);
        BEAST_EXPECT(!ec);
        
        // Check equality and inequality

        if (! (f1 && f2))
            return;

        std::error_code cec;
        BEAST_EXPECT (f1->condition(cec) == c1 && !cec);
        BEAST_EXPECT (f1->condition(cec) != c2 && !cec);
        BEAST_EXPECT (f2->condition(cec) == c2 && !cec);
        BEAST_EXPECT (f2->condition(cec) != c1 && !cec);
        BEAST_EXPECT (c1 != c2);
        BEAST_EXPECT (c1 == c1);
        std::error_code cec2;
        BEAST_EXPECT (f1->condition(cec) == f1->condition(cec2) && !cec && !cec2);

        // Should validate with the empty string
        BEAST_EXPECT (validate (*f1, c1));
        BEAST_EXPECT (validate(*f2, c2));

        // And with any string - the message doesn't matter for PrefixSha256
        BEAST_EXPECT (validate (*f1, c1, makeSlice(known[0].first)));
        BEAST_EXPECT (validate(*f1, c1, makeSlice(known[0].second)));
        BEAST_EXPECT (validate(*f2, c2, makeSlice(known[0].first)));
        BEAST_EXPECT (validate(*f2, c2, makeSlice(known[0].second)));

        // Shouldn't validate if the fulfillment & condition don't match
        // regardless of the message.
        BEAST_EXPECT (! validate(*f2, c1));
        BEAST_EXPECT (! validate(*f2, c1, makeSlice(known[0].first)));
        BEAST_EXPECT (! validate(*f2, c1, makeSlice(known[0].second)));
        BEAST_EXPECT (! validate(*f1, c2));
        BEAST_EXPECT (! validate(*f1, c2, makeSlice(known[0].first)));
        BEAST_EXPECT (! validate(*f1, c2, makeSlice(known[0].second)));
    }

    void testOtherTypes()
    {
        testcase ("Other Types");

        using namespace std::literals::string_literals;

        auto make_test_case = [](auto const& name, auto const& fulfillment, auto const& condition)
        {
            return std::tuple<std::string, std::string, std::string>{name, fulfillment, condition};
        };


        // name, fulfillment, condition
        std::tuple<std::string, std::string, std::string> const others[] =
        {
            // PREFIX + PREIMAGE:
            make_test_case("PREFIX + PREIMAGE"s,

        /*
         Fulfillment CHOICE
           prefixSha256 PrefixFulfillment SEQUENCE: tag = [1] constructed; length = 11
             prefix OCTET STRING: tag = [0] primitive; length = 0
               <no content>
             maxMessageLength INTEGER: tag = [1] primitive; length = 1
               0
             subfulfillment : tag = [2] constructed; length = 4
               Fulfillment CHOICE
                 preimageSha256 PreimageFulfillment SEQUENCE: tag = [0] constructed; length = 2
                   preimage OCTET STRING: tag = [0] primitive; length = 0
                     <no content>
         Successfully decoded 13 bytes.
         rec1value Fulfillment ::= prefixSha256 : 
           {
             prefix ''H,
             maxMessageLength 0,
             subfulfillment preimageSha256 : 
               {
                 preimage ''H
               }
           }
        */
                "A10B8000810100A204A0028000"s,

        /*
         prefixSha256 CompoundSha256Condition SEQUENCE: tag = [1] constructed; length = 42
           fingerprint OCTET STRING: tag = [0] primitive; length = 32
             0xbb1ac5260c0141b7e54b26ec2330637c55 ...
           cost INTEGER: tag = [1] primitive; length = 2
             1024
           subtypes ConditionTypes BIT STRING: tag = [2] primitive; length = 2
             0x0780
       Successfully decoded 44 bytes.
       rec1value Condition ::= prefixSha256 : 
         {
           fingerprint 'BB1AC5260C0141B7E54B26EC2330637C55 ...'H,
           cost 1024,
           subtypes { preImageSha256 }
         }
        */
                "A12A8020BB1AC5260C0141B7E54B26EC2330637C5597BF811951AC09E744AD20FF77E287810204"
                "0082020780"),

            // THRESHOLD:
            make_test_case("THRESHOLD"s,
        /*
         Fulfillment CHOICE
           thresholdSha256 ThresholdFulfillment SEQUENCE: tag = [2] constructed; length = 8
             subfulfillments SET OF: tag = [0] constructed; length = 4
               Fulfillment CHOICE
                 preimageSha256 PreimageFulfillment SEQUENCE: tag = [0] constructed; length = 2
                   preimage OCTET STRING: tag = [0] primitive; length = 0
                     <no content>
             subconditions SET OF: tag = [1] constructed; length = 0
         Successfully decoded 10 bytes.
         rec1value Fulfillment ::= thresholdSha256 : 
           {
             subfulfillments 
             {
               preimageSha256 : 
                 {
                   preimage ''H
                 }
             },
             subconditions 
             {
             }
           }
        */
                "A208A004A0028000A100"s,

        /*                                                      
         Condition CHOICE
           thresholdSha256 CompoundSha256Condition SEQUENCE: tag = [2] constructed; length = 42
             fingerprint OCTET STRING: tag = [0] primitive; length = 32
               0xb4b84136df48a71d73f4985c04c6767a77 ...
             cost INTEGER: tag = [1] primitive; length = 2
               1024
             subtypes ConditionTypes BIT STRING: tag = [2] primitive; length = 2
               0x0780
         Successfully decoded 44 bytes.
         rec1value Condition ::= thresholdSha256 : 
           {
             fingerprint 'B4B84136DF48A71D73F4985C04C6767A77 ...'H,
             cost 1024,
             subtypes { preImageSha256 }
           }
        */
                "A22A8020B4B84136DF48A71D73F4985C04C6767A778ECB65BA7023B4506823BEEE7631B9810204"
                           "0082020780"),

            // RSA:
            make_test_case("RSA"s,

        /*
         Fulfillment CHOICE
           rsaSha256 RsaSha256Fulfillment SEQUENCE: tag = [3] constructed; length = 520
             modulus OCTET STRING: tag = [0] primitive; length = 256
               0xe1ef8b24d6f76b09c81ed7752aa262f044 ...
             signature OCTET STRING: tag = [1] primitive; length = 256
               0xbd42d6569f6599aed455f96bc0ed08ed14 ...
         Successfully decoded 524 bytes.
         rec1value Fulfillment ::= rsaSha256 : 
           {
             modulus 'E1EF8B24D6F76B09C81ED7752AA262F044 ...'H,
             signature 'BD42D6569F6599AED455F96BC0ED08ED14 ...'H
           }
         */
              "A382020880820100E1EF8B24D6F76B09C81ED7752AA262F044F04A874D43809D31CEA612F99B0C97"
              "A8B4374153E3EEF3D66616843E0E41C293264B71B6173DB1CF0D6CD558C58657706FCF097F704C48"
              "3E59CBFDFD5B3EE7BC80D740C5E0F047F3E85FC0D75815776A6F3F23C5DC5E797139A6882E38336A"
              "4A5FB36137620FF3663DBAE328472801862F72F2F87B202B9C89ADD7CD5B0A076F7C53E35039F67E"
              "D17EC815E5B4305CC63197068D5E6E579BA6DE5F4E3E57DF5E4E072FF2CE4C66EB45233973875275"
              "9639F0257BF57DBD5C443FB5158CCE0A3D36ADC7BA01F33A0BB6DBB2BF989D607112F2344D993E77"
              "E563C1D361DEDF57DA96EF2CFC685F002B638246A5B309B981820100BD42D6569F6599AED455F96B"
              "C0ED08ED1480BF36CD9E1467F9C6F74461C9E3A749334B2F6404AA5F9F6BAFE76C347D069250B35D"
              "1C970C793059EE733A8193F30FA78FEC7CAE459E3DDFD7633805D476940D0CB53D7FB389DCDAEAF6"
              "E8CF48C4B5635430E4F2BCDFE505C2C0FC17B40D93C7EDB7C261EBF43895A705E024AA0549A660F7"
              "0A32150647522DBE6B63520497CFF8F8D5D74768A27C5B86E580BE3FCDC96F1976293CBA0D58DFC6"
              "0B518B632A6DC1E950C43E231FE1A379AA6DDCC52C70EDF851C6C0123A964261CFDB3857CD6CD5AD"
              "C37D8DA2CC924EDAE1D84CF6124587F274C1FA3697DA2901F0269F03B243C03B614E0385E1961FAC"
              "5000F9BB",

        /*
         Condition CHOICE
           rsaSha256 SimpleSha256Condition SEQUENCE: tag = [3] constructed; length = 37
             fingerprint OCTET STRING: tag = [0] primitive; length = 32
               0x4849505152535455484950515253545548 ...
             cost INTEGER: tag = [1] primitive; length = 1
               1
         Successfully decoded 39 bytes.
         rec1value Condition ::= rsaSha256 : 
           {
             fingerprint '4849505152535455484950515253545548 ...'H,
             cost 1
           }
         */
                "A32580204849505152535455484950515253545548495051525354554849505152535455810101" ),

            // ED25519:
            make_test_case("ED25519"s,

        /*
         Fulfillment CHOICE
           ed25519Sha256 Ed25519Sha512Fulfillment SEQUENCE: tag = [4] constructed; length = 100
             publicKey OCTET STRING: tag = [0] primitive; length = 32
               0xd75a980182b10ab7d54bfed3c964073a0e ...
             signature OCTET STRING: tag = [1] primitive; length = 64
               0xe5564300c360ac729086e2cc806e828a84 ...
         Successfully decoded 102 bytes.
         rec1value Fulfillment ::= ed25519Sha256 : 
           {
             publicKey 'D75A980182B10AB7D54BFED3C964073A0E ...'H,
             signature 'E5564300C360AC729086E2CC806E828A84 ...'H
           }
         */
              "A4648020D75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A8140E556"
              "4300C360AC729086E2CC806E828A84877F1EB8E5D974D873E065224901555FB8821590A33BACC61E"
              "39701CF9B46BD25BF5F0595BBE24655141438E7A100B",

        /*
         Condition CHOICE
           ed25519Sha256 SimpleSha256Condition SEQUENCE: tag = [4] constructed; length = 39
             fingerprint OCTET STRING: tag = [0] primitive; length = 32
               0x799239aba8fc4ff7eabfbc4c44e69e8bdf ...
             cost INTEGER: tag = [1] primitive; length = 3
               131072
         Successfully decoded 41 bytes.
         rec1value Condition ::= ed25519Sha256 : 
           {
             fingerprint '799239ABA8FC4FF7EABFBC4C44E69E8BDF ...'H,
             cost 131072
           }
         */
                "A4278020799239ABA8FC4FF7EABFBC4C44E69E8BDFED993324E12ED64792ABE289CF1D5F810302"
                "0000" )
        };

        for (auto x : others)
        {
            std::error_code ec;

            if (!BEAST_EXPECT(Fulfillment::deserialize(hexblob(std::get<1>(x)), ec)))
            {
                log << "Fulfillment deserialize error: " << std::get<0>(x) << " " << ec.message() << '\n';
            }
            auto const c = Condition::deserialize (hexblob(std::get<2>(x)), ec);
            if (!BEAST_EXPECT(!ec))
            {
                log << "Condition deserialize error: " << std::get<0>(x) << " " << ec.message() << '\n';
            }
        }


    }

    void run ()
    {
        testKnownVectors();
        testOtherTypes();
    }
};

BEAST_DEFINE_TESTSUITE (PreimageSha256, conditions, ripple);

}

}
