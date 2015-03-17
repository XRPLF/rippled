proc findSqlite3 {} {
    global Sqlite3Include Sqlite3Lib

    # candidate directories for local sqlite3:
    set includeDirs {
        "/usr/local/include"
        "/usr/include"
        "/opt/local/include"
    }
    set libDirs {
        "/usr/local/lib"
        "/usr/lib"
        "/opt/local/lib"
    }

    if [info exists Sqlite3Include] {
        set includeDirs [list $Sqlite3Include]
    }
    if [info exists Sqlite3Lib] {
        set libDirs [list $Sqlite3Lib]
    }

    set includeDir ""
    foreach I $includeDirs {
        set header "${I}/sqlite3.h"
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
        set libraryA "${L}/libsqlite3.a"
        set librarySo "${L}/libsqlite3.so"
        set libraryDl "${L}/libsqlite3.dylib"
        if {[file exists $libraryA] || [file exists $librarySo] || [file exists $libraryDl]} {
            set libDir $L
            break
        }
    }
    if {$libDir == ""} {
        return {}
    }

    return [list $includeDir $libDir]
}

proc buildSqlite3 {} {
    global CXXFLAGS tcl_platform

    puts "building static Sqlite3"

    set dirs [findSqlite3]
    if {$dirs == {}} {
        puts "cannot find Sqlite3 library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/sqlite3"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -I../../core -I${includeDir}"
    }

    execute "ar cr libsoci_sqlite3.a [glob *.o]"
    if {$tcl_platform(os) == "Darwin"} {
        # special case for Mac OS X
        execute "ranlib libsoci_sqlite3.a"
    }
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/sqlite3/libsoci_sqlite3.a lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/sqlite3/soci-sqlite3.h include"
    
}

proc buildSqlite3So {} {
    global CXXFLAGS SHARED

    puts "building shared Sqlite3"

    set dirs [findSqlite3]
    if {$dirs == {}} {
        puts "cannot find Sqlite3 library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/sqlite3"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -fPIC -I../../core -I${includeDir}"
    }

    execute "g++ $SHARED -o libsoci_sqlite3.so [glob *.o] -L${libDir} -lsqlite3"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/sqlite3/libsoci_sqlite3.so lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/sqlite3/soci-sqlite3.h include"
}

proc buildSqlite3Test {} {
    global CXXTESTFLAGS LDL

    puts "building Sqlite3 test"

    set dirs [findSqlite3]
    if {$dirs == {}} {
        puts "cannot find Sqlite3 library files, skipping this target"
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
    cd "../../src/backends/sqlite3/test"
    execute "g++ test-sqlite3.cpp -o test-sqlite3 $CXXTESTFLAGS -I.. -I../../../core -I../../../core/test -I${includeDir} -I${boostIncludeDir} -L../../../../build/unix/lib -L${libDir} -L${boostLibDir} -lsoci_core -lsoci_sqlite3 -lboost_date_time ${LDL} -lsqlite3"
    cd $cwd
    eval exec mkdir -p "tests"
    execute "cp ../../src/backends/sqlite3/test/test-sqlite3 tests"
}
