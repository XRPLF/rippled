#ifndef XRPL_TEST_JTX_NOOP_H_INCLUDED
#define XRPL_TEST_JTX_NOOP_H_INCLUDED

#include <test/jtx/flags.h>

namespace ripple {
namespace test {
namespace jtx {

/** The null transaction. */
inline Json::Value
noop(Account const& account)
{
    return fset(account, 0);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
