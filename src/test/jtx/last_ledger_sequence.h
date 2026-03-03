#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

struct last_ledger_seq
{
private:
    std::uint32_t num_;

public:
    explicit last_ledger_seq(std::uint32_t num) : num_(num)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
