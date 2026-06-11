/// @file
/// @brief Known error filter — suppress boring/expected errors from output
///
/// Loads patterns from known.txt (exact substring match) and
/// known_re.txt (regex match) to filter uninteresting errors.
/// Impedance feedback still receives all errors for grammar adaptation.

#ifndef KNOWN_ERRORS_HH
#define KNOWN_ERRORS_HH

#include <string>
#include <vector>

#ifndef HAVE_BOOST_REGEX
#include <regex>
#else
#include <boost/regex.hpp>
#endif

/// Load known error patterns from files in the given directory (or cwd if empty).
/// Files: known.txt (substring match), known_re.txt (regex match).
void known_errors_init(const std::string &dir = "");

/// Check if an error message matches any known pattern.
bool is_known_error(const std::string &msg);

/// Get count of suppressed errors.
long known_errors_suppressed();

#endif
