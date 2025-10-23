#ifndef XRPL_JSON_JSON_ERRORS_H_INCLUDED
#define XRPL_JSON_JSON_ERRORS_H_INCLUDED

#include <stdexcept>

namespace Json {

struct error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

}  // namespace Json

#endif  // JSON_FORWARDS_H_INCLUDED
