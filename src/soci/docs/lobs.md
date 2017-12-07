# Large Objects (LOBs)

## Binary (BLOBs)

The SOCI library provides also an interface for basic operations on large objects (BLOBs - Binary Large OBjects).

    blob b(sql); // sql is a session object
    sql << "select mp3 from mymusic where id = 123", into(b);

The following functions are provided in the `blob` interface, mimicking the file-like operations:

* `std::size_t get_len();`
* `std::size_t read_from_start(char * buf, std::size_t toRead, std::size_t offset = 0);`
* `std::size_t write_from_start(const char * buf, std::size_t toWrite, std::size_t offset = 0);`
* `std::size_t append(char const *buf, std::size_t toWrite);`
* `void trim(std::size_t newLen);`

The `offset` parameter is always counted from the beginning of the BLOB's data.

### Portability notes

* The way to define BLOB table columns and create or destroy BLOB objects in the database varies between different database engines.
  Please see the SQL documentation relevant for the given server to learn how this is actually done. The test programs provided with the SOCI library can be also a simple source of full working examples.
* The `trim` function is not currently available for the PostgreSQL backend.

## Long strings and XML

The SOCI library recognizes the fact that long string values are not handled portably and in some databases long string values need to be stored as a different data type.
Similar concerns relate to the use of XML values, which are essentially strings at the application level, but can be stored in special database-level field types.

In order to facilitate handling of long strings and XML values the following wrapper types are defined:

    struct xml_type
    {
        std::string value;
    };

    struct long_string
    {
        std::string value;
    };

Values of these wrapper types can be used with `into` and `use` elements with the database target type that is specifically intended to handle XML and long strings data types.

For Oracle, these database-side types are, respectively:

* `XMLType`,
* `CLOB`

For PostgreSQL, these types are:

* `XML`
* `text`

For Firebird, there is no special XML support, but `BLOB SUB_TYPE TEXT` can be
used for storing it, as well as long strings.

For ODBC backend, these types depend on the type of the database connected to.
In particularly important special case of Microsoft SQL Server, these types
are:

* `xml`
* `text`

When using ODBC backend to connect to a PostgreSQL database, please be aware
that by default PostgreSQL ODBC driver truncates all "unknown" types, such as
XML, to maximal varchar type size which is just 256 bytes and so is often
insufficient for XML values in practice. It is advised to set the
`UnknownsAsLongVarchar` connection option to 1 to avoid truncating XML strings
or use PostgreSQL ODBC driver 9.6.300 or later, which allows the backend to set
this option to 1 automatically on connection.
