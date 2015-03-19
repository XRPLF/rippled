#!/usr/bin/tclsh

# some common compilation settings if you need to change them:

if [info exists env(CXXFLAGS)] {
    set CXXFLAGS $env(CXXFLAGS)
} else {
    set CXXFLAGS "-Wall -pedantic -Wno-long-long -O2"
}

set CXXTESTFLAGS "-O2"

if {$tcl_platform(os) == "Darwin"} {
    # special case for Mac OS X
    set SHARED "-dynamiclib -flat_namespace -undefined suppress"
} else {
    set SHARED "-shared"
}

if {$tcl_platform(os) == "FreeBSD"} {
    # FreeBSD does not have the libdl library, it is part of libc.
    set LDL ""
} else {
    set LDL "-ldl"
}

source "execute.tcl"
source "find-boost.tcl"
source "build-core.tcl"
source "build-oracle.tcl"
source "build-postgresql.tcl"
source "build-mysql.tcl"

proc printUsageAndExit {} {
    puts "Usage:"
    puts "$ ./build.tcl list-of-targets"
    puts ""
    puts "list of targets can contain any of:"
    puts "core          - the core part of the library (static version)"
    puts "core-so       - the core part of the library (shared version)"
    puts "oracle        - the static Oracle backend"
    puts "oracle-so     - the shared Oracle backend"
    puts "                Note: before building Oracle backend"
    puts "                set the ORACLE_HOME variable properly."
    puts "postgresql    - the static PostgreSQL backend"
    puts "postgresql-so - the shared PostgreSQL backend"
    puts "mysql         - the static MySQL backend"
    puts "mysql-so      - the shared MySQL backend"
    puts ""
    puts "oracle-test     - the test for Oracle"
    puts "postgresql-test - the test for PostgreSQL"
    puts "mysql-test      - the test for MySQL"
    puts "                  Note: build static core and backends first."
    puts ""
    puts "Examples:"
    puts ""
    puts "$ ./build.tcl core mysql"
    puts ""
    puts "$ ./build.tcl core postgresql postgresql-test"
    puts ""
    puts "After successful build the results are in include, lib and test directories."
    puts "Move/copy the contents of these directories wherever you want."
    exit
}

if {$argc == 0 || $argv == "--help"} {
    printUsageAndExit
}

foreach target $argv {
    switch -exact $target {
        core buildCore
        core-so buildCoreSo
        oracle buildOracle
        oracle-so buildOracleSo
        oracle-test buildOracleTest
        postgresql buildPostgreSQL
        postgresql-so buildPostgreSQLSo
        postgresql-test buildPostgreSQLTest
        mysql buildMySQL
        mysql-so buildMySQLSo
        mysql-test buildMySQLTest
        default {
            puts "unknown target $target - skipping"
        }
    }
}
