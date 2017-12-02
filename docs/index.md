# SOCI - The C++ Database Access Library

SOCI is a database access library written in C++ that makes an illusion of embedding
SQL queries in the regular C++ code, staying entirely within the Standard C++.

The idea is to provide C++ programmers a way to access SQL databases in the most natural and intuitive way.
If you find existing libraries too difficult for your needs or just distracting, SOCI can be a good alternative.

## Basic Syntax

The simplest motivating code example for the SQL query that is supposed to retrieve a single row is:

```cpp
int id = ...;
string name;
int salary;

sql << "select name, salary from persons where id = " << id,
        into(name), into(salary);
```

## Basic ORM

The following benefits from extensive support for object-relational mapping:

```cpp
int id = ...;
Person p;

sql << "select first_name, last_name, date_of_birth "
       "from persons where id = " << id, into(p);
```

## Integrations

Integration with STL is also supported:

```cpp
Rowset<string> rs = (sql.prepare << "select name from persons");
std::copy(rs.begin(), rs.end(), std::ostream_iterator<std::string>(std::cout, "\n"));
```

SOCI offers also extensive [integration with Boost](boost.md) datatypes (optional, tuple and fusion) and flexible support for user-defined datatypes.

## Database Backends

Starting from its 2.0.0 release, SOCI uses the plug-in architecture for
backends - this allows to target various database servers.

Currently (SOCI 4.0.0), backends for following database systems are supported:

* [DB2](backends/db2.md)
* [Firebird](backends/firebird.md)
* [MySQL](backends/mysql.md)
* [ODBC](backends/odbc.md) (generic backend)
* [Oracle](backends/oracle.md)
* [PostgreSQL](backends/postgresql.md)
* [SQLite3](backends/sqlite3.md)

The intent of the library is to cover as many database technologies as possible.
For this, the project has to rely on volunteer contributions from other programmers,
who have expertise with the existing database interfaces and would like to help
writing dedicated backends.

## Langauge Bindings

Even though SOCI is mainly a C++ library, it also allows to use it from other programming languages.
Currently the package contains the Ada binding, with more bindings likely to come in the future.
