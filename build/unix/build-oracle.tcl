proc findOracle {} {
    global env oracleInclude oracleLib

    if {[info exists oracleInclude] &&
        [info exists oracleLib]} {
        set includeDir $oracleInclude
        set libDir $oracleLib
    } else {
        if {[info exists env(ORACLE_HOME)] == 0} {
            puts "The ORACLE_HOME variable is not set."
            return {}
        }
    
        set ORACLE_HOME $env(ORACLE_HOME)

        set includeDir [file join $ORACLE_HOME "rdbms/public"]
        set header [file join $includeDir "oci.h"]
        if {[file exists $header] == 0} {
            puts "ORACLE_HOME is strange."
            return {}
        }

        set libDir [file join $ORACLE_HOME "lib"]
        set libraryA [file join $libDir "libclntsh.a"]
        set librarySo [file join $libDir "libclntsh.so"]
        if {([file exists $libraryA] == 0) && ([file exists $librarySo] == 0)} {
            puts "ORACLE_HOME is strange."
            return {}
        }
    }

    return [list $includeDir $libDir]
}

proc buildOracle {} {
    global CXXFLAGS

    puts "building static Oracle"

    set dirs [findOracle]
    if {$dirs == {}} {
        puts "cannot find Oracle library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/oracle"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -I../../core -I${includeDir}"
    }

    execute "ar cr libsoci_oracle.a [glob *.o]"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/oracle/libsoci_oracle.a lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/oracle/soci-oracle.h include"
}

proc buildOracleSo {} {
    global CXXFLAGS SHARED

    puts "building shared Oracle"

    set dirs [findOracle]
    if {$dirs == {}} {
        puts "cannot find Oracle library files, skipping this target"
        return
    }

    set includeDir [lindex $dirs 0]
    set libDir [lindex $dirs 1]

    set cwd [pwd]
    cd "../../src/backends/oracle"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -fPIC -I../../core -I${includeDir}"
    }

    execute "g++ $SHARED -o libsoci_oracle.so [glob *.o] -L${libDir} -lclntsh -lnnz10"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/backends/oracle/libsoci_oracle.so lib"
    eval exec mkdir -p "include"
    execute "cp ../../src/backends/oracle/soci-oracle.h include"
}

proc buildOracleTest {} {
    global CXXTESTFLAGS LDL

    puts "building Oracle test"

    set dirs [findOracle]
    if {$dirs == {}} {
        puts "cannot find Oracle library files, skipping this target"
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
    cd "../../src/backends/oracle/test"
    execute "g++ test-oracle.cpp -o test-oracle $CXXTESTFLAGS -I.. -I../../../core -I../../../core/test -I${includeDir} -I${boostIncludeDir} -L../../../../build/unix/lib -L${libDir} -L${boostLibDir} -lsoci_core -lsoci_oracle -lboost_date_time ${LDL} -lclntsh -lnnz10"
    cd $cwd
    eval exec mkdir -p "tests"
    execute "cp ../../src/backends/oracle/test/test-oracle tests"
}
