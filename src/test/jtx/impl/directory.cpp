#include <test/jtx/directory.h>

#include <test/jtx/Env.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>

/** Directory operations. */
namespace xrpl::test::jtx::directory {

auto
bumpLastPage(
    Env& env,
    std::uint64_t newLastPage,
    Keylet directory,
    std::function<bool(ApplyView&, uint256, std::uint64_t)> adjust) -> std::expected<void, Error>
{
    std::expected<void, Error> res{};
    env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) -> bool {
        Sandbox sb(&view, TapNone);

        // Find the root page
        auto sleRoot = sb.peek(directory);
        if (!sleRoot)
        {
            res = std::unexpected<Error>(Error::DirectoryRootNotFound);
            return false;
        }

        // Find last page
        auto const lastIndex = sleRoot->getFieldU64(sfIndexPrevious);
        if (lastIndex == 0)
        {
            res = std::unexpected<Error>(Error::DirectoryTooSmall);
            return false;
        }

        if (sb.exists(keylet::page(directory, newLastPage)))
        {
            res = std::unexpected<Error>(Error::DirectoryPageDuplicate);
            return false;
        }

        if (lastIndex >= newLastPage)
        {
            res = std::unexpected<Error>(Error::InvalidLastPage);
            return false;
        }

        auto slePage = sb.peek(keylet::page(directory, lastIndex));
        if (!slePage)
        {
            res = std::unexpected<Error>(Error::DirectoryPageNotFound);
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
        if (prevIndex.valueOr(0) == 0)
        {
            sleRoot->setFieldU64(sfIndexNext, newLastPage);
        }
        else
        {
            auto slePrev = sb.peek(keylet::page(directory, *prevIndex));
            if (!slePrev)
            {
                res = std::unexpected<Error>(Error::DirectoryPageNotFound);
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
                    res = std::unexpected<Error>(Error::AdjustmentError);
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

}  // namespace xrpl::test::jtx::directory
