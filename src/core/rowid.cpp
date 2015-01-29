//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "rowid.h"
#include "session.h"

using namespace soci;
using namespace soci::details;

rowid::rowid(session & s)
{
    backEnd_ = s.make_rowid_backend();
}

rowid::~rowid()
{
    delete backEnd_;
}
