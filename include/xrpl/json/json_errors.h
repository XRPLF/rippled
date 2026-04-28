#pragma once

#include <stdexcept>

namespace Json {

struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

}  // namespace Json
