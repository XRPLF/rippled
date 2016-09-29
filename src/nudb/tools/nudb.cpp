//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <nudb/nudb.hpp>
#include <nudb/util.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace nudb {

namespace detail {

std::ostream&
operator<<(std::ostream& os, dat_file_header const h)
{
    os <<
        "type:            '" << std::string{h.type, h.type + sizeof(h.type)} << "'\n"
        "version:         " << h.version << "\n"
        "uid:             " << fhex(h.uid) << "\n"
        "appnum:          " << fhex(h.appnum) << "\n"
        "key_size:        " << h.key_size << "\n"
        ;
    return os;
}

std::ostream&
operator<<(std::ostream& os, key_file_header const h)
{
    os <<
        "type:            '" << std::string{h.type, h.type + sizeof(h.type)} << "'\n"
        "version:         " << h.version << "\n"
        "uid:             " << fhex(h.uid) << "\n"
        "appnum:          " << fhex(h.appnum) << "\n"
        "key_size:        " << h.key_size << "\n"
        "salt:            " << fhex(h.salt) << "\n"
        "pepper:          " << fhex(h.pepper) << "\n"
        "block_size:      " << fdec(h.block_size) << "\n"
        ;
    return os;
}

std::ostream&
operator<<(std::ostream& os, log_file_header const h)
{
    os << std::setfill('0') << std::internal << std::showbase <<
        "type:            '" << std::string{h.type, h.type + sizeof(h.type)} << "'\n"
        "version:         " << h.version << "\n"
        "uid:             " << fhex(h.uid) << "\n"
        "appnum:          " << fhex(h.appnum) << "\n"
        "key_size:        " << h.key_size << "\n"
        "salt:            " << fhex(h.salt) << "\n"
        "pepper:          " << fhex(h.pepper) << "\n"
        "block_size:      " << fdec(h.block_size) << "\n"
        "key_file_size:   " << fdec(h.key_file_size) << "\n"
        "dat_file_size:   " << fdec(h.dat_file_size) << "\n"
        ;
    return os;
}

} // detail

std::ostream&
operator<<(std::ostream& os, verify_info const& info)
{
    os <<
        "dat_path         " << info.dat_path << "\n"
        "key_path         " << info.key_path << "\n"
        "algorithm        " <<(info.algorithm ? "fast" : "normal") << "\n"
        "avg_fetch:       " << std::fixed << std::setprecision(3) << info.avg_fetch << "\n" <<
        "waste:           " << std::fixed << std::setprecision(3) << info.waste * 100 << "%" << "\n" <<
        "overhead:        " << std::fixed << std::setprecision(1) << info.overhead * 100 << "%" << "\n" <<
        "actual_load:     " << std::fixed << std::setprecision(0) << info.actual_load * 100 << "%" << "\n" <<
        "version:         " << fdec(info.version) << "\n" <<
        "uid:             " << fhex(info.uid) << "\n" <<
        "appnum:          " << fhex(info.appnum) << "\n" <<
        "key_size:        " << fdec(info.key_size) << "\n" <<
        "salt:            " << fhex(info.salt) << "\n" <<
        "pepper:          " << fhex(info.pepper) << "\n" <<
        "block_size:      " << fdec(info.block_size) << "\n" <<
        "bucket_size:     " << fdec(info.bucket_size) << "\n" <<
        "load_factor:     " << std::fixed << std::setprecision(0) << info.load_factor * 100 << "%" << "\n" <<
        "capacity:        " << fdec(info.capacity) << "\n" <<
        "buckets:         " << fdec(info.buckets) << "\n" <<
        "key_count:       " << fdec(info.key_count) << "\n" <<
        "value_count:     " << fdec(info.value_count) << "\n" <<
        "value_bytes:     " << fdec(info.value_bytes) << "\n" <<
        "spill_count:     " << fdec(info.spill_count) << "\n" <<
        "spill_count_tot: " << fdec(info.spill_count_tot) << "\n" <<
        "spill_bytes:     " << fdec(info.spill_bytes) << "\n" <<
        "spill_bytes_tot: " << fdec(info.spill_bytes_tot) << "\n" <<
        "key_file_size:   " << fdec(info.key_file_size) << "\n" <<
        "dat_file_size:   " << fdec(info.dat_file_size) << "\n" <<
        "hist:            " << fhist(info.hist) << "\n"
        ;
    return os;
}

template<class Hasher>
class admin_tool
{
    int ac_ = 0;
    char const* const* av_ = nullptr;
    boost::program_options::options_description desc_;

public:
    admin_tool()
        : desc_("Options")
    {
        namespace po = boost::program_options;
        desc_.add_options()
           ("buffer,b",    po::value<std::size_t>(),
                            "Set the buffer size in bytes (larger is faster).")
           ("dat,d",       po::value<std::string>(),
                            "Path to data file.")
           ("key,k",       po::value<std::string>(),
                            "Path to key file.")
           ("log,l",       po::value<std::string>(),
                            "Path to log file.")
           ("count,n",     po::value<std::uint64_t>(),
                            "The number of items in the data file.")
           ("command",     "Command to run.")
            ;
    }

