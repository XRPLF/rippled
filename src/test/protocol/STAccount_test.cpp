#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STAccount.h>

namespace ripple {

struct STAccount_test : public beast::unit_test::suite
{
    void
    testSTAccount()
    {
        {
            // Test default constructor.
            STAccount const defaultAcct;
            BEAST_EXPECT(defaultAcct.getSType() == STI_ACCOUNT);
            BEAST_EXPECT(defaultAcct.getText() == "");
            BEAST_EXPECT(defaultAcct.isDefault() == true);
            BEAST_EXPECT(defaultAcct.value() == AccountID{});
            {
#ifdef NDEBUG  // Qualified because the serialization asserts in a debug build.
                Serializer s;
                defaultAcct.add(s);  // Asserts in debug build
                BEAST_EXPECT(s.size() == 1);
                BEAST_EXPECT(strHex(s) == "00");
                SerialIter sit(s.slice());
                STAccount const deserializedDefault(sit, sfAccount);
                BEAST_EXPECT(deserializedDefault.isEquivalent(defaultAcct));
#endif  // NDEBUG
            }
            {
                // Construct a deserialized default STAccount.
                Serializer s;
                s.addVL(nullptr, 0);
                SerialIter sit(s.slice());
                STAccount const deserializedDefault(sit, sfAccount);
                BEAST_EXPECT(deserializedDefault.isEquivalent(defaultAcct));
            }

            // Test constructor from SField.
            STAccount const sfAcct{sfAccount};
            BEAST_EXPECT(sfAcct.getSType() == STI_ACCOUNT);
            BEAST_EXPECT(sfAcct.getText() == "");
            BEAST_EXPECT(sfAcct.isDefault());
            BEAST_EXPECT(sfAcct.value() == AccountID{});
            BEAST_EXPECT(sfAcct.isEquivalent(defaultAcct));
            {
                Serializer s;
                sfAcct.add(s);
                BEAST_EXPECT(s.size() == 1);
                BEAST_EXPECT(strHex(s) == "00");
                SerialIter sit(s.slice());
                STAccount const deserializedSf(sit, sfAccount);
                BEAST_EXPECT(deserializedSf.isEquivalent(sfAcct));
            }

            // Test constructor from SField and AccountID.
            STAccount const zeroAcct{sfAccount, AccountID{}};
            BEAST_EXPECT(zeroAcct.getText() == "rrrrrrrrrrrrrrrrrrrrrhoLvTp");
            BEAST_EXPECT(!zeroAcct.isDefault());
            BEAST_EXPECT(zeroAcct.value() == AccountID{0});
            BEAST_EXPECT(!zeroAcct.isEquivalent(defaultAcct));
            BEAST_EXPECT(!zeroAcct.isEquivalent(sfAcct));
            {
                Serializer s;
                zeroAcct.add(s);
                BEAST_EXPECT(s.size() == 21);
                BEAST_EXPECT(
                    strHex(s) == "140000000000000000000000000000000000000000");
                SerialIter sit(s.slice());
                STAccount const deserializedZero(sit, sfAccount);
                BEAST_EXPECT(deserializedZero.isEquivalent(zeroAcct));
            }
            {
                // Construct from a VL that is not exactly 160 bits.
                Serializer s;
                std::uint8_t const bits128[]{
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                s.addVL(bits128, sizeof(bits128));
                SerialIter sit(s.slice());
                try
                {
                    // Constructing an STAccount with a bad size should throw.
                    STAccount const deserializedBadSize(sit, sfAccount);
                }
                catch (std::runtime_error const& ex)
                {
                    BEAST_EXPECT(
                        ex.what() == std::string("Invalid STAccount size"));
                }
            }

            // Interestingly, equal values but different types are equivalent!
            STAccount const regKey{sfRegularKey, AccountID{}};
            BEAST_EXPECT(regKey.isEquivalent(zeroAcct));

            // Test assignment.
            STAccount assignAcct;
            BEAST_EXPECT(assignAcct.isEquivalent(defaultAcct));
            BEAST_EXPECT(assignAcct.isDefault());
            assignAcct = AccountID{};
            BEAST_EXPECT(!assignAcct.isEquivalent(defaultAcct));
            BEAST_EXPECT(assignAcct.isEquivalent(zeroAcct));
            BEAST_EXPECT(!assignAcct.isDefault());
        }
    }

    void
    testAccountID()
    {
        auto const s = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        if (auto const parsed = parseBase58<AccountID>(s); BEAST_EXPECT(parsed))
        {
            BEAST_EXPECT(toBase58(*parsed) == s);
        }

        {
            auto const s =
                "âabcd1rNxp4h8apvRis6mJf9Sh8C6iRxfrDWNâabcdAVâ\xc2\x80\xc2\x8f";
            BEAST_EXPECT(!parseBase58<AccountID>(s));
        }
    }

    void
    run() override
    {
        testSTAccount();
        testAccountID();
    }
};

BEAST_DEFINE_TESTSUITE(STAccount, protocol, ripple);

}  // namespace ripple
