//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "blob.h"
#include "session.h"

#include <cstddef>

using namespace soci;

blob::blob(session & s)
{
    backEnd_ = s.make_blob_backend();
}

blob::~blob()
{
    delete backEnd_;
}

std::size_t blob::get_len()
{
    return backEnd_->get_len();
}

std::size_t blob::read(std::size_t offset, char *buf, std::size_t toRead)
{
    return backEnd_->read(offset, buf, toRead);
}

std::size_t blob::write(
    std::size_t offset, char const * buf, std::size_t toWrite)
{
    return backEnd_->write(offset, buf, toWrite);
}

std::size_t blob::append(char const * buf, std::size_t toWrite)
{
    return backEnd_->append(buf, toWrite);
}

void blob::trim(std::size_t newLen)
{
    backEnd_->trim(newLen);
}
