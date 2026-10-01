#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <cstdint>

namespace xrpl::test::jtx {

struct LastLedgerSeq
{
private:
    std::uint32_t num_;

public:
    explicit LastLedgerSeq(std::uint32_t num) : num_(num)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
