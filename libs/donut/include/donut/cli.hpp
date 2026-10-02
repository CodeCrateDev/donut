#pragma once

#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace donut::cli {

// One command line option, as shown by print_usage() and accepted by parse().
struct Option {
    std::string_view name;  // long name without dashes, e.g. "interface"
    char short_name{};      // short name, e.g. 'i'; 0 if there is none
    std::string_view arg;  // argument placeholder, e.g. "<iface>"; empty for flags
    std::string_view help; // description
    bool required{};       // parse() fails if the option is missing
};

// Result of a successful parse().
struct Arguments {
    std::unordered_map<std::string, std::string> values;  // keyed by long name
    std::vector<std::string> positional;

    bool has(std::string_view name) const;
    const std::string& value(std::string_view name) const;
};

// Parses argv[1..argc-1] against the option list. Accepts "--name value",
// "--name=value", "-n value" and "-nvalue". A bare "--" makes everything
// after it positional. On failure returns false and sets error.
// Options marked required are NOT enforced here; call check_required() after
// handling --help/--version so those always work.
bool parse(int argc, char* const argv[], std::span<const Option> options,
           Arguments& out, std::string& error);

// Returns false and sets error if any option marked required is missing.
bool check_required(std::span<const Option> options, const Arguments& args,
                     std::string& error);

// Prints "<program> <version> (donut <toolkit version>)".
void print_version(std::string_view program, std::string_view program_version,
                   std::ostream& os = std::cout);

// Prints a usage block: program/version header, summary, usage line and the
// option list, with the descriptions aligned in a column.
void print_usage(std::string_view program, std::string_view program_version,
                 std::string_view summary, std::string_view usage_line,
                 std::span<const Option> options, std::ostream& os = std::cout);

}  // namespace donut::cli
