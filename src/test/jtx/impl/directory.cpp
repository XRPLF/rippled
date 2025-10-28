//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx/directory.h>

#include <xrpl/ledger/Sandbox.h>

namespace ripple::test::jtx {

/** Directory operations. */
namespace directory {

auto
bumpLastPage(
    Env& env,
    std::uint64_t lastPage,
    Keylet directory,
    std::function<bool(ApplyView&, uint256, std::uint64_t)> adjust)
    -> Expected<void, Error>
{
    Expected<void, Error> res{};
    env.app().openLedger().modify(
        [&](OpenView& view, beast::Journal j) -> bool {
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

            if (sb.exists(keylet::page(directory, lastPage)))
            {
                res = Unexpected<Error>(DirectoryPageDuplicate);
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
            auto sleNew =
                std::make_shared<SLE>(keylet::page(directory, lastPage));
            sleNew->setFieldH256(sfRootIndex, directory.key);
            sleNew->setFieldV256(sfIndexes, indexes);
            if (owner)
                sleNew->setAccountID(sfOwner, *owner);
            if (prevIndex)
                sleNew->setFieldU64(sfIndexPrevious, *prevIndex);
            sb.insert(sleNew);

            // Adjust root previous and previous node's next
            sleRoot->setFieldU64(sfIndexPrevious, lastPage);
            if (prevIndex.value_or(0) == 0)
                sleRoot->setFieldU64(sfIndexNext, lastPage);
            else
            {
                auto slePrev = sb.peek(keylet::page(directory, *prevIndex));
                if (!slePrev)
                {
                    res = Unexpected<Error>(DirectoryPageNotFound);
                    return false;
                }
                slePrev->setFieldU64(sfIndexNext, lastPage);
                sb.update(slePrev);
            }
            sb.update(sleRoot);

            // Fixup page numbers in the objects referred by indexes
            if (adjust)
                for (auto const key : indexes)
                {
                    if (!adjust(sb, key, lastPage))
                    {
                        res = Unexpected<Error>(AdjustmentError);
                        return false;
                    }
                }

            sb.apply(view);
            return true;
        });

    return res;
}
}  // namespace directory

}  // namespace ripple::test::jtx
