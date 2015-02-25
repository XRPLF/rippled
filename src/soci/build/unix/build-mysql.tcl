source "local/parameters.tcl"

proc findMySQL {} {
    global mysqlInclude mysqlLib

    # candidate directories for local MySQL:
    set includeDirs {
        "/usr/local/include/mysql"
        "/usr/include/mysql"
        "/usr/local/include"
        "/usr/include"
        "/opt/local/include"
    }
    set libDirs {
        "/usr/local/lib/mysql"
        "/usr/lib/mysql"
        "/usr/local/lib"
        "/usr/lib"
        "/opt/local/lib"
    }

    if [info exists mysqlInclude] {
        set includeDirs [list $mysqlInclude]
    }
    if [info exists mysqlLib] {
        set libDirs [list $mysqlLib]
    }

    set includeDir ""
    foreach I $includeDirs {
        set header "${I}/mysql.h"
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
        set libraryA "${L}/libmysqlclient.a"
        set librarySo "${L}/libmysqlclient.so"
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

proc buildMySQL {} {
    global CXXFLAGS

    puts "building static MySQL"

    set dirs [findMySQL]
    if {$dirs == {}} {
        puts "cannot find MySQL library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/mysql"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -I../../core -I${includeDir}"
    }

    execute "ar cr libsoci_mysql.a [glob *.o]"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/mysql/libsoci_mysql.a lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/mysql/soci-mysql.h include"
}

proc buildMySQLSo {} {
    global CXXFLAGS SHARED

    puts "building shared MySQL"

    set dirs [findMySQL]
    if {$dirs == {}} {
        puts "cannot find MySQL library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/mysql"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -fPIC -I../../core -I${includeDir}"
    }

    execute "g++ $SHARED -o libsoci_mysql.so [glob *.o] -L${libDir} -lmysqlclient -lz"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/mysql/libsoci_mysql.so lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/mysql/soci-mysql.h include"
}

proc buildMySQLTest {} {
    global CXXTESTFLAGS LDL

    puts "building MySQL test"

    set dirs [findMySQL]
    if {$dirs == {}} {
        puts "cannot find MySQL library files, skipping this target"
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
    cd "../../src/backends/mysql/test"
    execute "g++ test-mysql.cpp -o test-mysql $CXXTESTFLAGS -I.. -I../../../core -I../../../core/test -I${includeDir} -I${boostIncludeDir} -L../../../../build/unix/lib -L${libDir} -L${boostLibDir} -lsoci_core -lsoci_mysql -lboost_date_time ${LDL} -lmysqlclient -lz"
    cd $cwd
    eval exec mkdir -p "tests"
    execute "cp ../../src/backends/mysql/test/test-mysql tests"
}
