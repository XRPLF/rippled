#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

namespace delegate {

Json::Value
set(jtx::Account const& account,
    jtx::Account const& authorize,
    std::vector<std::string> const& permissions);

Json::Value
entry(
    jtx::Env& env,
    jtx::Account const& account,
    jtx::Account const& authorize);

struct as
{
private:
    jtx::Account delegate_;

public:
    explicit as(jtx::Account const& account) : delegate_(account)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfDelegate.jsonName] = delegate_.human();
    }
};

}  // namespace delegate
}  // namespace jtx
}  // namespace test
}  // namespace ripple
