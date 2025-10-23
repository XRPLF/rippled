#ifndef XRPL_TEST_JTX_ACCOUNT_TXN_ID_H_INCLUDED
#define XRPL_TEST_JTX_ACCOUNT_TXN_ID_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

struct account_txn_id
{
private:
    uint256 hash_;

public:
    explicit account_txn_id(uint256 const& hash) : hash_(hash)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};
}  // namespace jtx
}  // namespace test
}  // namespace ripple
#endif
