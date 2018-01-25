//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_H_INCLUDED
#define SOCI_H_INCLUDED

// namespace soci
#include "soci/soci-platform.h"
#include "soci/backend-loader.h"
#include "soci/blob.h"
#include "soci/blob-exchange.h"
#include "soci/column-info.h"
#include "soci/connection-pool.h"
#include "soci/error.h"
#include "soci/exchange-traits.h"
#include "soci/into.h"
#include "soci/into-type.h"
#include "soci/once-temp-type.h"
#include "soci/prepare-temp-type.h"
#include "soci/procedure.h"
#include "soci/ref-counted-prepare-info.h"
#include "soci/ref-counted-statement.h"
#include "soci/row.h"
#include "soci/row-exchange.h"
#include "soci/rowid.h"
#include "soci/rowid-exchange.h"
#include "soci/rowset.h"
#include "soci/session.h"
#include "soci/soci-backend.h"
#include "soci/statement.h"
#include "soci/transaction.h"
#include "soci/type-conversion.h"
#include "soci/type-conversion-traits.h"
#include "soci/type-holder.h"
#include "soci/type-ptr.h"
#include "soci/type-wrappers.h"
#include "soci/unsigned-types.h"
#include "soci/use.h"
#include "soci/use-type.h"
#include "soci/values.h"
#include "soci/values-exchange.h"

// namespace boost
#ifdef SOCI_USE_BOOST
#include <boost/version.hpp>
#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500
#include "soci/boost-fusion.h"
#endif // BOOST_VERSION
#include "soci/boost-optional.h"
#include "soci/boost-tuple.h"
#include "soci/boost-gregorian-date.h"
#endif // SOCI_USE_BOOST

#endif // SOCI_H_INCLUDED
