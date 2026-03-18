#include <test/jtx/Env.h>

#include <xrpld/app/ledger/Ledger.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/ledger/View.h>

namespace xrpl {
namespace test {

class SkipList_test : public beast::unit_test::suite
{
    void
    testSkipList()
    {
        jtx::Env env(*this);
        std::vector<std::shared_ptr<Ledger>> history;
        {
            Config config;
            auto prev = std::make_shared<Ledger>(
                create_genesis, config, std::vector<uint256>{}, env.app().getNodeFamily());
            history.push_back(prev);
            for (auto i = 0; i < 1023; ++i)
            {
                auto next = std::make_shared<Ledger>(*prev, env.app().timeKeeper().closeTime());
                next->updateSkipList();
                history.push_back(next);
                prev = next;
            }
        }

        {
            auto l = *(std::next(std::begin(history)));
            BEAST_EXPECT((*std::begin(history))->header().seq < l->header().seq);
            BEAST_EXPECT(!hashOfSeq(*l, l->header().seq + 1, env.journal).has_value());
            BEAST_EXPECT(hashOfSeq(*l, l->header().seq, env.journal) == l->header().hash);
            BEAST_EXPECT(hashOfSeq(*l, l->header().seq - 1, env.journal) == l->header().parentHash);
            BEAST_EXPECT(!hashOfSeq(*history.back(), l->header().seq, env.journal).has_value());
        }

        // ledger skip lists store up to the previous 256 hashes
        for (auto i = history.crbegin(); i != history.crend(); i += 256)
        {
            for (auto n = i; n != std::next(i, (*i)->header().seq - 256 > 1 ? 257 : 256); ++n)
            {
                BEAST_EXPECT(
                    hashOfSeq(**i, (*n)->header().seq, env.journal) == (*n)->header().hash);
            }

            // edge case accessing beyond 256
            BEAST_EXPECT(!hashOfSeq(**i, (*i)->header().seq - 258, env.journal).has_value());
        }

        // every 256th hash beyond the first 256 is stored
        for (auto i = history.crbegin(); i != std::next(history.crend(), -512); i += 256)
        {
            for (auto n = std::next(i, 512); n != history.crend(); n += 256)
            {
                BEAST_EXPECT(
                    hashOfSeq(**i, (*n)->header().seq, env.journal) == (*n)->header().hash);
            }
        }
    }

    void
    run() override
    {
        testSkipList();
    }
};

BEAST_DEFINE_TESTSUITE(SkipList, ledger, xrpl);

}  // namespace test
}  // namespace xrpl
