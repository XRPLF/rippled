#pragma once

#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>

namespace xrpl {

template <class FwdIt>
std::string
strHex(FwdIt begin, FwdIt end)
{
    static_assert(
        std::is_convertible<typename std::iterator_traits<FwdIt>::iterator_category, std::forward_iterator_tag>::value,
        "FwdIt must be a forward iterator");
    std::string result;
    result.reserve(2 * std::distance(begin, end));
    boost::algorithm::hex(begin, end, std::back_inserter(result));
    return result;
}

template <class T, class = decltype(std::declval<T>().begin())>
std::string
strHex(T const& from)
{
    return strHex(from.begin(), from.end());
}

}  // namespace xrpl
