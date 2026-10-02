#include <donut/cli.hpp>
#include <donut/exec.hpp>
#include <donut/net.hpp>
#include <donut/version.h>

#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kProgram = "donut-changemac";
constexpr std::string_view kToolVersion = CHANGEMAC_VERSION;
constexpr std::string_view kSummary =
    "Change the MAC (hardware) address of a network interface.";
constexpr std::string_view kUsageLine =
    "donut-changemac -i <iface> (-m <mac> | --random) [options]";

const donut::cli::Option kOptions[] = {
    {"interface", 'i', "<iface>", "network interface to configure", true},
    {"mac", 'm', "<mac>",
     "MAC address, e.g. aa:bb:cc:dd:ee:ff (case-insensitive)", false},
    {"random", 'r', "",
     "generate and set a random locally-administered MAC instead of --mac",
     false},
    {"dry-run", 'n', "", "show what would be done without applying anything", false},
    {"help", 'h', "", "show this help and exit", false},
    {"version", 'v', "", "show version information and exit", false},
};

char lower_hex(char c) {
    if (c >= 'A' && c <= 'F') return static_cast<char>(c - 'A' + 'a');
    return c;
}

bool hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Accepts "xx:xx:xx:xx:xx:xx" in any case; writes the normalized lowercase
// form to out. Returns false on any other shape.
bool parse_mac(std::string_view s, std::string& out) {
    if (s.size() != 17) return false;
    out.clear();
    for (int i = 0; i < 6; ++i) {
        const char hi = s[static_cast<size_t>(i) * 3];
        const char lo = s[static_cast<size_t>(i) * 3 + 1];
        if (!hex_digit(hi) || !hex_digit(lo)) return false;
        if (i < 5 && s[static_cast<size_t>(i) * 3 + 2] != ':') return false;
        out += lower_hex(hi);
        out += lower_hex(lo);
        if (i < 5) out += ':';
    }
    return true;
}

// A random MAC that is valid to use: locally administered (bit 1 of the
// first byte set, so it does not collide with a real vendor OUI) and unicast
// (bit 0 clear), with all other bits random.
std::string random_mac() {
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> byte(0, 255);
    unsigned char mac[6];
    for (unsigned char& b : mac) {
        b = static_cast<unsigned char>(byte(rng));
    }
    mac[0] = static_cast<unsigned char>((mac[0] & 0xfc) | 0x02);

    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
                  mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

bool run_command(const std::vector<std::string>& cmd, bool dry_run) {
    if (dry_run) {
        std::cout << "would run:";
        for (const auto& arg : cmd) std::cout << ' ' << arg;
        std::cout << '\n';
        return true;
    }
    const int status = donut::exec::run(cmd);
    if (status != 0) {
        std::cerr << kProgram << ": command failed (status " << status << "):";
        for (const auto& arg : cmd) std::cerr << ' ' << arg;
        std::cerr << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    donut::cli::Arguments args;
    std::string error;
    if (!donut::cli::parse(argc, argv, kOptions, args, error)) {
        std::cerr << kProgram << ": " << error << "\ntry '" << kProgram
                  << " --help' for more information\n";
        return 2;
    }
    if (args.has("help")) {
        donut::cli::print_usage(kProgram, kToolVersion, kSummary, kUsageLine, kOptions);
        return 0;
    }
    if (args.has("version")) {
        donut::cli::print_version(kProgram, kToolVersion);
        return 0;
    }
    if (!donut::cli::check_required(kOptions, args, error)) {
        std::cerr << kProgram << ": " << error << "\ntry '" << kProgram
                  << " --help' for more information\n";
        return 2;
    }

    const std::string iface = args.value("interface");
    const std::string mac_arg = args.value("mac");
    const bool dry_run = args.has("dry-run");

    if (!donut::net::valid_interface(iface)) {
        std::cerr << kProgram << ": invalid interface name '" << iface << "'\n";
        return 2;
    }

    std::string mac;
    if (args.has("random")) {
        if (args.has("mac")) {
            std::cerr << kProgram << ": --mac cannot be combined with --random\n";
            return 2;
        }
        mac = random_mac();
    } else {
        if (!args.has("mac")) {
            std::cerr << kProgram << ": either --mac or --random is required\n";
            return 2;
        }
        if (!parse_mac(mac_arg, mac)) {
            std::cerr << kProgram << ": invalid MAC address '" << mac_arg
                      << "', expected xx:xx:xx:xx:xx:xx\n";
            return 2;
        }
    }

    if (!dry_run && geteuid() != 0) {
        std::cerr << kProgram << ": must be run as root\n";
        return 1;
    }

    std::cout << kProgram << ": setting MAC of " << iface << " to " << mac << '\n';
    // The link must be down while the address is changed; it is brought back
    // up afterwards, keeping its IP configuration.
    if (!run_command({"ip", "link", "set", "dev", iface, "down"}, dry_run)) return 1;
    if (!run_command({"ip", "link", "set", "dev", iface, "address", mac}, dry_run)) {
        return 1;
    }
    if (!run_command({"ip", "link", "set", "dev", iface, "up"}, dry_run)) return 1;

    std::cout << kProgram << ": done\n";
    return 0;
}
