#pragma once

#include <cstdlib>
#include <string>
#include <type_traits>
#include <typeinfo>

#ifndef _MSC_VER
#include <cxxabi.h>
#endif

namespace beast {

template <typename T>
std::string
type_name()
{
    using TR = std::remove_reference_t<T>;

    std::string name = typeid(TR).name();

#ifndef _MSC_VER
    if (auto s = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, nullptr))
    {
        name = s;
        std::free(s);
    }
#endif

    if (std::is_const_v<TR>)
        name += " const";
    if (std::is_volatile_v<TR>)
        name += " volatile";
    if (std::is_lvalue_reference_v<T>)
    {
        name += "&";
    }
    else if (std::is_rvalue_reference_v<T>)
    {
        name += "&&";
    }

    return name;
}

}  // namespace beast
