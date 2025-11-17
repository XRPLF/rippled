#ifndef BEAST_CONTAINER_AGED_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_AGED_CONTAINER_H_INCLUDED

#include <type_traits>

namespace beast {

template <class T>
struct is_aged_container : std::false_type
{
    explicit is_aged_container() = default;
};

}  // namespace beast

#endif
