#ifndef XRPL_TEST_JTX_LAST_LEDGER_SEQUENCE_H_INCLUDED
#define XRPL_TEST_JTX_LAST_LEDGER_SEQUENCE_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
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
}  // namespace ripple

#endif
