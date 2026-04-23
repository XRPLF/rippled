#pragma once

#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

struct invoice_id
{
private:
    uint256 hash_;

public:
    explicit invoice_id(uint256 const& hash) : hash_(hash)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};
}  // namespace xrpl::test::jtx
