/// @file
/// @brief Cross-mode entry point
/// Main loop that generates databases and runs cross_tester rounds,
/// following the same fork-based pattern as eet_run / txcheck_run.

#include "cross/cross_main.hh"
#include "cross/cross_tester.hh"
#include "core/general_process.hh"
#include "txcheck/transaction_test.hh"

#include <iostream>
#include <chrono>
#include <thread>
#include <random>

#ifdef __linux__
#include <sched.h>
#endif

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
}

using namespace std;

#define CROSS_DEFAULT_DB_TEST_TIME 10

// ---------------------------------------------------------------------------
// cross_run: entry point for cross-mode testing
// ---------------------------------------------------------------------------
void cross_run(dbms_info &d_info, map<string, string> &options) {
    // --- Extract options ---
    int db_test_time = CROSS_DEFAULT_DB_TEST_TIME;
    if (options.count("db-test-num") > 0)
        db_test_time = stoi(options["db-test-num"]);

    int table_num = 0;
    if (options.count("db-table-num") > 0)
        table_num = stoi(options["db-table-num"]);

    int cpu_affinity = -1;
    if (options.count("cpu-affinity") > 0)
        cpu_affinity = stoi(options["cpu-affinity"]);

#ifdef __linux__
    if (cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_affinity, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) < 0) {
            cerr << "Failed to set CPU affinity" << endl;
            exit(EXIT_FAILURE);
        }
    }
#endif

    // Pipe for parent/child communication (execution time stats)
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int round = 0;
    while (1) {
        cerr << "\n=== cross_run: round " << round << " ===" << endl;
        round++;

        auto child_pid = fork();
        if (child_pid == 0) {
            // ---- Child process ----
            close(pipefd[0]); // close unused read end

            random_device rd;
            auto rand_seed = rd();
            cerr << "random seed: " << rand_seed << endl;
            smith::rng.seed(rand_seed);

            // Generate database (require_pkey_wkey is true for MODE_CROSS)
            while (true) {
                try {
                    generate_database(d_info, table_num);
                    break;
                } catch (exception &e) {
                    string err = e.what();
                    if (err.find("BUG") != string::npos || err.find("CONNECTION FAIL") != string::npos) {
                        cerr << "fatal error in generate_database: " << err << endl;
                        abort();
                    }
                    cerr << "generate_database error (will retry): " << err << endl;
                    rand_seed = rd();
                    cerr << "retrying with seed: " << rand_seed << endl;
                    smith::rng.seed(rand_seed);
                }
            }
            cerr << "database generated" << endl;

            auto db_schema = get_schema(d_info);

            for (int i = 0; i < db_test_time; i++) {
                cerr << "\n[cross_test " << i << "/" << db_test_time << "]" << endl;

                // Re-seed each round for variety
                rand_seed = rd();
                smith::rng.seed(rand_seed);

                try {
                    cross_tester ct(d_info, db_schema);

                    // Pre-create bug directory for saving artifacts
                    string bug_dir = "found_bugs/cross_bug_r" + to_string(round)
                                     + "_t" + to_string(i) + "/";

                    if (ct.cross_test(bug_dir)) {
                        cerr << RED << "CROSS BUG FOUND in round " << round
                             << ", test " << i << RESET << endl;

                        // Ensure bug directory exists
                        make_dir_error_exit(bug_dir);

                        // Minimize the test case
                        ct.minimize(bug_dir);

                        exit(EXIT_FAILURE); // signal bug to parent
                    }
                } catch (exception &e) {
                    string err = e.what();
                    cerr << "cross_test exception: " << err << endl;
                    if (err.find("BUG") != string::npos) {
                        cerr << RED << "BUG triggered during cross_test: " << err << RESET << endl;
                        exit(EXIT_FAILURE);
                    }
                    // Non-fatal: continue to next test
                }
            }

            // No bug found — signal success to parent
            close(pipefd[1]);
            exit(EXIT_SUCCESS);
        }

        // ---- Parent process ----
        int status;
        auto res = waitpid(child_pid, &status, 0);
        if (res <= 0) {
            cerr << "cross_run waitpid() fail: " << res << endl;
            abort();
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
            cerr << RED << "cross_run: child reported a bug, aborting" << RESET << endl;
            abort();
        }

        if (WIFSIGNALED(status)) {
            auto signal_num = WTERMSIG(status);
            cerr << "cross_run: child killed by signal " << signal_num
                 << " (" << strsignal(signal_num) << ")" << endl;
            if (options.count("ignore-crash") > 0) {
                cerr << "ignore-crash enabled, sleeping 1 min then continuing" << endl;
                chrono::minutes duration(1);
                this_thread::sleep_for(duration);
            } else {
                abort();
            }
        }

        cerr << "round " << round << " done" << endl;
    }
}
