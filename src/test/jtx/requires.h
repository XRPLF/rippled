#pragma once

#include <functional>
#include <vector>

namespace xrpl::test::jtx {

class Env;

using RequireT = std::function<void(Env&)>;
using RequiresT = std::vector<RequireT>;

}  // namespace xrpl::test::jtx
