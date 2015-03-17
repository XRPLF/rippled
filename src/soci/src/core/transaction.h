//
// Copyright (C) 2004-2008 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_TRANSACTION_H_INCLUDED
#define SOCI_TRANSACTION_H_INCLUDED

#include "session.h"
#include "soci-config.h"

namespace soci
{

class SOCI_DECL transaction
{
public:
    explicit transaction(session& sql);

    ~transaction();

    void commit();
    void rollback();

private:
    bool handled_;
    session& sql_;

    // Disable copying
    transaction(transaction const& other);
    transaction& operator=(transaction const& other);
};

} // namespace soci

#endif // SOCI_TRANSACTION_H_INCLUDED
