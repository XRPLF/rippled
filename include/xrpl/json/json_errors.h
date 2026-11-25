#pragma once

#include <stdexcept>

namespace json {

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

}  // namespace json
