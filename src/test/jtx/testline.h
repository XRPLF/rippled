#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
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
}  // namespace xrpl
