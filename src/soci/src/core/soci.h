//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_H_INCLUDED
#define SOCI_H_INCLUDED

#ifdef _MSC_VER
#pragma warning(disable:4251 4512 4511)
#endif

// namespace soci
#include "backend-loader.h"
#include "blob.h"
#include "blob-exchange.h"
#include "connection-pool.h"
#include "error.h"
#include "exchange-traits.h"
#include "into.h"
#include "into-type.h"
#include "once-temp-type.h"
#include "prepare-temp-type.h"
#include "procedure.h"
#include "ref-counted-prepare-info.h"
#include "ref-counted-statement.h"
#include "row.h"
#include "row-exchange.h"
#include "rowid.h"
#include "rowid-exchange.h"
#include "rowset.h"
#include "session.h"
#include "soci-backend.h"
#include "soci-config.h"
#include "soci-platform.h"
#include "statement.h"
#include "transaction.h"
#include "type-conversion.h"
#include "type-conversion-traits.h"
#include "type-holder.h"
#include "type-ptr.h"
#include "unsigned-types.h"
#include "use.h"
#include "use-type.h"
#include "values.h"
#include "values-exchange.h"

// namespace boost
#ifdef SOCI_USE_BOOST
#include <boost/version.hpp>
#if defined(BOOST_VERSION) && BOOST_VERSION >= 103500
#include "boost-fusion.h"
#endif // BOOST_VERSION
#include "boost-optional.h"
#include "boost-tuple.h"
#include "boost-gregorian-date.h"
#endif // SOCI_USE_BOOST

#endif // SOCI_H_INCLUDED
