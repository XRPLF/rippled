//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BLOB_EXCHANGE_H_INCLUDED
#define SOCI_BLOB_EXCHANGE_H_INCLUDED

#include "soci/blob.h"
#include "soci/into-type.h"
#include "soci/use-type.h"
// std
#include <string>

namespace soci
{

namespace details
{

template <>
class into_type<blob> : public standard_into_type
{
public:
    into_type(blob & b) : standard_into_type(&b, x_blob) {}
    into_type(blob & b, indicator & ind)
        : standard_into_type(&b, x_blob, ind) {}
};

template <>
class use_type<blob> : public standard_use_type
{
public:
    use_type(blob & b, std::string const & name = std::string())
        : standard_use_type(&b, x_blob, false, name) {}
    use_type(blob const & b, std::string const & name = std::string())
        : standard_use_type(const_cast<blob *>(&b), x_blob, true, name) {}
    use_type(blob & b, indicator & ind,
        std::string const & name = std::string())
        : standard_use_type(&b, x_blob, ind, false, name) {}
    use_type(blob const & b, indicator & ind,
        std::string const & name = std::string())
        : standard_use_type(const_cast<blob *>(&b), x_blob, ind, true, name) {}
};

template <>
struct exchange_traits<soci::blob>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_blob };
};

} // namespace details

} // namespace soci

#endif // SOCI_BLOB_EXCHANGE_H_INCLUDED
