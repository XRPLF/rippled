//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ROWID_H_INCLUDED
#define SOCI_ROWID_H_INCLUDED

#include "soci-config.h"

namespace soci
{

class session;

namespace details
{

class rowid_backend;

} // namespace details

// ROWID support

class SOCI_DECL rowid
{
public:
    explicit rowid(session & s);
    ~rowid();

    details::rowid_backend * get_backend() { return backEnd_; }

private:
    details::rowid_backend *backEnd_;
};

} // namespace soci

#endif
