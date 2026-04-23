#pragma once

#include <functional>
#include <vector>

namespace xrpl {
namespace test {
namespace jtx {

class Env;

using require_t = std::function<void(Env&)>;
using requires_t = std::vector<require_t>;

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
