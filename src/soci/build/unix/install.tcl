set headerInstallPrefix "/usr/local/include/soci"
set libInstallPrefix "/usr/local/lib"
set sociVersion "3.0.0"
set sociMajor "3"

source "execute.tcl"
source "local/parameters.tcl"

if [info exists env(DESTDIR)] {
    set DESTDIR $env(DESTDIR)
    set headerInstallPrefix [file normalize ${DESTDIR}/${headerInstallPrefix}]
    set libInstallPrefix [file normalize ${DESTDIR}/${libInstallPrefix}]
}

set uninstallFile [open "local/uninstall.sh" "w"]

if {[file exists $headerInstallPrefix] == 0} {
    execute "mkdir -p $headerInstallPrefix"
    puts $uninstallFile "rm -rf $headerInstallPrefix"
}

foreach header [glob "include/*"] {
    set tail [file tail $header]
    puts "copying $tail to ${headerInstallPrefix}"
    execute "cp $header $headerInstallPrefix"
    puts $uninstallFile "rm -f ${headerInstallPrefix}/${tail}"
}

if {[file exists $libInstallPrefix] == 0} {
    execute "mkdir -p $libInstallPrefix"
    puts $uninstallFile "rm -rf $libInstallPrefix"
}

foreach lib [glob "lib/*.a"] {
    set tail [file tail $lib]
    puts "copying $tail to ${libInstallPrefix}"
    execute "cp $lib $libInstallPrefix"
    puts $uninstallFile "rm -f ${libInstallPrefix}/${tail}"
}

set buildDir [pwd]
cd $libInstallPrefix
foreach lib [glob "${buildDir}/lib/*.so"] {
    set rootName [file rootname [file tail $lib]]
    set targetName "${rootName}-${sociVersion}.so"
    set majorLink "${rootName}-${sociMajor}.so"
    set link "${rootName}.so"

    puts "copying [file tail $lib] to ${targetName}"
    execute "cp $lib $targetName"
    puts $uninstallFile "rm -f ${libInstallPrefix}/${targetName}"

    puts "creating link ${majorLink}"
    execute "ln -s $targetName [file tail $majorLink]"
    puts $uninstallFile "rm -f ${libInstallPrefix}/${majorLink}"

    puts "creating ${link}"
    execute "ln -s $targetName [file tail $link]"
    puts $uninstallFile "rm -f ${libInstallPrefix}/${link}"
}

close $uninstallFile

puts "ldconfig ${libInstallPrefix}"
catch { eval exec "ldconfig ${libInstallPrefix}" }

puts ""
puts ""
puts "Hint: the shared libraries were installed in $libInstallPrefix"
puts "- If you use dynamically loaded backends, then you might need to set"
puts "  the SOCI_BACKENDS_PATH variable accordingly."
puts ""
puts "Hint: to remove all installed files and links run make uninstall"
puts ""
