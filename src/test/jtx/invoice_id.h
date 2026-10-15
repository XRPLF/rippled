#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>

namespace xrpl::test::jtx {

struct InvoiceId
{
private:
    UInt256 hash_;

public:
    explicit InvoiceId(UInt256 const& hash) : hash_(hash)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};
}  // namespace xrpl::test::jtx
