/// @file
/// @brief TxCheck mode entry point - full testing logic extracted from tx_main.cc
/// Contains the fork-based test loop, signal handling, reproduce mode, and helpers.

#include "txcheck/tx_main.hh"
#include "txcheck/transaction_test.hh"
#include "core/general_process.hh"
#include "txcheck/tx_general_process.hh"

#include <iostream>
#include <chrono>
#include <thread>

#ifndef HAVE_BOOST_REGEX
#include <regex>
#else
#include <boost/regex.hpp>
using boost::regex;
using boost::smatch;
using boost::regex_match;
#endif

#include "core/random.hh"
#include "grammar/grammar.hh"
#include "core/relmodel.hh"
#include "schema/schema.hh"

#include "core/log.hh"
#include "core/dump.hh"
#include "core/impedance.hh"
#include "schema/dut.hh"

#include <sys/time.h>
#include <sys/wait.h>

#ifdef __linux__
#include <sched.h>
#endif

using namespace std;
using namespace std::chrono;

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define NORMAL_EXIT 0
#define FIND_BUG_EXIT 7
#define MAX_TIMEOUT_TIME 3
#define MAX_SETUP_TRY_TIME 3

// ---------------------------------------------------------------------------
// Globals (must be defined here; tx_general_process.hh declares them extern)
// ---------------------------------------------------------------------------
pthread_mutex_t mutex_timeout;
pthread_cond_t  cond_timeout;

static int child_pid = 0;
static bool child_timed_out = false;

extern int write_op_id;

// ---------------------------------------------------------------------------
// Signal handler for SIGALRM - kills the child process on timeout
// ---------------------------------------------------------------------------
static void kill_process_signal(int signal)
{
    if (signal != SIGALRM) {
        printf("unexpect signal %d\n", signal);
        exit(1);
    }

    if (child_pid > 0) {
        printf("child pid timeout, kill it\n");
        child_timed_out = true;
        kill(child_pid, SIGKILL);
        // also kill server process to restart
        while (transaction_test::try_to_kill_server() == false) {}
    }

    cerr << "get SIGALRM, stop the process" << endl;
    return;
}

// ---------------------------------------------------------------------------
// Fork child to generate the initial database (tables + seed data)
// ---------------------------------------------------------------------------
static int fork_for_generating_database(dbms_info &d_info)
{
    static itimerval itimer;
    transaction_test::fork_if_server_closed(d_info);

    write_op_id = 0;
    child_pid = fork();
    if (child_pid == 0) { // in child process
        generate_database(d_info);
        ofstream output_wkey("wkey.txt");
        output_wkey << write_op_id << endl;
        output_wkey.close();
        exit(NORMAL_EXIT);
    }

    itimer.it_value.tv_sec = TRANSACTION_TIMEOUT;
    itimer.it_value.tv_usec = 0; // us limit
    setitimer(ITIMER_REAL, &itimer, NULL);

    int status;
    auto res = waitpid(child_pid, &status, 0);
    if (res <= 0) {
        cerr << "waitpid() fail: " << res << endl;
        throw runtime_error(string("waitpid() fail"));
    }

    if (!WIFSTOPPED(status))
        child_pid = 0;

    itimer.it_value.tv_sec = 0;
    itimer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &itimer, NULL);

    if (WIFEXITED(status)) {
        auto exit_code = WEXITSTATUS(status); // only low 8 bit (max 255)
        if (exit_code == FIND_BUG_EXIT) {
            cerr << RED << "a bug is found in fork process" << RESET << endl;
            transaction_test::record_bug_num++;
            exit(-1);
        }
        if (exit_code == 255)
            exit(-1);
    }

    if (WIFSIGNALED(status)) {
        auto killSignal = WTERMSIG(status);
        if (child_timed_out && killSignal == SIGKILL) {
            throw runtime_error(string("transaction test timeout"));
        } else {
            cerr << RED << "find memory bug" << RESET << endl;
            cerr << "killSignal: " << killSignal << endl;
            throw runtime_error(string("memory bug"));
        }
    }

    ifstream input_wkey("wkey.txt");
    input_wkey >> write_op_id;
    input_wkey.close();

    write_op_id++;

    return 0;
}

