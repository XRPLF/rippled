//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/PerfLog.h>
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/Handler.h>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <test/jtx/Env.h>
#include <thread>

//------------------------------------------------------------------------------

namespace ripple {

class PerfLog_test : public beast::unit_test::suite
{
    enum class WithFile : bool { no = false, yes = true };

    using path = boost::filesystem::path;

    // We're only using Env for its Journal.  That Journal gives better
    // coverage in unit tests.
    test::jtx::Env env_{*this};
    beast::Journal j_{env_.app().journal("PerfLog_test")};

    // A PerfLog needs a Parent that is a Stoppable and a function to
    // call if it wants to shutdown the system.  This class provides both.
    struct PerfLogParent : public RootStoppable
    {
        bool stopSignaled{false};
        beast::Journal j_;

        explicit PerfLogParent(beast::Journal const& j)
            : RootStoppable("testRootStoppable"), j_(j)
        {
        }

        ~PerfLogParent() override
        {
            doStop();
            cleanupPerfLogDir();
        }

        // Benign replacement for Application::signalStop().
        void
        signalStop()
        {
            stopSignaled = true;
        }

    private:
        // Interfaces for RootStoppable.  Made private to encourage the use
        // of doStart() and doStop().
        void
        onPrepare() override
        {
        }

        void
        onStart() override
        {
        }

        void
        onStop() override
        {
            if (areChildrenStopped())
                stopped();
        }

        void
        onChildrenStopped() override
        {
            onStop();
        }

    public:
        // Test interfaces to start and stop the PerfLog
        void
        doStart()
        {
            prepare();
            start();
        }

        void
        doStop()
        {
            if (started())
                stop(j_);
        }

        // Interfaces for PerfLog file management
        static path
        getPerfLogPath()
        {
            using namespace boost::filesystem;
            return temp_directory_path() / "perf_log_test_dir";
        }

        static path
        getPerfLogFileName()
        {
            return {"perf_log.txt"};
        }

        static std::chrono::milliseconds
        getLogInterval()
        {
            return std::chrono::milliseconds{10};
        }

        static perf::PerfLog::Setup
        getSetup(WithFile withFile)
        {
            return perf::PerfLog::Setup{
                withFile == WithFile::no
                    ? ""
                    : getPerfLogPath() / getPerfLogFileName(),
                getLogInterval()};
        }

        static void
        cleanupPerfLogDir()
        {
            using namespace boost::filesystem;

            auto const perfLogPath{getPerfLogPath()};
            auto const fullPath = perfLogPath / getPerfLogFileName();
            if (exists(fullPath))
                remove(fullPath);

            if (!exists(perfLogPath) || !is_directory(perfLogPath) ||
                !is_empty(perfLogPath))
            {
                return;
            }
            remove(perfLogPath);
        }
    };

    //------------------------------------------------------------------------------

    // Convenience function to return a PerfLog
    std::unique_ptr<perf::PerfLog>
    getPerfLog(PerfLogParent& parent, WithFile withFile)
    {
        return perf::make_PerfLog(
            parent.getSetup(withFile), parent, j_, [&parent]() {
                return parent.signalStop();
            });
    }

    //------------------------------------------------------------------------------

    // Convenience function to return a uint64 given a Json::Value containing
    // a string.
    static std::uint64_t
    jsonToUint64(Json::Value const& jsonUintAsString)
    {
        return std::stoull(jsonUintAsString.asString());
    }

    //------------------------------------------------------------------------------

    // The PerfLog's current state is easier to sort by duration if the
    // duration is converted from string to integer.  The following struct
    // is a way to think about the converted entry.
    struct Cur
    {
        std::uint64_t dur;
        std::string name;

        Cur(std::uint64_t d, std::string n) : dur(d), name(std::move(n))
        {
        }
    };

    // A convenience function to convert JSON to Cur and sort.  The sort
    // goes from longest to shortest duration.  That way stuff that was started
    // earlier goes to the front.
    static std::vector<Cur>
    getSortedCurrent(Json::Value const& currentJson)
    {
        std::vector<Cur> currents;
        currents.reserve(currentJson.size());
        for (Json::Value const& cur : currentJson)
        {
            currents.emplace_back(
                jsonToUint64(cur[jss::duration_us]),
                cur.isMember(jss::job) ? cur[jss::job].asString()
                                       : cur[jss::method].asString());
        }

        // Note that the longest durations should be at the front of the
        // vector since they were started first.
        std::sort(
            currents.begin(),
            currents.end(),
            [](Cur const& lhs, Cur const& rhs) {
                if (lhs.dur != rhs.dur)
                    return (rhs.dur < lhs.dur);
                return (lhs.name < rhs.name);
            });
        return currents;
    }

    //------------------------------------------------------------------------------

