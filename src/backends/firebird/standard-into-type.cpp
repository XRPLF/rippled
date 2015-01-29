//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"
#include "common.h"
#include <soci.h>

using namespace soci;
using namespace soci::details;
using namespace soci::details::firebird;

void firebird_standard_into_type_backend::define_by_pos(
    int & position, void * data, exchange_type type)
{
    position_ = position-1;
    data_ = data;
    type_ = type;

    ++position;

    statement_.intoType_ = eStandard;
    statement_.intos_.push_back(static_cast<void*>(this));

    XSQLVAR *var = statement_.sqldap_->sqlvar+position_;

    buf_ = allocBuffer(var);
    var->sqldata = buf_;
    var->sqlind = &indISCHolder_;
}

void firebird_standard_into_type_backend::pre_fetch()
{
    // nothing to do
}

void firebird_standard_into_type_backend::post_fetch(
    bool gotData, bool calledFromFetch, indicator * ind)
{
    if (calledFromFetch && (gotData == false))
    {
        // this is a normal end-of-rowset condition,
        // no need to set anything (fetch() will return false)
        return;
    }

    if (gotData)
    {
        if (i_null == statement_.inds_[position_][0] && NULL == ind)
        {
            throw soci_error("Null value fetched and no indicator defined.");
        }
        else if (NULL != ind)
        {
            *ind = statement_.inds_[position_][0];
        }
    }
}


void firebird_standard_into_type_backend::exchangeData()
{
    XSQLVAR *var = statement_.sqldap_->sqlvar+position_;

    switch (type_)
    {
            // simple cases
        case x_char:
            *reinterpret_cast<char*>(data_) = getTextParam(var)[0];
            break;
        case x_short:
            {
                short t = from_isc<short>(var);
                *reinterpret_cast<short*>(data_) = t;
            }
            break;
        case x_integer:
            {
                int t = from_isc<int>(var);
                *reinterpret_cast<int *>(data_) = t;
            }
            break;
        case x_long_long:
            {
                long long t = from_isc<long long>(var);
                *reinterpret_cast<long long *>(data_) = t;
            }
            break;
        case x_double:
            {
                double t = from_isc<double>(var);
                *reinterpret_cast<double*>(data_) = t;
            }
            break;

            // cases that require adjustments and buffer management
        case x_stdstring:
            *(reinterpret_cast<std::string*>(data_)) = getTextParam(var);
            break;
        case x_stdtm:
            tmDecode(var->sqltype,
                     buf_, static_cast<std::tm*>(data_));

            // isc_decode_timestamp() used by tmDecode() incorrectly sets
            // tm_isdst to 0 in the struct that it creates, see
            // http://tracker.firebirdsql.org/browse/CORE-3877, work around it
            // by pretending the DST is actually unknown.
            static_cast<std::tm*>(data_)->tm_isdst = -1;
            break;

            // cases that require special handling
        case x_blob:
            {
                blob *tmp = reinterpret_cast<blob*>(data_);

                firebird_blob_backend *blob =
                    dynamic_cast<firebird_blob_backend*>(tmp->get_backend());

                if (0 == blob)
                {
                    throw soci_error("Can't get Firebid BLOB BackEnd");
                }

                blob->assign(*reinterpret_cast<ISC_QUAD*>(buf_));
            }
            break;
        default:
            throw soci_error("Into element used with non-supported type.");
    } // switch
}

void firebird_standard_into_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
    std::vector<void*>::iterator it = 
        std::find(statement_.intos_.begin(), statement_.intos_.end(), this);
    if (it != statement_.intos_.end())
        statement_.intos_.erase(it);
}
