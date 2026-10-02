#include <donut/cli.hpp>

#include <donut/version.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace donut::cli {
namespace {

const Option* find_long(std::span<const Option> options, std::string_view name) {
    for (const auto& option : options) {
        if (option.name == name) return &option;
    }
    return nullptr;
}

const Option* find_short(std::span<const Option> options, char name) {
    for (const auto& option : options) {
        if (option.short_name != 0 && option.short_name == name) return &option;
    }
    return nullptr;
}

// Left-hand column of one option line, e.g. "  -i, --interface <iface>".
std::string option_column(const Option& option) {
    std::string left = "  ";
    if (option.short_name != 0) {
        left += '-';
        left += option.short_name;
        left += ", ";
    } else {
        left += "    ";
    }
    left += "--";
    left += option.name;
    if (!option.arg.empty()) {
        left += ' ';
        left += option.arg;
    }
    return left;
}

}  // namespace

bool Arguments::has(std::string_view name) const {
    return values.find(std::string(name)) != values.end();
}

const std::string& Arguments::value(std::string_view name) const {
    static const std::string empty;
    const auto it = values.find(std::string(name));
    return it == values.end() ? empty : it->second;
}

bool parse(int argc, char* const argv[], std::span<const Option> options,
           Arguments& out, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view token = argv[i];

        if (token == "--") {
            for (int j = i + 1; j < argc; ++j) out.positional.emplace_back(argv[j]);
            break;
        }

        if (token.starts_with("--")) {
            std::string_view body = token.substr(2);
            std::string_view inline_value;
            bool has_inline = false;
            if (const auto eq = body.find('='); eq != std::string_view::npos) {
                inline_value = body.substr(eq + 1);
                body = body.substr(0, eq);
                has_inline = true;
            }
            const Option* option = find_long(options, body);
            if (option == nullptr) {
                error = "unknown option '--" + std::string(body) + "'";
                return false;
            }
            if (!option->arg.empty()) {
                std::string_view value;
                if (has_inline) {
                    value = inline_value;
                } else if (i + 1 < argc) {
                    value = argv[++i];
                } else {
                    error = "option '--" + std::string(option->name) + "' requires an argument";
                    return false;
                }
                out.values[std::string(option->name)] = value;
            } else {
                if (has_inline) {
                    error = "option '--" + std::string(option->name) + "' does not take an argument";
                    return false;
                }
                out.values[std::string(option->name)] = {};
            }
        } else if (token.size() >= 2 && token[0] == '-') {
            const Option* option = find_short(options, token[1]);
            if (option == nullptr) {
                error = std::string("unknown option '-") + token[1] + "'";
                return false;
            }
            if (!option->arg.empty()) {
                std::string_view value;
                if (token.size() > 2) {
                    value = token.substr(2);
                } else if (i + 1 < argc) {
                    value = argv[++i];
                } else {
                    error = "option '-" + std::string(1, option->short_name) + "' requires an argument";
                    return false;
                }
                out.values[std::string(option->name)] = value;
            } else {
                out.values[std::string(option->name)] = {};
            }
        } else {
            out.positional.emplace_back(token);
        }
    }

    return true;
}

bool check_required(std::span<const Option> options, const Arguments& args,
                    std::string& error) {
    for (const auto& option : options) {
        if (option.required && !args.has(option.name)) {
            error = "missing required option '--" + std::string(option.name) + "'";
            return false;
        }
    }
    return true;
}

void print_version(std::string_view program, std::string_view program_version,
                   std::ostream& os) {
    os << program << ' ' << program_version << " (donut " << DONUT_VERSION << ")\n";
}

void print_usage(std::string_view program, std::string_view program_version,
                 std::string_view summary, std::string_view usage_line,
                 std::span<const Option> options, std::ostream& os) {
    os << program << ' ' << program_version << " (donut " << DONUT_VERSION << ")\n\n";
    if (!summary.empty()) {
        os << "  " << summary << "\n\n";
    }
    os << "Usage:\n  " << usage_line << "\n\nOptions:\n";

    std::size_t width = 0;
    for (const auto& option : options) {
        width = std::max(width, option_column(option).size());
    }
    for (const auto& option : options) {
        const std::string left = option_column(option);
        os << left << std::string(width - left.size() + 2, ' ') << option.help;
        if (option.required) os << " (required)";
        os << '\n';
    }
}

}  // namespace donut::cli
