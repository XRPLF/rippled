#include <test/jtx/directory.h>

#include <xrpl/ledger/Sandbox.h>

namespace xrpl::test::jtx {

/** Directory operations. */
namespace directory {

std::size_t
getIndexCountToBump(Env& env, Keylet directory)
{
    std::size_t toAdd = 0;
    env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) -> bool {
        Sandbox sb(&view, tapNONE);

        // Find the root page
        auto sleRoot = sb.peek(directory);
        if (!sleRoot)
        {
            toAdd += 64;
            return false;
        }

        auto const lastIndex0 = sleRoot->getFieldU64(sfIndexPrevious);
        auto slePage0 = sb.peek(keylet::page(directory, lastIndex0));
        if (!slePage0)
        {
            return false;
        }
        auto indexes0 = slePage0->getFieldV256(sfIndexes);
        if (lastIndex0 == 0)
            toAdd += 32;
        if (indexes0.size() < 32)
            toAdd += 32 - indexes0.size();
        return false;
    });
    return toAdd;
}

auto
bumpLastPage(
    Env& env,
    std::uint64_t newLastPage,
    Keylet directory,
    std::function<bool(ApplyView&, uint256, std::uint64_t)> adjust) -> Expected<void, Error>
{
    Expected<void, Error> res{};
    env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) -> bool {
        Sandbox sb(&view, tapNONE);

        // Find the root page
        auto sleRoot = sb.peek(directory);
        if (!sleRoot)
        {
            res = Unexpected<Error>(DirectoryRootNotFound);
            return false;
        }

        // Find last page
        auto const lastIndex = sleRoot->getFieldU64(sfIndexPrevious);
        if (lastIndex == 0)
        {
            res = Unexpected<Error>(DirectoryTooSmall);
            return false;
        }

        if (sb.exists(keylet::page(directory, newLastPage)))
        {
            res = Unexpected<Error>(DirectoryPageDuplicate);
            return false;
        }

        if (lastIndex >= newLastPage)
        {
            res = Unexpected<Error>(InvalidLastPage);
            return false;
        }

        auto slePage = sb.peek(keylet::page(directory, lastIndex));
        if (!slePage)
        {
            res = Unexpected<Error>(DirectoryPageNotFound);
            return false;
        }

        // Copy its data and delete the page
        auto indexes = slePage->getFieldV256(sfIndexes);
        auto prevIndex = slePage->at(~sfIndexPrevious);
        auto owner = slePage->at(~sfOwner);
        sb.erase(slePage);

        // Create new page to replace slePage
        auto sleNew = std::make_shared<SLE>(keylet::page(directory, newLastPage));
        sleNew->setFieldH256(sfRootIndex, directory.key);
        sleNew->setFieldV256(sfIndexes, indexes);
        if (owner)
            sleNew->setAccountID(sfOwner, *owner);
        if (prevIndex)
            sleNew->setFieldU64(sfIndexPrevious, *prevIndex);
        sb.insert(sleNew);

        // Adjust root previous and previous node's next
        sleRoot->setFieldU64(sfIndexPrevious, newLastPage);
        if (prevIndex.value_or(0) == 0)
        {
            sleRoot->setFieldU64(sfIndexNext, newLastPage);
        }
        else
        {
            auto slePrev = sb.peek(keylet::page(directory, *prevIndex));
            if (!slePrev)
            {
                res = Unexpected<Error>(DirectoryPageNotFound);
                return false;
            }
            slePrev->setFieldU64(sfIndexNext, newLastPage);
            sb.update(slePrev);
        }
        sb.update(sleRoot);

        // Fixup page numbers in the objects referred by indexes
        if (adjust)
        {
            for (auto const key : indexes)
            {
                if (!adjust(sb, key, newLastPage))
                {
                    res = Unexpected<Error>(AdjustmentError);
                    return false;
                }
            }
        }

        sb.apply(view);
        return true;
    });

    return res;
}

bool
adjustOwnerNode(ApplyView& view, uint256 key, std::uint64_t page)
{
    auto sle = view.peek({ltANY, key});
    if (sle && sle->isFieldPresent(sfOwnerNode))
    {
        sle->setFieldU64(sfOwnerNode, page);
        view.update(sle);
        return true;
    }

    return false;
}

}  // namespace directory

}  // namespace xrpl::test::jtx