    // Helper function that checks the size of the PerfLog file and then
    // returns when the file gets bigger.  This indicates that the PerfLog
    // has written new values to the file and _should_ have the latest
    // update.
    static void
    waitForFileUpdate(PerfLogParent const& parent)
    {
        using namespace boost::filesystem;

        auto const path = parent.getPerfLogPath() / parent.getPerfLogFileName();
        if (!exists(path))
            return;

        // We wait for the file to change size twice.  The first file size
        // change may have been in process while we arrived.
        std::uintmax_t const firstSize{file_size(path)};
        std::uintmax_t secondSize{firstSize};
        do
        {
            std::this_thread::sleep_for(parent.getLogInterval());
            secondSize = file_size(path);
        } while (firstSize >= secondSize);

        do
        {
            std::this_thread::sleep_for(parent.getLogInterval());
        } while (secondSize >= file_size(path));
    }

    //------------------------------------------------------------------------------

public:
    void
    testFileCreation()
    {
        using namespace boost::filesystem;

        auto const perfLogPath{PerfLogParent::getPerfLogPath()};
        auto const fullPath = perfLogPath / PerfLogParent::getPerfLogFileName();
        {
            // Verify a PerfLog creates its file when constructed.
            PerfLogParent parent{j_};
            BEAST_EXPECT(!exists(perfLogPath));

            auto perfLog{getPerfLog(parent, WithFile::yes)};

            BEAST_EXPECT(parent.stopSignaled == false);
            BEAST_EXPECT(exists(perfLogPath));
        }
        {
            // Create a file where PerfLog wants to put its directory.
            // Make sure that PerfLog tries to shutdown the server since it
            // can't open its file.
            PerfLogParent parent{j_};
            if (!BEAST_EXPECT(!exists(perfLogPath)))
                return;

            {
                // Make a file that prevents PerfLog from creating its file.
                std::ofstream nastyFile;
                nastyFile.open(
                    perfLogPath.c_str(), std::ios::out | std::ios::app);
                if (!BEAST_EXPECT(nastyFile))
                    return;
                nastyFile.close();
            }

            // Now construct a PerfLog.  The PerfLog should attempt to shut
            // down the server because it can't open its file.
            BEAST_EXPECT(parent.stopSignaled == false);
            auto perfLog{getPerfLog(parent, WithFile::yes)};
            BEAST_EXPECT(parent.stopSignaled == true);

            // Start PerfLog and wait long enough for PerfLog::report()
            // to not be able to write to its file.  That should cause no
            // problems.
            parent.doStart();
            std::this_thread::sleep_for(parent.getLogInterval() * 10);
            parent.doStop();

            // Remove the file.
            remove(perfLogPath);
        }
        {
            // Put a write protected file where PerfLog wants to write its
            // file.  Make sure that PerfLog tries to shutdown the server
            // since it can't open its file.
            PerfLogParent parent{j_};
            if (!BEAST_EXPECT(!exists(perfLogPath)))
                return;

            // Construct and write protect a file to prevent PerfLog
            // from creating its file.
            boost::system::error_code ec;
            boost::filesystem::create_directories(perfLogPath, ec);
            if (!BEAST_EXPECT(!ec))
                return;

            auto fileWriteable = [](boost::filesystem::path const& p) -> bool {
                return std::ofstream{p.c_str(), std::ios::out | std::ios::app}
                    .is_open();
            };

            if (!BEAST_EXPECT(fileWriteable(fullPath)))
                return;

            boost::filesystem::permissions(
                fullPath,
                perms::remove_perms | perms::owner_write | perms::others_write |
                    perms::group_write);

            // If the test is running as root, then the write protect may have
            // no effect.  Make sure write protect worked before proceeding.
            if (fileWriteable(fullPath))
            {
                log << "Unable to write protect file.  Test skipped."
                    << std::endl;
                return;
            }

            // Now construct a PerfLog.  The PerfLog should attempt to shut
            // down the server because it can't open its file.
            BEAST_EXPECT(parent.stopSignaled == false);
            auto perfLog{getPerfLog(parent, WithFile::yes)};
            BEAST_EXPECT(parent.stopSignaled == true);

            // Start PerfLog and wait long enough for PerfLog::report()
            // to not be able to write to its file.  That should cause no
            // problems.
            parent.doStart();
            std::this_thread::sleep_for(parent.getLogInterval() * 10);
            parent.doStop();

            // Fix file permissions so the file can be cleaned up.
            boost::filesystem::permissions(
                fullPath,
                perms::add_perms | perms::owner_write | perms::others_write |
                    perms::group_write);
        }
    }

