//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_FIREBIRD_ERROR_H_INCLUDED
#define SOCI_FIREBIRD_ERROR_H_INCLUDED

#include "soci/firebird/soci-firebird.h"
#include <string>

namespace soci
{

namespace details
{

namespace firebird
{

void SOCI_FIREBIRD_DECL get_iscerror_details(ISC_STATUS * status_vector, std::string &msg);

bool SOCI_FIREBIRD_DECL check_iscerror(ISC_STATUS const * status_vector, long errNum);

void SOCI_FIREBIRD_DECL throw_iscerror(ISC_STATUS * status_vector);

} // namespace firebird

} // namespace details

} // namespace soci

#endif // SOCI_FIREBIRD_ERROR_H_INCLUDED
