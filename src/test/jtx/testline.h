#ifndef XRPL_TEST_JTX_TESTLINE_H_INCLUDED
#define XRPL_TEST_JTX_TESTLINE_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Store the line number of the current test in a JTx.

    Intended to help debug failing transaction submission tests.
*/
class testline
{
private:
    int line_;

public:
    explicit testline(int line) : line_(line)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

#define THISLINE testline(__LINE__)

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
