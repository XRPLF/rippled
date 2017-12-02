//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_MYSQL_COMMON_H_INCLUDED
#define SOCI_MYSQL_COMMON_H_INCLUDED

#include "soci/mysql/soci-mysql.h"
#include "soci-cstrtod.h"
#include "soci-compiler.h"
// std
#include <cstddef>
#include <ctime>
#include <locale>
#include <sstream>
#include <vector>

namespace soci
{

namespace details
{

namespace mysql
{

// The idea is that infinity - infinity gives NaN, and NaN != NaN is true.
//
// This should work on any IEEE-754-compliant implementation, which is
// another way of saying that it does not always work (in particular,
// according to stackoverflow, it won't work with gcc with the --fast-math
// option), but I know of no better way of testing this portably in C++ prior
// to C++11.  When soci moves to C++11 this should be replaced
// with std::isfinite().
template <typename T>
bool is_infinity_or_nan(T x)
{
    T y = x - x;

    // We really need exact floating point comparison here.
    GCC_WARNING_SUPPRESS(float-equal)

    return (y != y);

    GCC_WARNING_RESTORE(float-equal)
}

template <typename T>
void parse_num(char const *buf, T &x)
{
    std::istringstream iss(buf);
    iss >> x;
    if (iss.fail() || (iss.eof() == false))
    {
        throw soci_error("Cannot convert data.");
    }
}

inline
void parse_num(char const *buf, double &x)
{
    x = cstring_to_double(buf);

    if (is_infinity_or_nan(x)) {
        throw soci_error(std::string("Cannot convert data: string \"") + buf +
                         "\" is not a finite number.");
    }
}

// helper for escaping strings
char * quote(MYSQL * conn, const char *s, size_t len);

// helper for vector operations
template <typename T>
std::size_t get_vector_size(void *p)
{
    std::vector<T> *v = static_cast<std::vector<T> *>(p);
    return v->size();
}

} // namespace mysql

} // namespace details

} // namespace soci

#endif // SOCI_MYSQL_COMMON_H_INCLUDED
