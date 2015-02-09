proc execute {command} {
    puts $command
    set result [catch {eval exec $command "2>@ stdout"} output]
    if {$result != 0} {
        puts "The last command did not execute properly:"
        puts $output
        puts "Please contact the SOCI team."
        exit
    } else {
        if {$output != ""} {
            puts $output
        }
    }
}
