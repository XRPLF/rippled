#ifndef XRPL_TEST_JTX_REQUIRES_H_INCLUDED
#define XRPL_TEST_JTX_REQUIRES_H_INCLUDED

#include <functional>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

class Env;

using require_t = std::function<void(Env&)>;
using requires_t = std::vector<require_t>;

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
