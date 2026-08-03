#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>

#include <string>
#include <utility>
#include <vector>

namespace xrpl::test::jtx::delegate {

json::Value
set(jtx::Account const& account,
    jtx::Account const& authorize,
    std::vector<std::string> const& permissions);

json::Value
entry(jtx::Env& env, jtx::Account const& account, jtx::Account const& authorize);

struct As
{
private:
    jtx::Account delegate_;

public:
    explicit As(jtx::Account account) : delegate_(std::move(account))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfDelegate.jsonName] = delegate_.human();
    }
};

}  // namespace xrpl::test::jtx::delegate
