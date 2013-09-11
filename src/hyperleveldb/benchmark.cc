// Copyright (c) 2013, Cornell University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of HyperLevelDB nor the names of its contributors may
//       be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C
#include <cstdlib>

// STL
#include <tr1/memory>
#include <vector>

// LevelDB
#include <hyperleveldb/cache.h>
#include <hyperleveldb/db.h>
#include <hyperleveldb/filter_policy.h>

// po6
#include <po6/io/fd.h>
#include <po6/threads/thread.h>

// e
#include <e/guard.h>

// armnod
#include <armnod.h>

// numbers
#include <numbers.h>

static long _done = 0;
static long _number = 1000000;
static long _threads = 1;

static struct poptOption _popts[] = {
    {"number", 'n', POPT_ARG_LONG, &_number, 0,
     "perform N operations against the database (default: 1000000)", "N"},
    {"threads", 't', POPT_ARG_LONG, &_threads, 0,
     "run the test with T concurrent threads (default: 1)", "T"},
    POPT_TABLEEND
};

static void
worker_thread(leveldb::DB*,
              numbers::throughput_latency_logger* tll,
              const armnod::argparser& k,
              const armnod::argparser& v);

int
main(int argc, const char* argv[])
{
    armnod::argparser key_parser("key-");
    armnod::argparser value_parser("value-");
    std::vector<poptOption> popts;
    poptOption s[] = {POPT_AUTOHELP {NULL, 0, POPT_ARG_INCLUDE_TABLE, _popts, 0, "Benchmark:", NULL}, POPT_TABLEEND};
    popts.push_back(s[0]);
    popts.push_back(s[1]);
    popts.push_back(key_parser.options("Key Generation:"));
    popts.push_back(value_parser.options("Value Generation:"));
    popts.push_back(s[2]);
    poptContext poptcon;
    poptcon = poptGetContext(NULL, argc, argv, &popts.front(), POPT_CONTEXT_POSIXMEHARDER);
    e::guard g = e::makeguard(poptFreeContext, poptcon); g.use_variable();
    poptSetOtherOptionHelp(poptcon, "[OPTIONS]");

    int rc;

    while ((rc = poptGetNextOpt(poptcon)) != -1)
    {
        switch (rc)
        {
            case POPT_ERROR_NOARG:
            case POPT_ERROR_BADOPT:
            case POPT_ERROR_BADNUMBER:
            case POPT_ERROR_OVERFLOW:
                std::cerr << poptStrerror(rc) << " " << poptBadOption(poptcon, 0) << std::endl;
                return EXIT_FAILURE;
            case POPT_ERROR_OPTSTOODEEP:
            case POPT_ERROR_BADQUOTE:
            case POPT_ERROR_ERRNO:
            default:
                std::cerr << "logic error in argument parsing" << std::endl;
                return EXIT_FAILURE;
        }
    }

    leveldb::Options opts;
    opts.create_if_missing = true;
    opts.write_buffer_size = 64ULL * 1024ULL * 1024ULL;
    opts.filter_policy = leveldb::NewBloomFilterPolicy(10);
    leveldb::DB* db;
    leveldb::Status st = leveldb::DB::Open(opts, "tmp", &db);

    if (!st.ok())
    {
        std::cerr << "could not open LevelDB: " << st.ToString() << std::endl;
        return EXIT_FAILURE;
    }

    numbers::throughput_latency_logger tll;

    if (!tll.open("benchmark.log"))
    {
        std::cerr << "could not open log: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    typedef std::tr1::shared_ptr<po6::threads::thread> thread_ptr;
    std::vector<thread_ptr> threads;

    for (size_t i = 0; i < _threads; ++i)
    {
        thread_ptr t(new po6::threads::thread(std::tr1::bind(worker_thread, db, &tll, key_parser, value_parser)));
        threads.push_back(t);
        t->start();
    }

    for (size_t i = 0; i < threads.size(); ++i)
    {
        threads[i]->join();
    }

    std::string tmp;
    if (db->GetProperty("leveldb.stats", &tmp)) std::cout << tmp << std::endl;
    delete db;

    if (!tll.close())
    {
        std::cerr << "could not close log: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static uint64_t
get_random()
{
    po6::io::fd sysrand(open("/dev/urandom", O_RDONLY));

    if (sysrand.get() < 0)
    {
        return 0xcafebabe;
    }

    uint64_t ret;

    if (sysrand.read(&ret, sizeof(ret)) != sizeof(ret))
    {
        return 0xdeadbeef;
    }

    return ret;
}

void
worker_thread(leveldb::DB* db,
              numbers::throughput_latency_logger* tll,
              const armnod::argparser& _k,
              const armnod::argparser& _v)
{
    armnod::generator key(armnod::argparser(_k).config());
    armnod::generator val(armnod::argparser(_v).config());
    key.seed(get_random());
    val.seed(get_random());
    numbers::throughput_latency_logger::thread_state ts;
    tll->initialize_thread(&ts);

    while (__sync_fetch_and_add(&_done, 1) < _number)
    {
        std::string k = key();
        std::string v = val();

        // issue a "get"
        std::string tmp;
        leveldb::ReadOptions ropts;
        leveldb::Status rst = db->Get(ropts, leveldb::Slice(k.data(), k.size()), &tmp);
        assert(rst.ok() || rst.IsNotFound());

        // issue a "put"
        leveldb::WriteOptions wopts;
        wopts.sync = false;
        tll->start(&ts, 0);
        leveldb::Status wst = db->Put(wopts, leveldb::Slice(k.data(), k.size()), leveldb::Slice(v.data(), v.size()));
        tll->finish(&ts);
        assert(wst.ok());
    }

    tll->terminate_thread(&ts);
}