    void
    testRPC(WithFile withFile)
    {
        // Exercise the rpc interfaces of PerfLog.
        // Start up the PerfLog that we'll use for testing.
        PerfLogParent parent{j_};
        auto perfLog{getPerfLog(parent, withFile)};
        parent.doStart();

        // Get the all the labels we can use for RPC interfaces without
        // causing an assert.
        std::vector<char const*> labels{ripple::RPC::getHandlerNames()};
        std::shuffle(labels.begin(), labels.end(), default_prng());

        // Get two IDs to associate with each label.  Errors tend to happen at
        // boundaries, so we pick IDs starting from zero and ending at
        // std::uint64_t>::max().
        std::vector<std::uint64_t> ids;
        ids.reserve(labels.size() * 2);
        std::generate_n(
            std::back_inserter(ids),
            labels.size(),
            [i = std::numeric_limits<std::uint64_t>::min()]() mutable {
                return i++;
            });
        std::generate_n(
            std::back_inserter(ids),
            labels.size(),
            [i = std::numeric_limits<std::uint64_t>::max()]() mutable {
                return i--;
            });
        std::shuffle(ids.begin(), ids.end(), default_prng());

        // Start all of the RPC commands twice to show they can all be tracked
        // simultaneously.
        for (int labelIndex = 0; labelIndex < labels.size(); ++labelIndex)
        {
            for (int idIndex = 0; idIndex < 2; ++idIndex)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                perfLog->rpcStart(
                    labels[labelIndex], ids[(labelIndex * 2) + idIndex]);
            }
        }
        {
            // Examine current PerfLog::counterJson() values.
            Json::Value const countersJson{perfLog->countersJson()["rpc"]};
            BEAST_EXPECT(countersJson.size() == labels.size() + 1);
            for (auto& label : labels)
            {
                // Expect every label in labels to have the same contents.
                Json::Value const& counter{countersJson[label]};
                BEAST_EXPECT(counter[jss::duration_us] == "0");
                BEAST_EXPECT(counter[jss::errored] == "0");
                BEAST_EXPECT(counter[jss::finished] == "0");
                BEAST_EXPECT(counter[jss::started] == "2");
            }
            // Expect "total" to have a lot of "started"
            Json::Value const& total{countersJson[jss::total]};
            BEAST_EXPECT(total[jss::duration_us] == "0");
            BEAST_EXPECT(total[jss::errored] == "0");
            BEAST_EXPECT(total[jss::finished] == "0");
            BEAST_EXPECT(jsonToUint64(total[jss::started]) == ids.size());
        }
        {
            // Verify that every entry in labels appears twice in currents.
            // If we sort by duration_us they should be in the order the
            // rpcStart() call was made.
            std::vector<Cur> const currents{
                getSortedCurrent(perfLog->currentJson()[jss::methods])};
            BEAST_EXPECT(currents.size() == labels.size() * 2);

            std::uint64_t prevDur = std::numeric_limits<std::uint64_t>::max();
            for (int i = 0; i < currents.size(); ++i)
            {
                BEAST_EXPECT(currents[i].name == labels[i / 2]);
                BEAST_EXPECT(prevDur > currents[i].dur);
                prevDur = currents[i].dur;
            }
        }

        // Finish all but the first RPC command in reverse order to show that
        // the start and finish of the commands can interleave.  Half of the
        // commands finish correctly, the other half with errors.
        for (int labelIndex = labels.size() - 1; labelIndex > 0; --labelIndex)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            perfLog->rpcFinish(labels[labelIndex], ids[(labelIndex * 2) + 1]);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            perfLog->rpcError(labels[labelIndex], ids[(labelIndex * 2) + 0]);
        }
        perfLog->rpcFinish(labels[0], ids[0 + 1]);
        // Note that label[0] id[0] is intentionally left unfinished.

        auto validateFinalCounters = [this, &labels](
                                         Json::Value const& countersJson) {
            {
                Json::Value const& jobQueue = countersJson[jss::job_queue];
                BEAST_EXPECT(jobQueue.isObject());
                BEAST_EXPECT(jobQueue.size() == 0);
            }

            Json::Value const& rpc = countersJson[jss::rpc];
            BEAST_EXPECT(rpc.size() == labels.size() + 1);

            // Verify that every entry in labels appears in rpc.
            // If we access the entries by label we should be able to correlate
            // their durations with the appropriate labels.
            {
                // The first label is special.  It should have "errored" : "0".
                Json::Value const& first = rpc[labels[0]];
                BEAST_EXPECT(first[jss::duration_us] != "0");
                BEAST_EXPECT(first[jss::errored] == "0");
                BEAST_EXPECT(first[jss::finished] == "1");
                BEAST_EXPECT(first[jss::started] == "2");
            }

            // Check the rest of the labels.
            std::uint64_t prevDur = std::numeric_limits<std::uint64_t>::max();
            for (int i = 1; i < labels.size(); ++i)
            {
                Json::Value const& counter{rpc[labels[i]]};
                std::uint64_t const dur{
                    jsonToUint64(counter[jss::duration_us])};
                BEAST_EXPECT(dur != 0 && dur < prevDur);
                prevDur = dur;
                BEAST_EXPECT(counter[jss::errored] == "1");
                BEAST_EXPECT(counter[jss::finished] == "1");
                BEAST_EXPECT(counter[jss::started] == "2");
            }

            // Check "total"
            Json::Value const& total{rpc[jss::total]};
            BEAST_EXPECT(total[jss::duration_us] != "0");
            BEAST_EXPECT(
                jsonToUint64(total[jss::errored]) == labels.size() - 1);
            BEAST_EXPECT(jsonToUint64(total[jss::finished]) == labels.size());
            BEAST_EXPECT(
                jsonToUint64(total[jss::started]) == labels.size() * 2);
        };

