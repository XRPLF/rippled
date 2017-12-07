# Supported Backends and Features

Follow the links to learn more about each backend and detailed supported features.

|Oracle|PostgreSQL|MySQL|SQLite3|Firebird|ODBC|DB2|
|--- |--- |--- |--- |--- |--- |--- |
|Binding by Name|YES|YES (>=8.0)|YES|YES|YES|YES|YES|
|Dynamic Binding|YES|YES|YES|YES|YES|YES|
|Bulk Operations|YES|YES|YES|YES|YES|YES|YES|
|Transactions|YES|YES|YES (>=4.0)|YES|YES|YES|YES|
|BLOB Data Type|YES|YES|YES (mapped to `std::string`)|YES|YES|NO|NO|
|RowID Data Type|YES|YES|NO|NO|NO|NO|NO|
|Nested Statements|YES|NO|NO|NO|NO|NO|YES|
|Stored Procedures|YES|YES|NO (but stored functions, YES)|NO|YES|NO|YES|
