//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci/firebird/soci-firebird.h"
#include "soci-exchange-cast.h"
#include "firebird/common.h"
#include "soci/soci.h"

#include <sstream>

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
            exchange_type_cast<x_char>(data_) = getTextParam(var)[0];
            break;
        case x_short:
            exchange_type_cast<x_short>(data_) = from_isc<short>(var);
            break;
        case x_integer:
            exchange_type_cast<x_integer>(data_) = from_isc<int>(var);
            break;
        case x_long_long:
            exchange_type_cast<x_long_long>(data_) = from_isc<long long>(var);
            break;
        case x_double:
            exchange_type_cast<x_double>(data_) = from_isc<double>(var);
            break;

            // cases that require adjustments and buffer management
        case x_stdstring:
            exchange_type_cast<x_stdstring>(data_) = getTextParam(var);
            break;
        case x_stdtm:
            {
                std::tm& t = exchange_type_cast<x_stdtm>(data_);
                tmDecode(var->sqltype, buf_, &t);

                // isc_decode_timestamp() used by tmDecode() incorrectly sets
                // tm_isdst to 0 in the struct that it creates, see
                // http://tracker.firebirdsql.org/browse/CORE-3877, work around it
                // by pretending the DST is actually unknown.
                t.tm_isdst = -1;
            }
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

        case x_longstring:
            copy_from_blob(exchange_type_cast<x_longstring>(data_).value);
            break;

        case x_xmltype:
            copy_from_blob(exchange_type_cast<x_xmltype>(data_).value);
            break;

        default:
            throw soci_error("Into element used with non-supported type.");
    } // switch
}

void firebird_standard_into_type_backend::copy_from_blob(std::string& out)
{
    firebird_blob_backend blob(statement_.session_);
    blob.assign(*reinterpret_cast<ISC_QUAD*>(buf_));

    std::size_t const len_total = blob.get_len();
    out.resize(len_total);

    std::size_t const len_read = blob.read(0, &out[0], len_total);
    if (len_read != len_total)
    {
        std::ostringstream os;
        os << "Read " << len_read << " bytes instead of expected "
           << len_total << " from Firebird text blob object";
        throw soci_error(os.str());
    }
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
