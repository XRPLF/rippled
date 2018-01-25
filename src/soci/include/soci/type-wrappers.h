//
// Copyright (C) 2016 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_TYPE_WRAPPERS_H_INCLUDED
#define SOCI_TYPE_WRAPPERS_H_INCLUDED

namespace soci
{

// These wrapper types can be used by the application
// with 'into' and 'use' elements in order to guide the library
// in selecting specialized methods for binding and transferring data;
// if the target database does not provide any such specialized methods,
// it is expected to handle these wrappers as equivalent
// to their contained field types.

struct xml_type
{
    std::string value;
};

struct long_string
{
    std::string value;
};

} // namespace soci

#endif // SOCI_TYPE_WRAPPERS_H_INCLUDED
