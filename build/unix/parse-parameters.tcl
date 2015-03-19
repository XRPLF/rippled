set paramsFile [open "local/parameters"]
set tclParamsFile [open "local/parameters.tcl" "w"]

set line [gets $paramsFile]
while {![eof $paramsFile]} {
    set pair [split $line "="]
    if {[llength $pair] == 2} {
        set name [lindex $pair 0]
        set value [lindex $pair 1]

        switch -exact -- $name {
            --include-prefix {
                puts $tclParamsFile "set headerInstallPrefix $value"
                puts "setting prefix for SOCI headers to $value"
            }
            --lib-prefix {
                puts $tclParamsFile "set libInstallPrefix $value"
                puts "setting prefix for SOCI libraries to $value"
            }
            --mysql-include {
                puts $tclParamsFile "set mysqlInclude $value"
                puts "setting include directory for MySQL to $value"
            }
            --mysql-lib {
                puts $tclParamsFile "set mysqlLib $value"
                puts "setting lib directory for MySQL to $value"
            }
            --oracle-include {
                puts $tclParamsFile "set oracleInclude $value"
                puts "setting include directory for Oracle to $value"
            }
            --oracle-lib {
                puts $tclParamsFile "set oracleLib $value"
                puts "setting lib directory for Oracle to $value"
            }
            --postgresql-include {
                puts $tclParamsFile "set postgresqlInclude $value"
                puts "setting include directory for PostgreSQL to $value"
            }
            --postgresql-lib {
                puts $tclParamsFile "set postgresqlLib $value"
                puts "setting lib directory for PostgreSQL to $value"
            }
            --boost-include {
                puts $tclParamsFile "set boostInclude $value"
                puts "setting Boost include directory to $value"
            }
            --boost-lib {
                puts $tclParamsFile "set boostLib $value"
                puts "setting Boost lib directory to $value"
            }
            default {
                puts "unknown option: $name : skipping it!"
            }
        }
    }
    set line [gets $paramsFile]
}
close $paramsFile
close $tclParamsFile