    std::string
    progname() const
    {
        using namespace boost::filesystem;
        return path{av_[0]}.stem().string();
    }

    std::string
    filename(std::string const& s)
    {
        using namespace boost::filesystem;
        return path{s}.filename().string();
    }

    void
    help()
    {
        std::cout <<
            "usage: " << progname() << " <command> [file...] <options>\n";
        std::cout <<
            "\n"
            "Commands:\n"
            "\n"
            "    help\n"
            "\n"
            "        Print this help information.\n"
            "\n"
            "    info <dat-path> [<key-path> [<log-path>]]\n"
            "\n"
            "        Show metadata and header information for database files.\n"
            "\n"
            "    recover <dat-path> <key-path> <log-path>\n"
            "\n"
            "        Perform a database recovery. A recovery is necessary if a log\n"
            "        file is present.  Running commands on an unrecovered database\n"
            "        may result in lost or corrupted data.\n"
            "\n"
            "    rekey <dat-path] <key-path> <log-path> --count=<items> --buffer=<bytes>\n"
            "\n"
            "        Generate the key file for a data file.  The buffer  option is\n"
            "        required,  larger  buffers process faster.  A buffer equal to\n"
            "        the size of the key file  processes the fastest. This command\n"
            "        must be  passed  the count of  items in the data file,  which\n"
            "        can be calculated with the 'visit' command.\n"
            "\n"
            "        If the rekey is aborted before completion,  the database must\n"
            "        be subsequently restored by running the 'recover' command.\n"
            "\n"
            "    verify <dat-path> <key-path> [--buffer=<bytes>]\n"
            "\n"
            "        Verify  the  integrity of a  database.  The buffer  option is\n"
            "        optional, if omitted a slow  algorithm is used. When a buffer\n"
            "        size  is  provided,  a  fast  algorithm is used  with  larger\n"
            "        buffers  resulting in bigger speedups.  A buffer equal to the\n"
            "        size of the key file provides the fastest speedup.\n"
            "\n"
            "    visit <dat-path>\n"
            "\n"
            "        Iterate a data file and show information, including the count\n"
            "        of items in the file and a histogram of their log base2 size.\n"
            "\n"
            "Notes:\n"
            "\n"
            "    Paths may be full or relative, and should include the extension.\n"
            "    The recover  algorithm  should be  invoked  before  running  any\n"
            "    operation which can modify the database.\n"
            "\n"
            ;
        desc_.print(std::cout);
    };

    int
    error(std::string const& why)
    {
        std::cerr <<
            progname() << ": " << why << ".\n"
            "Use '" << progname() << " help' for usage.\n";
        return EXIT_FAILURE;
    };

    int
    operator()(int ac, char const* const* av)
    {
        namespace po = boost::program_options;

        ac_ = ac;
        av_ = av;

        try
        {
            po::positional_options_description pod;
            pod.add("command", 1);
            pod.add("dat", 1);
            pod.add("key", 1);
            pod.add("log", 1);

            po::variables_map vm;
            po::store(po::command_line_parser(ac, av)
                .options(desc_)
                .positional(pod)
                .run()
                ,vm);
            po::notify(vm);

            std::string cmd;

            if(vm.count("command"))
                cmd = vm["command"].as<std::string>();

            if(cmd == "help")
            {
                help();
                return EXIT_SUCCESS;
            }

            if(cmd == "info")
                return do_info(vm);

            if(cmd == "recover")
                return do_recover(vm);

            if(cmd == "rekey")
                return do_rekey(vm);

            if(cmd == "verify")
                return do_verify(vm);

            if(cmd == "visit")
                return do_visit(vm);

            return error("Unknown command '" + cmd + "'");
        }
        catch(std::exception const& e)
        {
            return error(e.what());
        }
    }

private:
    int
    do_info(boost::program_options::variables_map const& vm)
    {
        if(! vm.count("dat") && ! vm.count("key") && ! vm.count("log"))
            return error("No files specified");
        if(vm.count("dat"))
            do_info(vm["dat"].as<std::string>());
        if(vm.count("key"))
            do_info(vm["key"].as<std::string>());
        if(vm.count("log"))
            do_info(vm["log"].as<std::string>());
        return EXIT_SUCCESS;
    }

