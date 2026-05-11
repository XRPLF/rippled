
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>  // IWYU pragma: keep

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <bit>
#include <cstdint>
#include <unordered_set>

namespace xrpl::test {

class STIssue_test : public beast::unit_test::Suite
{
public:
    void
    testConstructor()
    {
        testcase("Constructor");
        using namespace jtx;
        Account const alice{"alice"};
        auto const usd = alice["USD"];
        Issue issue;

        try
        {
            issue = xrpIssue();
            issue.account = alice;
            STIssue const stissue(sfAsset, Asset{issue});
            fail("Inconsistent XRP Issue doesn't fail");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            issue = usd;
            issue.account = xrpAccount();
            STIssue const stissue(sfAsset, Asset{issue});
            fail("Inconsistent IOU Issue doesn't fail");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            // Currency is USD but account is XRP
            auto const data =
                "00000000000000000000000055534400000000000000000000000000000000"
                "000000000000000000";
            BaseUint<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            fail("Inconsistent IOU Issue doesn't fail on serializer");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            STIssue const stissue(sfAsset, Asset{xrpIssue()});
        }
        catch (...)
        {
            fail("XRP issue failed");
        }

        try
        {
            STIssue const stissue(sfAsset, Asset{usd});
        }
        catch (...)
        {
            fail("USD issue failed");
        }

        try
        {
            auto const data =
                "0000000000000000000000005553440000000000ae123a8556f3cf91154711"
                "376afb0f894f832b3d";
            BaseUint<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            BEAST_EXPECT(stissue.value() == usd);
        }
        catch (...)
        {
            fail("USD Issue fails on serializer");
        }

        try
        {
            auto const data = "0000000000000000000000000000000000000000";
            BaseUint<160> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            BEAST_EXPECT(stissue.value() == xrpCurrency());
        }
        catch (...)
        {
            fail("XRP Issue fails on serializer");
        }
    }

