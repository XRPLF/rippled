//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BLOB_H_INCLUDED
#define SOCI_BLOB_H_INCLUDED

#include "soci/soci-platform.h"
// std
#include <cstddef>

namespace soci
{
// basic blob operations

class session;

namespace details
{
class blob_backend;
} // namespace details

class SOCI_DECL blob
{
public:
    explicit blob(session & s);
    ~blob();

    std::size_t get_len();

    // offset is backend-specific
    std::size_t read(std::size_t offset, char * buf, std::size_t toRead);

    // offset starts from 0
    std::size_t read_from_start(char * buf, std::size_t toRead,
        std::size_t offset = 0);

    // offset is backend-specific
    std::size_t write(std::size_t offset, char const * buf,
        std::size_t toWrite);

    // offset starts from 0
    std::size_t write_from_start(const char * buf, std::size_t toWrite,
        std::size_t offset = 0);
    
    std::size_t append(char const * buf, std::size_t toWrite);
    
    void trim(std::size_t newLen);

    details::blob_backend * get_backend() { return backEnd_; }

private:
    details::blob_backend * backEnd_;
};

} // namespace soci

#endif
