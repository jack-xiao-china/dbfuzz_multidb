/// @file
/// @brief Smoke mode: strict replication of sqlsmith runtime behavior
///
/// Transaction pattern:  ROLLBACK; BEGIN; <stmt>; ROLLBACK;
/// Session variables:    statement_timeout=1s, client_min_messages=ERROR
/// Error classification: broken(timeout/syntax/failure) matching sqlsmith
/// Connection recovery:  outer while(1) catches broken, sleep 1s, reconnect
/// Impedance feedback:   reuse dbfuzz core/impedance (identical to sqlsmith)
/// Logger chain:         impedance_feedback + cerr_logger + query_dumper

#include "config.h"
#include "smoke_main.hh"

#include "core/general_process.hh"
#include "core/relmodel.hh"
#include "core/impedance.hh"
#include "core/log.hh"
#include "core/known_errors.hh"
#include "core/dump.hh"
#include "core/random.hh"
#include "grammar/grammar.hh"
#include "schema/schema.hh"
#include "schema/dut.hh"

#include <iostream>
#include <fstream>
#include <sstream>
#include <csignal>
#include <chrono>
#include <thread>
#include <vector>

using namespace std;

// ── Global state (matching sqlsmith pattern) ──
static cerr_logger *global_cerr_logger = nullptr;
static vector<shared_ptr<logger>> smoke_loggers;

// ── Signal handler (matching sqlsmith cerr_log_handler) ──
extern "C" void smoke_signal_handler(int) {
    if (global_cerr_logger)
        global_cerr_logger->report();
    impedance::report(cerr);
    exit(1);
}

// ── Helper: translate dbfuzz DUT errors into sqlsmith-style categories ──
static bool is_connection_error(const string &err) {
    return err.find("connection") != string::npos
        || err.find("server closed") != string::npos
        || err.find("broken pipe") != string::npos
        || err.find("Connection refused") != string::npos
        || err.find("CONNECTION_BAD") != string::npos
        || err.find("Lost connection") != string::npos
        || err.find("MySQL server has gone") != string::npos
        || err.find("Can't connect") != string::npos;
}

static bool is_timeout_error(const string &err) {
    return err.find("statement timeout") != string::npos
        || err.find("canceling statement") != string::npos
        || err.find("Query execution was interrupted") != string::npos
        || err.find("lock wait timeout") != string::npos;
}

// ═══════════════════════════════════════════════════
// Main entry point
// ═══════════════════════════════════════════════════

