#include <test/jtx/owners.h>

#include <test/jtx/Env.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
namespace xrpl {
namespace detail {

std::uint32_t
ownedCountOf(ReadView const& view, AccountID const& id, LedgerEntryType type)
{
    std::uint32_t count = 0;
    forEachItem(view, id, [&count, type](SLE::const_ref sle) {
        if (sle->getType() == type)
            ++count;
    });
    return count;
}

void
ownedCountHelper(
    test::jtx::Env& env,
    AccountID const& id,
    LedgerEntryType type,
    std::uint32_t value)
{
    env.test.expect(ownedCountOf(*env.current(), id, type) == value);
}

}  // namespace detail

namespace test::jtx {

void
Owners::operator()(Env& env) const
{
    env.test.expect(env.le(account_)->getFieldU32(sfOwnerCount) == value_);
}

}  // namespace test::jtx

}  // namespace xrpl
