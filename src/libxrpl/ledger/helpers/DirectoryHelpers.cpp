#include <xrpl/ledger/helpers/DirectoryHelpers.h>
//
#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirFirst(view, root, page, index, entry);
}

bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry)
{
    return detail::internalDirNext(view, root, page, index, entry);
}

void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::forEachItem : valid root type");

    if (root.type != ltDIR_NODE)
        return;

    auto pos = root;

    while (true)
    {
        auto sle = view.read(pos);
        if (!sle)
            return;
        for (auto const& key : sle->getFieldV256(sfIndexes))
            f(view.read(keylet::child(key)));
        auto const next = sle->getFieldU64(sfIndexNext);
        if (next == 0u)
            return;
        pos = keylet::page(root, next);
    }
}

bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::forEachItemAfter : valid root type");

    if (root.type != ltDIR_NODE)
        return false;

    auto currentIndex = root;

    // If startAfter is not zero try jumping to that page using the hint
    if (after.isNonZero())
    {
        auto const hintIndex = keylet::page(root, hint);

        if (auto hintDir = view.read(hintIndex))
        {
            for (auto const& key : hintDir->getFieldV256(sfIndexes))
            {
                if (key == after)
                {
                    // We found the hint, we can start here
                    currentIndex = hintIndex;
                    break;
                }
            }
        }

        bool found = false;
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return found;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (!found)
                {
                    if (key == after)
                        found = true;
                }
                else if (f(view.read(keylet::child(key))) && limit-- <= 1)
                {
                    return found;
                }
            }

            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return found;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
    else
    {
        for (;;)
        {
            auto const ownerDir = view.read(currentIndex);
            if (!ownerDir)
                return true;
            for (auto const& key : ownerDir->getFieldV256(sfIndexes))
            {
                if (f(view.read(keylet::child(key))) && limit-- <= 1)
                    return true;
            }
            auto const uNodeNext = ownerDir->getFieldU64(sfIndexNext);
            if (uNodeNext == 0)
                return true;
            currentIndex = keylet::page(root, uNodeNext);
        }
    }
}

bool
dirIsEmpty(ReadView const& view, Keylet const& k)
{
    auto const sleNode = view.read(k);
    if (!sleNode)
        return true;
    if (!sleNode->getFieldV256(sfIndexes).empty())
        return false;
    // The first page of a directory may legitimately be empty even if there
    // are other pages (the first page is the anchor page) so check to see if
    // there is another page. If there is, the directory isn't empty.
    return sleNode->getFieldU64(sfIndexNext) == 0;
}

std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account)
{
    return [account](std::shared_ptr<SLE> const& sle) { (*sle)[sfOwner] = account; };
}

}  // namespace xrpl
