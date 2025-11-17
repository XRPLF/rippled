#include <test/jtx/owners.h>

namespace ripple {
namespace detail {

std::uint32_t
owned_count_of(ReadView const& view, AccountID const& id, LedgerEntryType type)
{
    std::uint32_t count = 0;
    forEachItem(
        view, id, [&count, type](std::shared_ptr<SLE const> const& sle) {
            if (sle->getType() == type)
                ++count;
        });
    return count;
}

void
owned_count_helper(
    test::jtx::Env& env,
    AccountID const& id,
    LedgerEntryType type,
    std::uint32_t value)
{
    env.test.expect(owned_count_of(*env.current(), id, type) == value);
}

}  // namespace detail

namespace test {
namespace jtx {

void
owners::operator()(Env& env) const
{
    env.test.expect(env.le(account_)->getFieldU32(sfOwnerCount) == value_);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