// ---------------------------------------------------------------------------
// Fork child to run one round of transaction testing
// ---------------------------------------------------------------------------
static int fork_for_transaction_test(dbms_info &d_info)
{
    static itimerval itimer;

    transaction_test::fork_if_server_closed(d_info);

    child_pid = fork();
    if (child_pid == 0) { // in child process
        try {
            transaction_test tt(d_info);
            auto ret = tt.test();
            if (ret == 1) {
                cerr << RED << "Find a bug !!!" << RESET << endl;
                exit(FIND_BUG_EXIT);
            }
        } catch (std::exception &e) { // ignore runtime error
            cerr << "in test: " << e.what() << endl;
        }
        exit(NORMAL_EXIT);
    }

    itimer.it_value.tv_sec = TRANSACTION_TIMEOUT;
    itimer.it_value.tv_usec = 0; // us limit
    setitimer(ITIMER_REAL, &itimer, NULL);

    int status;
    auto res = waitpid(child_pid, &status, 0);
    if (res <= 0) {
        cerr << "waitpid() fail: " << res << endl;
        throw runtime_error(string("waitpid() fail"));
    }

    if (!WIFSTOPPED(status))
        child_pid = 0;

    itimer.it_value.tv_sec = 0;
    itimer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &itimer, NULL);

    if (WIFEXITED(status)) {
        auto exit_code = WEXITSTATUS(status); // only low 8 bit (max 255)
        if (exit_code == FIND_BUG_EXIT) {
            cerr << RED << "a bug is found in fork process" << RESET << endl;
            transaction_test::record_bug_num++;
        }
        if (exit_code == 255)
            abort();
    }

    if (WIFSIGNALED(status)) {
        auto killSignal = WTERMSIG(status);
        if (child_timed_out && killSignal == SIGKILL) {
            throw runtime_error(string("transaction test timeout"));
        } else {
            cerr << RED << "find memory bug" << RESET << endl;
            cerr << "killSignal: " << killSignal << endl;
            abort();
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Run a batch of transaction tests against a freshly generated database
// ---------------------------------------------------------------------------
static int random_test(dbms_info &d_info)
{
    random_device rd;
    auto rand_seed = rd();
    cerr << "\n\n";
    cerr << "random seed for db: " << rand_seed << endl;
    smith::rng.seed(rand_seed);

    // reset the target DBMS to initial state
    int setup_try_time = 0;
    while (1) {
        if (setup_try_time > MAX_SETUP_TRY_TIME) {
            kill_process_with_SIGTERM(transaction_test::server_process_id);
            setup_try_time = 0;
        }

        try {
            // don't fork, so that the static schema can be used in each test case
            transaction_test::fork_if_server_closed(d_info);
            generate_database(d_info);
            break;
        } catch (std::exception &e) {
            cerr << e.what() << " in setup stage" << endl;
            setup_try_time++;
        }
    }

    int i = TEST_TIME_FOR_EACH_DB;
    while (i--) {
        // each round, generate random seed again, otherwise it will perform the same tests
        rand_seed = rd();
        cerr << "\n\n";
        cerr << "random seed for tests: " << rand_seed << endl;
        smith::rng.seed(rand_seed);

        try {
            fork_for_transaction_test(d_info);
        } catch (exception &e) {
            string err = e.what();
            cerr << "ERROR in random_test: " << err << endl;
            if (err == "restart server")
                break;
            else if (err == "transaction test timeout") {
                break; // break the test and begin a new test
                // after killing and starting a new server, created tables might be lost
                // so it needs to begin a new test to generate tables
            } else {
                cerr << "the exception cannot be handled" << endl;
                throw e;
            }
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// TxCheck mode entry point
// ---------------------------------------------------------------------------
void txcheck_run(dbms_info &d_info, map<string, string> &options)
{
    // --- Extract general options ---
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

    bool use_fixed_seed = false;
    unsigned int fixed_seed = 0;
    if (options.count("seed") > 0) {
        use_fixed_seed = true;
        fixed_seed = stoul(options["seed"]);
    }

    bool ignore_crash = options.count("ignore-crash") > 0;

    // --- Set up signal handlers ---

    // SIGUSR1 handler (used by child processes for timeout communication)
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = user_signal;
    if (sigaction(SIGUSR1, &action, NULL)) {
        cerr << "sigaction error" << endl;
        exit(1);
    }

    // SIGALRM handler (kills child process on timeout)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = kill_process_signal;
    if (sigaction(SIGALRM, &sa, NULL)) {
        cerr << "sigaction error" << endl;
        exit(1);
    }

    // --- Initialize synchronization primitives ---
    pthread_mutex_init(&mutex_timeout, NULL);
    pthread_cond_init(&cond_timeout, NULL);

    // --- Reproduce mode ---
    if (options.count("reproduce-sql")) {
        cerr << "enter reproduce mode" << endl;
        if (!options.count("reproduce-tid")) {
            cerr << "should also provide tid file" << endl;
            return;
        }

        // get stmt queue
        vector<shared_ptr<prod>> stmt_queue;
        ifstream stmt_file(options["reproduce-sql"]);
        stringstream buffer;
        buffer << stmt_file.rdbuf();
        stmt_file.close();

        string stmts(buffer.str());
        int old_off = 0;
        while (1) {
            int new_off = stmts.find(";\n\n", old_off);
            if (new_off == string::npos)
                break;

            auto each_sql = stmts.substr(old_off, new_off - old_off); // not include ;\n\n
            old_off = new_off + string(";\n\n").size();

            stmt_queue.push_back(make_shared<txn_string_stmt>((prod *)0, each_sql));
        }

        // get tid queue
        vector<int> tid_queue;
        ifstream tid_file(options["reproduce-tid"]);
        int tid;
        int max_tid = -1;
        while (tid_file >> tid) {
            tid_queue.push_back(tid);
            if (tid > max_tid)
                max_tid = tid;
        }
        tid_file.close();

        // get stmt use queue
        vector<stmt_usage> stmt_usage_queue;
        ifstream stmt_usage_file(options["reproduce-usage"]);
        int use;
        while (stmt_usage_file >> use) {
            switch (use) {
            case 0:
                stmt_usage_queue.push_back(stmt_usage(INIT_TYPE, false, "t_***"));
                break;
            case 1:
                stmt_usage_queue.push_back(stmt_usage(SELECT_READ, false, "t_***"));
                break;
            case 2:
                stmt_usage_queue.push_back(stmt_usage(UPDATE_WRITE, false, "t_***"));
                break;
            case 3:
                stmt_usage_queue.push_back(stmt_usage(INSERT_WRITE, false, "t_***"));
                break;
            case 4:
                stmt_usage_queue.push_back(stmt_usage(DELETE_WRITE, false, "t_***"));
                break;
            case 5:
                stmt_usage_queue.push_back(stmt_usage(BEFORE_WRITE_READ, true, "t_***"));
                break;
            case 6:
                stmt_usage_queue.push_back(stmt_usage(AFTER_WRITE_READ, true, "t_***"));
                break;
            case 7:
                stmt_usage_queue.push_back(stmt_usage(VERSION_SET_READ, true, "t_***"));
                break;
            default:
                cerr << "unknown stmt usage: " << use << endl;
                exit(-1);
                break;
            }
        }
        stmt_usage_file.close();

        auto backup_file = options["reproduce-backup"];
        use_backup_file(backup_file, d_info);

        if (options.count("min"))
            minimize_testcase(d_info, stmt_queue, tid_queue, stmt_usage_queue);
        else {
            string empty_str;
            reproduce_routine(d_info, stmt_queue, tid_queue, stmt_usage_queue, empty_str);
        }

        return;
    }

    // --- Main fuzzing loop ---
    while (1) {
        random_test(d_info);
    }
}
