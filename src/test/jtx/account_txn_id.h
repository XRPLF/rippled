#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>

namespace xrpl::test::jtx {

struct AccountTxnId
{
private:
    uint256 hash_;

public:
    explicit AccountTxnId(uint256 const& hash) : hash_(hash)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};
}  // namespace xrpl::test::jtx
