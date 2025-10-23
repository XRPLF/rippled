#ifndef XRPL_TEST_JTX_INVOICE_ID_H_INCLUDED
#define XRPL_TEST_JTX_INVOICE_ID_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

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
}  // namespace jtx
}  // namespace test
}  // namespace ripple
#endif