void smoke_run(dbms_info &d_info, map<string, string> &options)
{
    // ── 1. Parse smoke-specific options (aligned with sqlsmith CLI) ──
    int max_queries = 0;     // 0 = unlimited
    bool verbose = false;
    bool dump_all_queries = false;
    bool dump_all_graphs = false;
    bool dry_run = false;
    bool exclude_catalog = false;

    if (options.count("max-queries"))
        max_queries = stoi(options["max-queries"]);
    if (options.count("verbose"))
        verbose = true;
    if (options.count("dump-all-queries"))
        dump_all_queries = true;
    if (options.count("dump-all-graphs"))
        dump_all_graphs = true;
    if (options.count("dry-run"))
        dry_run = true;
    if (options.count("exclude-catalog"))
        exclude_catalog = true;

    // ── RNG state restoration (matching sqlsmith --rng-state) ──
    string rng_state_in, rng_state_out;
    if (options.count("rng-state"))
        rng_state_in = options["rng-state"];
    if (options.count("rng-state-out"))
        rng_state_out = options["rng-state-out"];

    if (!rng_state_in.empty()) {
        try {
            rng_state_deserialize(rng_state_in);
            cerr << "[SMOKE] RNG state restored from --rng-state" << endl;
        } catch (const exception &e) {
            cerr << "[SMOKE] Warning: failed to restore RNG state: " << e.what() << endl;
        }
    }

    // ── 2. Build logger chain (matching sqlsmith) ──
    // impedance_feedback is always active (same as sqlsmith)
    smoke_loggers.push_back(make_shared<impedance_feedback>());

    if (verbose) {
        auto l = make_shared<cerr_logger>();
        global_cerr_logger = &*l;
        smoke_loggers.push_back(l);
        signal(SIGINT, smoke_signal_handler);
    }

    if (dump_all_queries)
        smoke_loggers.push_back(make_shared<query_dumper>());

    if (dump_all_graphs)
        smoke_loggers.push_back(make_shared<ast_logger>());

    // ── Known error filter (matching sqlsmith known.txt/known_re.txt) ──
    known_errors_init();

    // ── 3. Initialize DUT and Schema ──
    cerr << "[SMOKE] Connecting to " << d_info.dbms_name
         << " " << d_info.host_addr << ":" << d_info.test_port
         << " db=" << d_info.test_db << " ..." << endl;

    shared_ptr<dut_base> conn;
    shared_ptr<schema> db_schema;

    try {
        conn = dut_setup(d_info);
        db_schema = get_schema(d_info);
    } catch (const exception &e) {
        cerr << "[SMOKE] Failed to initialize: " << e.what() << endl;
        return;
    }

    cerr << "[SMOKE] Schema loaded: " << db_schema->tables.size() << " tables, "
         << db_schema->operators.size() << " operators, "
         << db_schema->routines.size() << " routines, "
         << db_schema->aggregates.size() << " aggregates" << endl;

    // ── 4. Set session variables (matching sqlsmith connect()) ──
    if (!dry_run) {
        try {
            conn->test("SET statement_timeout TO '1s'", NULL);
        } catch (...) {
            // Non-PG DBMS may not support this; silently continue
            if (verbose)
                cerr << "[SMOKE] Warning: could not set statement_timeout" << endl;
        }
        try {
            conn->test("SET client_min_messages TO 'ERROR'", NULL);
        } catch (...) {
            // Silently ignore
        }
        try {
            conn->test("SET application_name TO 'dbfuzz::dut'", NULL);
        } catch (...) {
            // Non-PG DBMS may not support this; silently continue
        }
    }

    // ── 5. Build scope (matching sqlsmith main() scope init) ──
    struct scope root_scope;
    root_scope.schema = db_schema.get();
    root_scope.stmt_seq = make_shared<map<string, unsigned int>>();

    for (auto &t : db_schema->tables) {
        if (exclude_catalog &&
            (t.schema == "pg_catalog" || t.schema == "information_schema"))
            continue;
        root_scope.tables.push_back(&t);
    }

    // Fill refs: add each table once for column reference resolution
    for (auto *t : root_scope.tables) {
        root_scope.refs.push_back(t);
    }

    // ── 6. Main loop (strict sqlsmith double-loop structure) ──
    long long query_count = 0;

    cerr << "[SMOKE] Starting smoke test"
         << (dry_run ? " (dry-run)" : "")
         << (verbose ? " (verbose)" : "")
         << (max_queries > 0 ? " max-queries=" + to_string(max_queries) : " unlimited")
         << " ..." << endl;

    while (1) {  // OUTER: connection recovery loop
        try {
            while (1) {  // INNER: query generation loop

                // Check max-queries limit
                if (max_queries > 0 && query_count >= max_queries) {
                    // Final report
                    for (auto l : smoke_loggers) {
                        auto *cl = dynamic_cast<cerr_logger*>(l.get());
                        if (cl) cl->report();
                    }
                    impedance::report(cerr);

                    // Report known error suppression stats
                    if (known_errors_suppressed() > 0)
                        cerr << "[SMOKE] Suppressed " << known_errors_suppressed()
                             << " known errors" << endl;

                    // Save RNG state for reproduction
                    if (!rng_state_out.empty()) {
                        ofstream ofs(rng_state_out);
                        ofs << rng_state_serialize();
                        cerr << "[SMOKE] RNG state saved to " << rng_state_out << endl;
                    }

                    cerr << "[SMOKE] Completed " << query_count << " queries." << endl;
                    return;
                }

                // ── Generate AST ──
                shared_ptr<prod> gen;
                try {
                    root_scope.new_stmt();
                    gen = statement_factory(&root_scope);
                } catch (const runtime_error &e) {
                    // Generation failure, retry
                    continue;
                }

                // Notify loggers: generated
                for (auto l : smoke_loggers) {
                    try { l->generated(*gen); } catch (...) {}
                }

                if (dry_run) {
                    query_count++;
                    continue;
                }

                // ── Convert to SQL string ──
                ostringstream sql;
                sql << *gen;
                string sql_str = sql.str();

                // ── Execute (matching sqlsmith transaction wrapping) ──
                try {
                    // ROLLBACK → BEGIN → stmt → ROLLBACK
                    // First ROLLBACK clears any leftover transaction state
                    try { conn->test("ROLLBACK", NULL); } catch (...) {}
                    conn->test("BEGIN", NULL);
                    conn->test(sql_str, NULL);
                    conn->test("ROLLBACK", NULL);

                    // Success → notify loggers: executed
                    for (auto l : smoke_loggers) {
                        try { l->executed(*gen); } catch (...) {}
                    }

                } catch (const exception &e) {
                    string err = e.what();

                    if (is_connection_error(err)) {
                        // Connection broken → re-throw to outer loop
                        dut::broken be(err.c_str());
                        for (auto l : smoke_loggers) {
                            try { l->error(*gen, be); } catch (...) {}
                        }
                        throw be;

                    } else {
                        // Other SQL error → notify loggers
                        bool known = is_known_error(err);
                        dut::failure fe(err.c_str());
                        for (auto l : smoke_loggers) {
                            try {
                                // Known errors only go to impedance_feedback (for grammar adaptation)
                                // Other loggers skip known errors to reduce noise
                                if (known && dynamic_cast<impedance_feedback*>(l.get()) == nullptr)
                                    continue;
                                l->error(*gen, fe);
                            } catch (...) {}
                        }
                        // Try ROLLBACK to clean up
                        try { conn->test("ROLLBACK", NULL); } catch (...) {}
                    }
                }

                query_count++;
            }
        }
        catch (const dut::broken &e) {
            // ── Connection recovery (matching sqlsmith outer loop) ──
            if (verbose)
                cerr << "[SMOKE] Connection lost: " << e.what()
                     << " — reconnecting in 1s..." << endl;

            this_thread::sleep_for(chrono::milliseconds(1000));

            // Reconnect
            try {
                conn = dut_setup(d_info);
                // Re-set session variables
                try { conn->test("SET statement_timeout TO '1s'", NULL); } catch (...) {}
                try { conn->test("SET client_min_messages TO 'ERROR'", NULL); } catch (...) {}
                try { conn->test("SET application_name TO 'dbfuzz::dut'", NULL); } catch (...) {}
                if (verbose)
                    cerr << "[SMOKE] Reconnected successfully." << endl;
            } catch (const exception &re) {
                if (verbose)
                    cerr << "[SMOKE] Reconnect failed: " << re.what() << endl;
                // Continue outer loop to retry
            }
        }
    }
}