    void
    testCompare()
    {
        testcase("Compare");
        using namespace jtx;
        Account const alice{"alice"};
        auto const usd = alice["USD"];
        Asset const asset1{xrpIssue()};
        Asset const asset2{usd};
        Asset const asset3{MPTID{2}};

        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset2);
        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset3);
        BEAST_EXPECT(STIssue(sfAsset, asset1) == asset1);
        BEAST_EXPECT(STIssue(sfAsset, asset1).getText() == "XRP");
        BEAST_EXPECT(
            STIssue(sfAsset, asset2).getText() == "USD/rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn");
        BEAST_EXPECT(
            STIssue(sfAsset, asset3).getText() ==
            "000000000000000000000000000000000000000000000002");
    }

    // Hard-coded wire-format fixture.
    //
    // Verifies what STIssue::add() actually puts on the wire for the 4-byte
    // sequence field of an MPT issue.
    //
    // Before fix (V1, no amendment): add32() applies a native-to-BE swap on
    // top of MPTID bytes that are already canonical BE. On LE hosts the two
    // swaps cancel and the wire bytes end up in LE order — the opposite of
    // what a conforming client expects.
    //
    // After fix (V2, amendment enabled): addRaw() writes the MPTID bytes
    // verbatim. The wire bytes match the canonical BE encoding from makeMptID().
    void
    testMPTWireFormat()
    {
        testcase("MPT serialization - serialized sequence bytes are canonical big-endian");
        using namespace jtx;
        Account const alice{"alice"};
        BEAST_EXPECT(std::endian::native == std::endian::little);

        // Sequence 240 = 0x000000F0.
        // Canonical BE bytes a client would expect: {0x00, 0x00, 0x00, 0xF0}.
        // Serialized layout: issuer(20) + marker(20) + sequence(4).
        MPTID const mptID240 = makeMptID(240, alice.id());

        // Before fix: wire sequence bytes are LE-swapped, not canonical.
        {
            STIssue const st(sfAsset, Asset{MPTIssue{mptID240}});
            Serializer s;
            st.add(s);
            Slice const sl = s.slice();
            BEAST_EXPECT(sl.size() == 44);
            BEAST_EXPECT(sl[40] == 0xF0);  // ← wrong: LSB of 0xF0000000 on LE
            BEAST_EXPECT(sl[41] == 0x00);
            BEAST_EXPECT(sl[42] == 0x00);
            BEAST_EXPECT(sl[43] == 0x00);
        }

        // After fix: wire sequence bytes are canonical BE.
        {
            std::unordered_set<uint256, beast::Uhash<>> const presets{fixCleanup3_2_0};
            CurrentTransactionRulesGuard const guard(Rules{presets});

            STIssue const st(sfAsset, Asset{MPTIssue{mptID240}});
            Serializer s;
            st.add(s);
            Slice const sl = s.slice();
            BEAST_EXPECT(sl.size() == 44);
            BEAST_EXPECT(sl[40] == 0x00);  // ← correct: MSB of 0x000000F0
            BEAST_EXPECT(sl[41] == 0x00);
            BEAST_EXPECT(sl[42] == 0x00);
            BEAST_EXPECT(sl[43] == 0xF0);
        }

        // 0xDEADBEEF: non-palindromic value makes the byte-order contrast
        // unambiguous regardless of host endianness.
        MPTID const mptIDBEEF = makeMptID(0xDEADBEEF, alice.id());

        // Before fix: {0xEF, 0xBE, 0xAD, 0xDE} — LE-swapped.
        {
            STIssue const st(sfAsset, Asset{MPTIssue{mptIDBEEF}});
            Serializer s;
            st.add(s);
            Slice const sl = s.slice();
            BEAST_EXPECT(sl[40] == 0xEF);
            BEAST_EXPECT(sl[41] == 0xBE);
            BEAST_EXPECT(sl[42] == 0xAD);
            BEAST_EXPECT(sl[43] == 0xDE);
        }

        // After fix: {0xDE, 0xAD, 0xBE, 0xEF} — canonical BE.
        {
            std::unordered_set<uint256, beast::Uhash<>> const presets{fixCleanup3_2_0};
            CurrentTransactionRulesGuard const guard(Rules{presets});

            STIssue const st(sfAsset, Asset{MPTIssue{mptIDBEEF}});
            Serializer s;
            st.add(s);
            Slice const sl = s.slice();
            BEAST_EXPECT(sl[40] == 0xDE);
            BEAST_EXPECT(sl[41] == 0xAD);
            BEAST_EXPECT(sl[42] == 0xBE);
            BEAST_EXPECT(sl[43] == 0xEF);
        }
    }

    // Cross-path test.
    //
    // Verifies that the STIssue codec (add/deserialize) agrees with the JSON
    // path (mptIssueFromJson) on the meaning of a raw MPTID.
    //
    // The sentinels (noAccount for V1, xrpAccount for V2) are internal codec
    // details. Clients only ever hold the raw 24-byte MPTID returned by RPC;
    // they never construct sentinel bytes themselves.
    //
    // Before fix (V1): the deserializer calls get32(), which byte-swaps the
    // canonical BE sequence bytes on LE hosts. The reconstructed MPTID does
    // not match the original — codec output diverges from JSON output.
    //
    // After fix (V2): the deserializer reads the sequence bytes raw. The
    // reconstructed MPTID matches exactly — codec and JSON paths agree.
    void
    testMPTCrossPath()
    {
        testcase(
            "MPT serialization - decoded MPTID matches canonical value: broken in V1, fixed in V2");
        using namespace jtx;
        Account const alice{"alice"};

        // Use a non-palindromic sequence so byte-swapping produces a visibly
        // different MPTID. seq=1 (0x00000001) would give 0x01000000 when
        // swapped; 0xDEADBEEF is unambiguous on any host.
        for (auto const seq : {240u, 0xDEADBEEFu, 1u})
        {
            MPTID const canonical = makeMptID(seq, alice.id());

            // The JSON path parses the hex string directly into an MPTID —
            // always canonical. This is the reference value that the codec
            // (add/deserialize round-trip) must agree with.
            json::Value jv;
            jv[jss::mpt_issuance_id] = to_string(canonical);
            MPTIssue const fromJson = mptIssueFromJson(jv);
            BEAST_EXPECT(fromJson.getMptID() == canonical);

            // Before fix: V1 codec writes [issuer][noAccount][add32(seq)].
            // Simulate the deserialization path with canonical (BE) sequence
            // bytes and V1 marker: get32() byte-swaps on LE, so the
            // reconstructed MPTID ≠ canonical and ≠ what mptIssueFromJson
            // produced.
            {
                Serializer s;
                s.addBitString(alice.id());
                s.addBitString(noAccount());
                s.addRaw(canonical.data(), sizeof(std::uint32_t));
                SerialIter sit(s.slice());
                STIssue const parsed(sit, sfAsset);
                BEAST_EXPECT(parsed != Asset{MPTIssue{canonical}});  // ← bug
                BEAST_EXPECT(parsed != Asset{fromJson});             // ← JSON/binary divergence
            }

            // After fix: V2 codec writes [issuer][xrpAccount][addRaw(seq)].
            // Same canonical (BE) sequence bytes, V2 marker: getRaw()
            // preserves bytes, so the reconstructed MPTID == canonical and
            // == what mptIssueFromJson produced.
            {
                Serializer s;
                s.addBitString(alice.id());
                s.addBitString(xrpAccount());
                s.addRaw(canonical.data(), sizeof(std::uint32_t));
                SerialIter sit(s.slice());
                STIssue const parsed(sit, sfAsset);
                BEAST_EXPECT(parsed == Asset{MPTIssue{canonical}});  // ← fixed
                BEAST_EXPECT(parsed == Asset{fromJson});             // ← JSON/binary agree
            }
        }

        // V1 round-trip (no amendment): xrpld's own add() and the
        // deserializer are symmetrically wrong, so they cancel and the MPTID
        // survives intact — the bug is invisible in internal round-trips.
        {
            for (auto const seq : {240u, 0xDEADBEEFu, 1u})
            {
                MPTID const expected = makeMptID(seq, alice.id());
                STIssue const original(sfAsset, Asset{MPTIssue{expected}});
                Serializer s;
                original.add(s);
                SerialIter sit(s.slice());
                BEAST_EXPECT(STIssue(sit, sfAsset) == Asset{MPTIssue{expected}});
            }
        }

        // V2 full round-trip (amendment enabled): add() and the deserializer
        // both use canonical bytes — round-trip is correct and canonical.
        {
            std::unordered_set<uint256, beast::Uhash<>> const presets{fixCleanup3_2_0};
            CurrentTransactionRulesGuard const guard(Rules{presets});

            for (auto const seq : {240u, 0xDEADBEEFu, 1u})
            {
                MPTID const expected = makeMptID(seq, alice.id());
                STIssue const original(sfAsset, Asset{MPTIssue{expected}});
                Serializer s;
                original.add(s);
                SerialIter sit(s.slice());
                BEAST_EXPECT(STIssue(sit, sfAsset) == Asset{MPTIssue{expected}});
            }
        }
    }

    void
    run() override
    {
        // compliments other unit tests to ensure complete coverage
        testConstructor();
        testCompare();
        testMPTWireFormat();
        testMPTCrossPath();
    }
};

BEAST_DEFINE_TESTSUITE(STIssue, protocol, xrpl);

}  // namespace xrpl::test
