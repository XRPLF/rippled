#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

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

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
