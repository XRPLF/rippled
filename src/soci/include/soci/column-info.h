//
// Copyright (C) 2016 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_COLUMN_INFO_H_INCLUDED
#define SOCI_COLUMN_INFO_H_INCLUDED

#include "soci/soci-backend.h"
#include "soci/type-conversion.h"
#include "soci/values.h"

namespace soci
{

struct SOCI_DECL column_info
{
    std::string name;
    data_type type;
    std::size_t length; // meaningful for text columns only
    std::size_t precision;
    std::size_t scale;
    bool nullable;
};

template <>
struct type_conversion<column_info>
{
    typedef values base_type;
    
    static std::size_t get_numeric_value(const values & v,
        const std::string & field_name)
    {
        data_type dt = v.get_properties(field_name).get_data_type();
        switch (dt)
        {
        case dt_double:
            return static_cast<std::size_t>(
                v.get<double>(field_name, 0.0));
        case dt_integer:
            return static_cast<std::size_t>(
                v.get<int>(field_name, 0));
        case dt_long_long:
            return static_cast<std::size_t>(
                v.get<long long>(field_name, 0ll));
        case dt_unsigned_long_long:
            return static_cast<std::size_t>(
                v.get<unsigned long long>(field_name, 0ull));
            break;
        default:
            return 0u;
        }        
    }

    static void from_base(values const & v, indicator /* ind */, column_info & ci)
    {
        ci.name = v.get<std::string>("COLUMN_NAME");

        ci.length = get_numeric_value(v, "CHARACTER_MAXIMUM_LENGTH");
        ci.precision = get_numeric_value(v, "NUMERIC_PRECISION");
        ci.scale = get_numeric_value(v, "NUMERIC_SCALE");

        const std::string & type_name = v.get<std::string>("DATA_TYPE");
        if (type_name == "text" || type_name == "TEXT" ||
            type_name == "clob" || type_name == "CLOB" ||
            type_name.find("char") != std::string::npos ||
            type_name.find("CHAR") != std::string::npos)
        {
            ci.type = dt_string;
        }
        else if (type_name == "integer" || type_name == "INTEGER")
        {
            ci.type = dt_integer;
        }
        else if (type_name.find("number") != std::string::npos ||
            type_name.find("NUMBER") != std::string::npos ||
            type_name.find("numeric") != std::string::npos ||
            type_name.find("NUMERIC") != std::string::npos)
        {
            if (ci.scale != 0)
            {
                ci.type = dt_double;
            }
            else
            {
                ci.type = dt_integer;
            }
        }
        else if (type_name.find("time") != std::string::npos ||
            type_name.find("TIME") != std::string::npos ||
            type_name.find("date") != std::string::npos ||
            type_name.find("DATE") != std::string::npos)
        {
            ci.type = dt_date;
        }
        else if (type_name.find("blob") != std::string::npos ||
            type_name.find("BLOB") != std::string::npos ||
            type_name.find("oid") != std::string::npos ||
            type_name.find("OID") != std::string::npos)
        {
            ci.type = dt_blob;
        }
        else if (type_name.find("xml") != std::string::npos ||
            type_name.find("XML") != std::string::npos)
        {
            ci.type = dt_xml;
        }
        else
        {
            // this seems to be a safe default
            ci.type = dt_string;
        }

        const std::string & nullable_s = v.get<std::string>("IS_NULLABLE");
        ci.nullable = (nullable_s == "YES");
    }
};

} // namespace soci

#endif // SOCI_COLUMN_INFO_H_INCLUDED
