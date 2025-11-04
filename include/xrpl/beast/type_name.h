#ifndef BEAST_TYPE_NAME_H_INCLUDED
#define BEAST_TYPE_NAME_H_INCLUDED

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
    using TR = typename std::remove_reference<T>::type;

    std::string name = typeid(TR).name();

#ifndef _MSC_VER
    if (auto s = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, nullptr))
    {
        name = s;
        std::free(s);
    }
#endif

    if (std::is_const<TR>::value)
        name += " const";
    if (std::is_volatile<TR>::value)
        name += " volatile";
    if (std::is_lvalue_reference<T>::value)
        name += "&";
    else if (std::is_rvalue_reference<T>::value)
        name += "&&";

    return name;
}

}  // namespace beast

#endif
