#pragma once

#include <stdexcept>

namespace Json {

struct error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

}  // namespace Json
