proc findBoost {} {
    global rootBoost

    # candidate directories for local Boost:
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

    if [info exists rootBoost] {
        set includeDirs [list $rootBoost]
        set libDirs [list $rootBoost]
    }

    set includeDir ""
    foreach I $includeDirs {
        set header "${I}/boost/version.hpp"
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
        set libraryA "${L}/libboost_date_time.a"
        set librarySo "${L}/libboost_date_time.so"
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
