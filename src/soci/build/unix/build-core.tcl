proc buildCore {} {
    global CXXFLAGS

    puts "building static core"

    set cwd [pwd]
    cd "../../src/core"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS"
    }

    execute "ar cr libsoci_core.a [glob *.o]"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/core/libsoci_core.a lib"
    eval exec mkdir -p "include"
    execute "cp [glob ../../src/core/*.h] include"
}

proc buildCoreSo {} {
    global CXXFLAGS SHARED

    puts "building shared core"

    set cwd [pwd]
    cd "../../src/core"
    foreach cppFile [glob "*.cpp"] {
        execute "g++ -c $cppFile $CXXFLAGS -fPIC"
    }

    execute "g++ $SHARED -o libsoci_core.so [glob *.o]"
    cd $cwd
    eval exec mkdir -p "lib"
    execute "cp ../../src/core/libsoci_core.so lib"
    eval exec mkdir -p "include"
    execute "cp [glob ../../src/core/*.h] include"
}