    void
    do_info(path_type const& path)
    {
        error_code ec;
        auto const err =
            [&]
            {
                std::cout << path << ": " << ec.message() << "\n";
            };
        native_file f;
        f.open(file_mode::read, path, ec);
        if(ec)
            return err();
        auto const size = f.size(ec);
        if(ec)
            return err();
        if(size < 8)
        {
            std::cout << "File " << path << " is too small to be a database file.\n";
            return;
        }
        std::array<char, 8> ta;
        f.read(0, ta.data(), ta.size(), ec);
        if(ec)
            return err();
        std::string ts{ta.data(), ta.size()};

        if(ts == "nudb.dat")
        {
            detail::dat_file_header h;
            detail::read(f, h, ec);
            if(ec)
                return err();
            f.close();
            std::cout <<
                "data file:       " << path << "\n"
                "file size:       " << fdec(size) << "\n" <<
                h << "\n";
            return;
        }

        if(ts == "nudb.key")
        {
            detail::key_file_header h;
            detail::read(f, h, ec);
            if(ec)
                return err();
            f.close();
            std::cout <<
                "key file:        " << path << "\n"
                "file size:       " << fdec(size) << "\n" <<
                h << "\n";
            return;
        }

        if(ts == "nudb.log")
        {
            detail::log_file_header h;
            detail::read(f, h, ec);
            if(ec)
                return err();
            f.close();
            std::cout <<
                "log file:        " << path << "\n"
                "file size:       " << fdec(size) << "\n" <<
                h << "\n";
            return;
        }

        std::cout << "File " << path << " has unknown type '" << ts << "'.\n";
    }

    int
    do_recover(boost::program_options::variables_map const& vm)
    {
        if(! vm.count("dat") || ! vm.count("key") || ! vm.count("log"))
            return error("Missing file specifications");
        error_code ec;
        recover<xxhasher>(
            vm["dat"].as<std::string>(),
            vm["key"].as<std::string>(),
            vm["log"].as<std::string>(),
            ec);
        if(ec)
        {
            std::cerr << "recover: " << ec.message() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    int
    do_rekey(boost::program_options::variables_map const& vm)
    {
        if(! vm.count("dat"))
            return error("Missing data file path");
        if(! vm.count("key"))
            return error("Missing key file path");
        if(! vm.count("log"))
            return error("Missing log file path");
        if(! vm.count("count"))
            return error("Missing item count");
        if(! vm.count("buffer"))
            return error("Missing buffer size");
        auto const dp = vm["dat"].as<std::string>();
        auto const kp = vm["key"].as<std::string>();
        auto const lp = vm["log"].as<std::string>();
        auto const itemCount = vm["count"].as<std::size_t>();
        auto const bufferSize = vm["buffer"].as<std::size_t>();
        error_code ec;
        progress p{std::cout};
        rekey<Hasher, native_file>(dp, kp, lp,
            block_size(kp), 0.5f, itemCount,
                bufferSize, ec, p);
        if(ec)
        {
            std::cerr << "rekey: " << ec.message() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    int
    do_verify(boost::program_options::variables_map const& vm)
    {
        if(! vm.count("dat"))
            return error("Missing data file path");
        if(! vm.count("key"))
            return error("Missing key file path");

        auto const bufferSize = vm.count("buffer") ?
            vm["buffer"].as<std::size_t>() : 0;
        auto const dp = vm["dat"].as<std::string>();
        auto const kp = vm.count("key") ?
            vm["key"].as<std::string>() : std::string{};

        if(! vm.count("key"))
        {
            // todo
            std::cerr << "unimplemented: dat-only verify\n";
            return EXIT_FAILURE;
        }

        error_code ec;
        progress p(std::cout);
        {
            verify_info info;
            verify<Hasher>(info, dp, kp, bufferSize, p, ec);
            if(! ec)
                std::cout << info;
        }
        if(ec)
        {
            std::cerr << "verify: " << ec.message() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    int
    do_visit(boost::program_options::variables_map const& vm)
    {
        if(! vm.count("dat"))
            return error("Missing dat path");
        auto const path = vm["dat"].as<std::string>();
        error_code ec;
        auto const err =
            [&]
            {
                std::cout << path << ": " << ec.message() << "\n";
                return EXIT_FAILURE;
            };
        {
            native_file f;
            f.open(file_mode::read, path, ec);
            if(ec)
                return err();
            auto const fileSize = f.size(ec);
            if(ec)
                return err();
            detail::dat_file_header h;
            detail::read(f, h, ec);
            if(ec)
                return err();
            f.close();
            std::cout <<
                "data file:       " << path << "\n"
                "file size:       " << fdec(fileSize) << "\n" <<
                h;
            std::cout.flush();
        }

        std::uint64_t n = 0;
        std::array<std::uint64_t, 64> hist;
        hist.fill(0);
        progress p{std::cout};
        visit(path,
            [&](void const*, std::size_t,
                void const*, std::size_t data_size,
                error_code& ec)
            {
                ++n;
                ++hist[log2(data_size)];
                //std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }, p, ec);
        if(! ec)
            std::cout <<
                "value_count      " << fdec(n) << "\n" <<
                "sizes:           " << fhist(hist) << "\n";
        if(ec)
        {
            std::cerr << "visit: " << ec.message() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
};

} // nudb

int
main(int ac, char const* const* av)
{
    using namespace nudb;
    admin_tool<xxhasher> t;
    auto const rv = t(ac, av);
    std::cout.flush();
    basic_seconds_clock_main_hook();
    return rv;
}
