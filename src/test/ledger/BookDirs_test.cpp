
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/BookDirs.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>

#include <iterator>
#include <optional>

namespace xrpl::test {

struct BookDirs_test : public beast::unit_test::suite
{
    void
    test_bookdir(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);
        auto gw = Account("gw");
        auto USD = gw["USD"];
        env.fund(XRP(1000000), "alice", "bob", "gw");
        env.close();

        {
            Book const book(xrpIssue(), USD, std::nullopt);
            {
                auto d = BookDirs(*env.current(), book);
                BEAST_EXPECT(std::begin(d) == std::end(d));
                BEAST_EXPECT(std::distance(d.begin(), d.end()) == 0);
            }
            {
                auto d = BookDirs(*env.current(), reversed(book));
                BEAST_EXPECT(std::distance(d.begin(), d.end()) == 0);
            }
        }

        {
            env(offer("alice", Account("alice")["USD"](50), XRP(10)));
            auto d =
                BookDirs(*env.current(), Book(Account("alice")["USD"], xrpIssue(), std::nullopt));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            env(offer("alice", gw["CNY"](50), XRP(10)));
            auto d = BookDirs(*env.current(), Book(gw["CNY"], xrpIssue(), std::nullopt));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            env.trust(Account("bob")["CNY"](10), "alice");
            env(pay("bob", "alice", Account("bob")["CNY"](10)));
            env(offer("alice", USD(50), Account("bob")["CNY"](10)));
            auto d = BookDirs(*env.current(), Book(USD, Account("bob")["CNY"], std::nullopt));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            auto AUD = gw["AUD"];
            for (auto i = 1, j = 3; i <= 3; ++i, --j)
            {
                for (auto k = 0; k < 80; ++k)
                    env(offer("alice", AUD(i), XRP(j)));
            }

            auto d = BookDirs(*env.current(), Book(AUD, xrpIssue(), std::nullopt));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 240);
            auto i = 1, j = 3, k = 0;
            for (auto const& e : d)
            {
                BEAST_EXPECT(e->getFieldAmount(sfTakerPays) == AUD(i));
                BEAST_EXPECT(e->getFieldAmount(sfTakerGets) == XRP(j));
                if (++k % 80 == 0)
                {
                    ++i;
                    --j;
                }
            }
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testable_amendments();
        test_bookdir(sa - featurePermissionedDEX);
        test_bookdir(sa);
    }
};

BEAST_DEFINE_TESTSUITE(BookDirs, ledger, xrpl);

}  // namespace xrpl::test
