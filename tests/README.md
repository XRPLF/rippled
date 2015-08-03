# soci/tests

SOCI tests using [CATCH](http://catch-lib.net/) testing framework live here.

Currently one test is built for each backend, i.e. there are
`soci_oracle_test`, `soci_postgresql_test`, `soci_sqlite3_test` and so on and
for ODBC backend there are multiple versions of the test depending on the ODBC
driver used. Each of these tests can be run with a single parameter describing
the database to use for testing in the backend-specific way as well as any of
the standard [CATCH command line options](https://github.com/philsquared/Catch/blob/master/docs/command-line.md).