        auto validateFinalCurrent = [this,
                                     &labels](Json::Value const& currentJson) {
            {
                Json::Value const& job_queue = currentJson[jss::jobs];
                BEAST_EXPECT(job_queue.isArray());
                BEAST_EXPECT(job_queue.size() == 0);
            }

            Json::Value const& methods = currentJson[jss::methods];
            BEAST_EXPECT(methods.size() == 1);
            BEAST_EXPECT(methods.isArray());

            Json::Value const& only = methods[0u];
            BEAST_EXPECT(only.size() == 2);
            BEAST_EXPECT(only.isObject());
            BEAST_EXPECT(only[jss::duration_us] != "0");
            BEAST_EXPECT(only[jss::method] == labels[0]);
        };

        // Validate the final state of the PerfLog.
        validateFinalCounters(perfLog->countersJson());
        validateFinalCurrent(perfLog->currentJson());

        // Give the PerfLog enough time to flush it's state to the file.
        waitForFileUpdate(parent);

        // Politely stop the PerfLog.
        parent.doStop();

        auto const fullPath =
            parent.getPerfLogPath() / parent.getPerfLogFileName();

        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(fullPath));
        }
        else
        {
            // The last line in the log file should contain the same
            // information that countersJson() and currentJson() returned.
            // Verify that.

            // Get the last line of the log.
            std::ifstream logStream(fullPath.c_str());
            std::string lastLine;
            for (std::string line; std::getline(logStream, line);)
            {
                if (!line.empty())
                    lastLine = std::move(line);
            }

            Json::Value parsedLastLine;
            Json::Reader().parse(lastLine, parsedLastLine);
            if (!BEAST_EXPECT(!RPC::contains_error(parsedLastLine)))
                // Avoid cascade of failures
                return;

            // Validate the contents of the last line of the log.
            validateFinalCounters(parsedLastLine[jss::counters]);
            validateFinalCurrent(parsedLastLine[jss::current_activities]);
        }
    }

    void
    testJobs(WithFile withFile)
    {
        using namespace std::chrono;

        // Exercise the jobs interfaces of PerfLog.
        // Start up the PerfLog that we'll use for testing.
        PerfLogParent parent{j_};
        auto perfLog{getPerfLog(parent, withFile)};
        parent.doStart();

        // Get the all the JobTypes we can use to call the jobs interfaces
        // without causing an assert.
        struct JobName
        {
            JobType type;
            std::string typeName;

            JobName(JobType t, std::string name)
                : type(t), typeName(std::move(name))
            {
            }
        };

        std::vector<JobName> jobs;
        {
            auto const& jobTypes = JobTypes::instance();
            jobs.reserve(jobTypes.size());
            for (auto const& job : jobTypes)
            {
                jobs.emplace_back(job.first, job.second.name());
            }
        }
        std::shuffle(jobs.begin(), jobs.end(), default_prng());

        // Walk through all of the jobs, enqueuing every job once.  Check
        // the jobs data with every addition.
        for (int i = 0; i < jobs.size(); ++i)
        {
            perfLog->jobQueue(jobs[i].type);
            Json::Value const jq_counters{
                perfLog->countersJson()[jss::job_queue]};

            BEAST_EXPECT(jq_counters.size() == i + 2);
            for (int j = 0; j <= i; ++j)
            {
                // Verify all expected counters are present and contain
                // expected values.
                Json::Value const& counter{jq_counters[jobs[j].typeName]};
                BEAST_EXPECT(counter.size() == 5);
                BEAST_EXPECT(counter[jss::queued] == "1");
                BEAST_EXPECT(counter[jss::started] == "0");
                BEAST_EXPECT(counter[jss::finished] == "0");
                BEAST_EXPECT(counter[jss::queued_duration_us] == "0");
                BEAST_EXPECT(counter[jss::running_duration_us] == "0");
            }

            // Verify jss::total is present and has expected values.
            Json::Value const& total{jq_counters[jss::total]};
            BEAST_EXPECT(total.size() == 5);
            BEAST_EXPECT(jsonToUint64(total[jss::queued]) == i + 1);
            BEAST_EXPECT(total[jss::started] == "0");
            BEAST_EXPECT(total[jss::finished] == "0");
            BEAST_EXPECT(total[jss::queued_duration_us] == "0");
            BEAST_EXPECT(total[jss::running_duration_us] == "0");
        }

        // Even with jobs queued, the perfLog should report nothing current.
        {
            Json::Value current{perfLog->currentJson()};
            BEAST_EXPECT(current.size() == 2);
            BEAST_EXPECT(current.isMember(jss::jobs));
            BEAST_EXPECT(current[jss::jobs].size() == 0);
            BEAST_EXPECT(current.isMember(jss::methods));
            BEAST_EXPECT(current[jss::methods].size() == 0);
        }

        // Current jobs are tracked by Worker ID.  Even though it's not
        // realistic, crank up the number of workers so we can have many
        // jobs in process simultaneously without problems.
        perfLog->resizeJobs(jobs.size() * 2);

        // Start two instances of every job to show that the same job can run
        // simultaneously (on different Worker threads).  Admittedly, this
        // will make the jss::queued count look a bit goofy since there will
        // be half as many queued as started...
        for (int i = 0; i < jobs.size(); ++i)
        {
            perfLog->jobStart(
                jobs[i].type, microseconds{i + 1}, steady_clock::now(), i * 2);
            std::this_thread::sleep_for(microseconds(10));

            // Check each jobType counter entry.
            Json::Value const jq_counters{
                perfLog->countersJson()[jss::job_queue]};
            for (int j = 0; j < jobs.size(); ++j)
            {
                Json::Value const& counter{jq_counters[jobs[j].typeName]};
                std::uint64_t const queued_dur_us{
                    jsonToUint64(counter[jss::queued_duration_us])};
                if (j < i)
                {
                    BEAST_EXPECT(counter[jss::started] == "2");
                    BEAST_EXPECT(queued_dur_us == j + 1);
                }
                else if (j == i)
                {
                    BEAST_EXPECT(counter[jss::started] == "1");
                    BEAST_EXPECT(queued_dur_us == j + 1);
                }
                else
                {
                    BEAST_EXPECT(counter[jss::started] == "0");
                    BEAST_EXPECT(queued_dur_us == 0);
                }

                BEAST_EXPECT(counter[jss::queued] == "1");
                BEAST_EXPECT(counter[jss::finished] == "0");
                BEAST_EXPECT(counter[jss::running_duration_us] == "0");
            }
            {
                // Verify values in jss::total are what we expect.
                Json::Value const& total{jq_counters[jss::total]};
                BEAST_EXPECT(jsonToUint64(total[jss::queued]) == jobs.size());
                BEAST_EXPECT(jsonToUint64(total[jss::started]) == (i * 2) + 1);
                BEAST_EXPECT(total[jss::finished] == "0");

                // Total queued duration is triangle number of (i + 1).
                BEAST_EXPECT(
                    jsonToUint64(total[jss::queued_duration_us]) ==
                    (((i * i) + 3 * i + 2) / 2));
                BEAST_EXPECT(total[jss::running_duration_us] == "0");
            }

            perfLog->jobStart(
                jobs[i].type,
                microseconds{0},
                steady_clock::now(),
                (i * 2) + 1);
            std::this_thread::sleep_for(microseconds{10});

            // Verify that every entry in jobs appears twice in currents.
            // If we sort by duration_us they should be in the order the
            // rpcStart() call was made.
            std::vector<Cur> const currents{
                getSortedCurrent(perfLog->currentJson()[jss::jobs])};
            BEAST_EXPECT(currents.size() == (i + 1) * 2);

            std::uint64_t prevDur = std::numeric_limits<std::uint64_t>::max();
            for (int j = 0; j <= i; ++j)
            {
                BEAST_EXPECT(currents[j * 2].name == jobs[j].typeName);
                BEAST_EXPECT(prevDur > currents[j * 2].dur);
                prevDur = currents[j * 2].dur;

                BEAST_EXPECT(currents[(j * 2) + 1].name == jobs[j].typeName);
                BEAST_EXPECT(prevDur > currents[(j * 2) + 1].dur);
                prevDur = currents[(j * 2) + 1].dur;
            }
        }

        // Finish every job we started.  Finish them in reverse.
        for (int i = jobs.size() - 1; i >= 0; --i)
        {
            // A number of the computations in this loop care about the
            // number of jobs that have finished.  Make that available.
            int const finished = ((jobs.size() - i) * 2) - 1;
            perfLog->jobFinish(
                jobs[i].type, microseconds(finished), (i * 2) + 1);
            std::this_thread::sleep_for(microseconds(10));

            Json::Value const jq_counters{
                perfLog->countersJson()[jss::job_queue]};
            for (int j = 0; j < jobs.size(); ++j)
            {
                Json::Value const& counter{jq_counters[jobs[j].typeName]};
                std::uint64_t const running_dur_us{
                    jsonToUint64(counter[jss::running_duration_us])};
                if (j < i)
                {
                    BEAST_EXPECT(counter[jss::finished] == "0");
                    BEAST_EXPECT(running_dur_us == 0);
                }
                else if (j == i)
                {
                    BEAST_EXPECT(counter[jss::finished] == "1");
                    BEAST_EXPECT(running_dur_us == ((jobs.size() - j) * 2) - 1);
                }
                else
                {
                    BEAST_EXPECT(counter[jss::finished] == "2");
                    BEAST_EXPECT(running_dur_us == ((jobs.size() - j) * 4) - 1);
                }

                std::uint64_t const queued_dur_us{
                    jsonToUint64(counter[jss::queued_duration_us])};
                BEAST_EXPECT(queued_dur_us == j + 1);
                BEAST_EXPECT(counter[jss::queued] == "1");
                BEAST_EXPECT(counter[jss::started] == "2");
            }
            {
                // Verify values in jss::total are what we expect.
                Json::Value const& total{jq_counters[jss::total]};
                BEAST_EXPECT(jsonToUint64(total[jss::queued]) == jobs.size());
                BEAST_EXPECT(
                    jsonToUint64(total[jss::started]) == jobs.size() * 2);
                BEAST_EXPECT(jsonToUint64(total[jss::finished]) == finished);

                // Total queued duration should be triangle number of
                // jobs.size().
                int const queuedDur = ((jobs.size() * (jobs.size() + 1)) / 2);
                BEAST_EXPECT(
                    jsonToUint64(total[jss::queued_duration_us]) == queuedDur);

                // Total running duration should be triangle number of finished.
                int const runningDur = ((finished * (finished + 1)) / 2);
                BEAST_EXPECT(
                    jsonToUint64(total[jss::running_duration_us]) ==
                    runningDur);
            }

            perfLog->jobFinish(
                jobs[i].type, microseconds(finished + 1), (i * 2));
            std::this_thread::sleep_for(microseconds(10));

            // Verify that the two jobs we just finished no longer appear in
            // currents.
            std::vector<Cur> const currents{
                getSortedCurrent(perfLog->currentJson()[jss::jobs])};
            BEAST_EXPECT(currents.size() == i * 2);

            std::uint64_t prevDur = std::numeric_limits<std::uint64_t>::max();
            for (int j = 0; j < i; ++j)
            {
                BEAST_EXPECT(currents[j * 2].name == jobs[j].typeName);
                BEAST_EXPECT(prevDur > currents[j * 2].dur);
                prevDur = currents[j * 2].dur;

                BEAST_EXPECT(currents[(j * 2) + 1].name == jobs[j].typeName);
                BEAST_EXPECT(prevDur > currents[(j * 2) + 1].dur);
                prevDur = currents[(j * 2) + 1].dur;
            }
        }

        // Validate the final results.
        auto validateFinalCounters = [this,
                                      &jobs](Json::Value const& countersJson) {
            {
                Json::Value const& rpc = countersJson[jss::rpc];
                BEAST_EXPECT(rpc.isObject());
                BEAST_EXPECT(rpc.size() == 0);
            }

            Json::Value const& jobQueue = countersJson[jss::job_queue];
            for (int i = jobs.size() - 1; i >= 0; --i)
            {
                Json::Value const& counter{jobQueue[jobs[i].typeName]};
                std::uint64_t const running_dur_us{
                    jsonToUint64(counter[jss::running_duration_us])};
                BEAST_EXPECT(running_dur_us == ((jobs.size() - i) * 4) - 1);

                std::uint64_t const queued_dur_us{
                    jsonToUint64(counter[jss::queued_duration_us])};
                BEAST_EXPECT(queued_dur_us == i + 1);

                BEAST_EXPECT(counter[jss::queued] == "1");
                BEAST_EXPECT(counter[jss::started] == "2");
                BEAST_EXPECT(counter[jss::finished] == "2");
            }

            // Verify values in jss::total are what we expect.
            Json::Value const& total{jobQueue[jss::total]};
            const int finished = jobs.size() * 2;
            BEAST_EXPECT(jsonToUint64(total[jss::queued]) == jobs.size());
            BEAST_EXPECT(jsonToUint64(total[jss::started]) == finished);
            BEAST_EXPECT(jsonToUint64(total[jss::finished]) == finished);

            // Total queued duration should be triangle number of
            // jobs.size().
            int const queuedDur = ((jobs.size() * (jobs.size() + 1)) / 2);
            BEAST_EXPECT(
                jsonToUint64(total[jss::queued_duration_us]) == queuedDur);

            // Total running duration should be triangle number of finished.
            int const runningDur = ((finished * (finished + 1)) / 2);
            BEAST_EXPECT(
                jsonToUint64(total[jss::running_duration_us]) == runningDur);
        };

        auto validateFinalCurrent = [this](Json::Value const& currentJson) {
            {
                Json::Value const& j = currentJson[jss::jobs];
                BEAST_EXPECT(j.isArray());
                BEAST_EXPECT(j.size() == 0);
            }

            Json::Value const& methods = currentJson[jss::methods];
            BEAST_EXPECT(methods.size() == 0);
            BEAST_EXPECT(methods.isArray());
        };

        // Validate the final state of the PerfLog.
        validateFinalCounters(perfLog->countersJson());
        validateFinalCurrent(perfLog->currentJson());

        // Give the PerfLog enough time to flush it's state to the file.
        waitForFileUpdate(parent);

        // Politely stop the PerfLog.
        parent.doStop();

        // Check file contents if that is appropriate.
        auto const fullPath =
            parent.getPerfLogPath() / parent.getPerfLogFileName();

        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(fullPath));
        }
        else
        {
            // The last line in the log file should contain the same
            // information that countersJson() and currentJson() returned.
            // Verify that.

            // Get the last line of the log.
            std::ifstream logStream(fullPath.c_str());
            std::string lastLine;
            for (std::string line; std::getline(logStream, line);)
            {
                if (!line.empty())
                    lastLine = std::move(line);
            }

            Json::Value parsedLastLine;
            Json::Reader().parse(lastLine, parsedLastLine);
            if (!BEAST_EXPECT(!RPC::contains_error(parsedLastLine)))
                // Avoid cascade of failures
                return;

            // Validate the contents of the last line of the log.
            validateFinalCounters(parsedLastLine[jss::counters]);
            validateFinalCurrent(parsedLastLine[jss::current_activities]);
        }
    }

    void
    testInvalidID(WithFile withFile)
    {
        using namespace std::chrono;

        // The Worker ID is used to identify jobs in progress.  Show that
        // the PerLog behaves as well as possible if an invalid ID is passed.

        // Start up the PerfLog that we'll use for testing.
        PerfLogParent parent{j_};
        auto perfLog{getPerfLog(parent, withFile)};
        parent.doStart();

        // Randomly select a job type and its name.
        JobType jobType;
        std::string jobTypeName;
        {
            auto const& jobTypes = JobTypes::instance();

            std::uniform_int_distribution<> dis(0, jobTypes.size() - 1);
            auto iter{jobTypes.begin()};
            std::advance(iter, dis(default_prng()));

            jobType = iter->second.type();
            jobTypeName = iter->second.name();
        }

        // Say there's one worker thread.
        perfLog->resizeJobs(1);

        // Lambda to validate countersJson for this test.
        auto verifyCounters = [this, jobTypeName](
                                  Json::Value const& countersJson,
                                  int started,
                                  int finished,
                                  int queued_us,
                                  int running_us) {
            BEAST_EXPECT(countersJson.isObject());
            BEAST_EXPECT(countersJson.size() == 2);

            BEAST_EXPECT(countersJson.isMember(jss::rpc));
            BEAST_EXPECT(countersJson[jss::rpc].isObject());
            BEAST_EXPECT(countersJson[jss::rpc].size() == 0);

            BEAST_EXPECT(countersJson.isMember(jss::job_queue));
            BEAST_EXPECT(countersJson[jss::job_queue].isObject());
            BEAST_EXPECT(countersJson[jss::job_queue].size() == 1);
            {
                Json::Value const& job{
                    countersJson[jss::job_queue][jobTypeName]};

                BEAST_EXPECT(job.isObject());
                BEAST_EXPECT(jsonToUint64(job[jss::queued]) == 0);
                BEAST_EXPECT(jsonToUint64(job[jss::started]) == started);
                BEAST_EXPECT(jsonToUint64(job[jss::finished]) == finished);

                BEAST_EXPECT(
                    jsonToUint64(job[jss::queued_duration_us]) == queued_us);
                BEAST_EXPECT(
                    jsonToUint64(job[jss::running_duration_us]) == running_us);
            }
        };

        // Lambda to validate currentJson (always empty) for this test.
        auto verifyEmptyCurrent = [this](Json::Value const& currentJson) {
            BEAST_EXPECT(currentJson.isObject());
            BEAST_EXPECT(currentJson.size() == 2);

            BEAST_EXPECT(currentJson.isMember(jss::jobs));
            BEAST_EXPECT(currentJson[jss::jobs].isArray());
            BEAST_EXPECT(currentJson[jss::jobs].size() == 0);

            BEAST_EXPECT(currentJson.isMember(jss::methods));
            BEAST_EXPECT(currentJson[jss::methods].isArray());
            BEAST_EXPECT(currentJson[jss::methods].size() == 0);
        };

        // Start an ID that's too large.
        perfLog->jobStart(jobType, microseconds{11}, steady_clock::now(), 2);
        std::this_thread::sleep_for(microseconds{10});
        verifyCounters(perfLog->countersJson(), 1, 0, 11, 0);
        verifyEmptyCurrent(perfLog->currentJson());

        // Start a negative ID
        perfLog->jobStart(jobType, microseconds{13}, steady_clock::now(), -1);
        std::this_thread::sleep_for(microseconds{10});
        verifyCounters(perfLog->countersJson(), 2, 0, 24, 0);
        verifyEmptyCurrent(perfLog->currentJson());

        // Finish the too large ID
        perfLog->jobFinish(jobType, microseconds{17}, 2);
        std::this_thread::sleep_for(microseconds{10});
        verifyCounters(perfLog->countersJson(), 2, 1, 24, 17);
        verifyEmptyCurrent(perfLog->currentJson());

        // Finish the negative ID
        perfLog->jobFinish(jobType, microseconds{19}, -1);
        std::this_thread::sleep_for(microseconds{10});
        verifyCounters(perfLog->countersJson(), 2, 2, 24, 36);
        verifyEmptyCurrent(perfLog->currentJson());

        // Give the PerfLog enough time to flush it's state to the file.
        waitForFileUpdate(parent);

        // Politely stop the PerfLog.
        parent.doStop();

        // Check file contents if that is appropriate.
        auto const fullPath =
            parent.getPerfLogPath() / parent.getPerfLogFileName();

        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(fullPath));
        }
        else
        {
            // The last line in the log file should contain the same
            // information that countersJson() and currentJson() returned.
            // Verify that.

            // Get the last line of the log.
            std::ifstream logStream(fullPath.c_str());
            std::string lastLine;
            for (std::string line; std::getline(logStream, line);)
            {
                if (!line.empty())
                    lastLine = std::move(line);
            }

            Json::Value parsedLastLine;
            Json::Reader().parse(lastLine, parsedLastLine);
            if (!BEAST_EXPECT(!RPC::contains_error(parsedLastLine)))
                // Avoid cascade of failures
                return;

            // Validate the contents of the last line of the log.
            verifyCounters(parsedLastLine[jss::counters], 2, 2, 24, 36);
            verifyEmptyCurrent(parsedLastLine[jss::current_activities]);
        }
    }

    void
    testRotate(WithFile withFile)
    {
        // We can't fully test rotate because unit tests must run on Windows,
        // and Windows doesn't (may not?) support rotate.  But at least call
        // the interface and see that it doesn't crash.
        using namespace boost::filesystem;

        auto const perfLogPath{PerfLogParent::getPerfLogPath()};
        auto const fullPath = perfLogPath / PerfLogParent::getPerfLogFileName();

        PerfLogParent parent{j_};
        BEAST_EXPECT(!exists(perfLogPath));

        auto perfLog{getPerfLog(parent, withFile)};

        BEAST_EXPECT(parent.stopSignaled == false);
        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(perfLogPath));
        }
        else
        {
            BEAST_EXPECT(exists(fullPath));
            BEAST_EXPECT(file_size(fullPath) == 0);
        }

        // Start PerfLog and wait long enough for PerfLog::report()
        // to write to its file.
        parent.doStart();
        waitForFileUpdate(parent);

        decltype(file_size(fullPath)) firstFileSize{0};
        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(perfLogPath));
        }
        else
        {
            firstFileSize = file_size(fullPath);
            BEAST_EXPECT(firstFileSize > 0);
        }

        // Rotate and then wait to make sure more stuff is written to the file.
        perfLog->rotate();
        waitForFileUpdate(parent);

        parent.doStop();

        if (withFile == WithFile::no)
        {
            BEAST_EXPECT(!exists(perfLogPath));
        }
        else
        {
            BEAST_EXPECT(file_size(fullPath) > firstFileSize);
        }
    }

    void
    run() override
    {
        testFileCreation();
        testRPC(WithFile::no);
        testRPC(WithFile::yes);
        testJobs(WithFile::no);
        testJobs(WithFile::yes);
        testInvalidID(WithFile::no);
        testInvalidID(WithFile::yes);
        testRotate(WithFile::no);
        testRotate(WithFile::yes);
    }
};

BEAST_DEFINE_TESTSUITE(PerfLog, basics, ripple);

}  // namespace ripple
