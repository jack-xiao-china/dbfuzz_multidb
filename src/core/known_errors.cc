#include "core/known_errors.hh"

#include <iostream>
#include <fstream>
#include <sstream>

#ifndef HAVE_BOOST_REGEX
using std::regex;
using std::smatch;
using std::regex_search;
#else
using boost::regex;
using boost::smatch;
using boost::regex_search;
#endif

using namespace std;

static vector<string> exact_patterns;
static vector<regex> regex_patterns;
static long suppressed_count = 0;

static void load_file(const string &path, vector<string> &lines)
{
    ifstream f(path);
    if (!f.is_open())
        return;
    string line;
    while (getline(f, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (!line.empty())
            lines.push_back(line);
    }
}

void known_errors_init(const string &dir)
{
    string prefix = dir.empty() ? "" : dir + "/";

    // Load exact substring patterns
    load_file(prefix + "known.txt", exact_patterns);
    if (!exact_patterns.empty())
        cerr << "[KNOWN] Loaded " << exact_patterns.size()
             << " exact patterns from " << prefix << "known.txt" << endl;

    // Load regex patterns
    vector<string> re_lines;
    load_file(prefix + "known_re.txt", re_lines);
    for (auto &line : re_lines) {
        try {
            regex_patterns.emplace_back(line);
        } catch (const exception &e) {
            cerr << "[KNOWN] Bad regex in known_re.txt: " << line
                 << " (" << e.what() << ")" << endl;
        }
    }
    if (!regex_patterns.empty())
        cerr << "[KNOWN] Loaded " << regex_patterns.size()
             << " regex patterns from " << prefix << "known_re.txt" << endl;
}

bool is_known_error(const string &msg)
{
    // Check exact substring match
    for (auto &pat : exact_patterns) {
        if (msg.find(pat) != string::npos) {
            suppressed_count++;
            return true;
        }
    }

    // Check regex match
    for (auto &re : regex_patterns) {
        if (regex_search(msg, re)) {
            suppressed_count++;
            return true;
        }
    }

    return false;
}

long known_errors_suppressed()
{
    return suppressed_count;
}
