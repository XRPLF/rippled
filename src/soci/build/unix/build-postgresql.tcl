proc findPostgreSQL {} {
    global postgresqlInclude postgresqlLib

    # candidate directories for local PostgreSQL:
    set includeDirs {
        "/usr/local/pgsql/include"
        "/usr/local/postgresql/include"
        "/usr/local/include/pgsql"
        "/usr/local/include/postgresql"
        "/usr/include/pgsql"
        "/usr/include/postgresql"
        "/usr/local/include"
        "/usr/include"
        "/opt/local/include"
    }
    set libDirs {
        "/usr/local/pgsql/lib"
        "/usr/local/postgresql/lib"
        "/usr/local/lib/pgsql"
        "/usr/local/lib/postgresql"
        "/usr/lib/pgsql"
        "/usr/lib/postgresql"
        "/usr/local/lib"
        "/usr/lib"
        "/opt/local/lib"
    }

    if [info exists postgresqlInclude] {
        set includeDirs [list $postgresqlInclude]
    }
    if [info exists postgresqlLib] {
        set libDirs [list $postgresqlLib]
    }

    set includeDir ""
    foreach I $includeDirs {
        set header "${I}/libpq/libpq-fs.h"
        if {[file exists $header]} {
            set includeDir $I
            break
        }
    }
    if {$includeDir == ""} {
        return {}
    }

    set libDir ""
    foreach L $libDirs {
        set libraryA "${L}/libpq.a"
        set librarySo "${L}/libpq.so"
        if {[file exists $libraryA] || [file exists $librarySo]} {
            set libDir $L
            break
        }
    }
    if {$libDir == ""} {
        return {}
    }

    return [list $includeDir $libDir]
}

proc buildPostgreSQL {} {
    global CXXFLAGS

    puts "building static PostgreSQL"

    set dirs [findPostgreSQL]
    if {$dirs == {}} {
        puts "cannot find PostgreSQL library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/postgresql"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -I../../core -I${includeDir}"
    }

    execute "ar cr libsoci_postgresql.a [glob *.o]"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/postgresql/libsoci_postgresql.a lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/postgresql/soci-postgresql.h include"
}

proc buildPostgreSQLSo {} {
    global CXXFLAGS SHARED

    puts "building shared PostgreSQL"

    set dirs [findPostgreSQL]
    if {$dirs == {}} {
        puts "cannot find PostgreSQL library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/postgresql"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -fPIC -I../../core -I${includeDir}"
    }

    execute "g++ $SHARED -o libsoci_postgresql.so [glob *.o] -L${libDir} -lpq"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/postgresql/libsoci_postgresql.so lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/postgresql/soci-postgresql.h include"
}

proc buildPostgreSQLTest {} {
    global CXXTESTFLAGS LDL

    puts "building PostgreSQL test"

    set dirs [findPostgreSQL]
    if {$dirs == {}} {
        puts "cannot find PostgreSQL library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set dirs [findBoost]
    if {$dirs == {}} {
        puts "cannot find Boost library files, skipping this target"
        return
    }

    set boostIncludeDir [lindex $dirs 0]
    set boostLibDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/postgresql/test"
    execute "g++ test-postgresql.cpp -o test-postgresql $CXXTESTFLAGS -I.. -I../../../core -I../../../core/test -I${includeDir} -I${boostIncludeDir} -L../../../../build/unix/lib -L${libDir} -L${boostLibDir} -lsoci_core -lsoci_postgresql -lboost_date_time ${LDL} -lpq"
    cd $cwd
    eval exec mkdir -p "tests"
    execute "cp ../../src/backends/postgresql/test/test-postgresql tests"
}
