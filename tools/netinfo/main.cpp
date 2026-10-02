#include <donut/cli.hpp>
#include <donut/net.hpp>
#include <donut/version.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kProgram = "donut-netinfo";
constexpr std::string_view kToolVersion = NETINFO_VERSION;
constexpr std::string_view kSummary =
    "Show network interfaces, addresses, the default route and DNS servers at a glance.";
constexpr std::string_view kUsageLine = "donut-netinfo [-i <iface>]";
constexpr std::string_view kResolvConf = "/etc/resolv.conf";
constexpr std::string_view kRouteFile = "/proc/net/route";

const donut::cli::Option kOptions[] = {
    {"interface", 'i', "<iface>", "show only this interface", false},
    {"help", 'h', "", "show this help and exit", false},
    {"version", 'v', "", "show version information and exit", false},
};

struct NicInfo {
    bool up = false;
    bool has_mac = false;
    unsigned char mac[6] = {};
    std::vector<std::string> ipv4;  // "192.168.1.50/24"
    std::vector<std::string> ipv6;  // "fe80::1/64"
};

std::string mac_string(const unsigned char mac[6]) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
                  mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

bool collect_interfaces(std::map<std::string, NicInfo>& nics) {
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return false;

    for (const ifaddrs* ifa = ifs; ifa != nullptr; ifa = ifa->ifa_next) {
        NicInfo& info = nics[ifa->ifa_name];
        if (ifa->ifa_flags & IFF_UP) info.up = true;
        if (ifa->ifa_addr == nullptr) continue;

        const int family = ifa->ifa_addr->sa_family;
        if (family == AF_PACKET) {
            const auto* sll = reinterpret_cast<const sockaddr_ll*>(ifa->ifa_addr);
            if (sll->sll_halen == 6) {
                std::memcpy(info.mac, sll->sll_addr, 6);
                info.has_mac = true;
            }
        } else if (family == AF_INET) {
            const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
            char buf[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) == nullptr) {
                continue;
            }
            std::string entry = buf;
            if (ifa->ifa_netmask != nullptr) {
                const auto* mask = reinterpret_cast<const sockaddr_in*>(ifa->ifa_netmask);
                const unsigned int bits = ntohl(mask->sin_addr.s_addr);
                entry += '/' + std::to_string(std::popcount(bits));
            }
            info.ipv4.push_back(entry);
        } else if (family == AF_INET6) {
            const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(ifa->ifa_addr);
            char buf[INET6_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf)) == nullptr) {
                continue;
            }
            std::string entry = buf;
            if (ifa->ifa_netmask != nullptr) {
                const auto* mask =
                    reinterpret_cast<const sockaddr_in6*>(ifa->ifa_netmask);
                int bits = 0;
                for (const unsigned char byte : mask->sin6_addr.s6_addr) {
                    bits += std::popcount(static_cast<unsigned int>(byte));
                }
                entry += '/' + std::to_string(bits);
            }
            info.ipv6.push_back(entry);
        }
    }
    freeifaddrs(ifs);
    return true;
}

// /proc/net/route lists gateways in little-endian hex; a destination of
// 00000000 marks a default route.
void print_default_routes() {
    std::ifstream route{std::string(kRouteFile)};
    std::string line;
    std::getline(route, line);  // header

    bool any = false;
    while (std::getline(route, line)) {
        std::istringstream fields(line);
        std::string iface, dest, gw;
        if (!(fields >> iface >> dest >> gw)) continue;
        if (dest != "00000000") continue;
        const unsigned long value = std::strtoul(gw.c_str(), nullptr, 16);
        any = true;
        std::cout << "default route: via "
                  << ((value >> 0) & 0xff) << '.' << ((value >> 8) & 0xff) << '.'
                  << ((value >> 16) & 0xff) << '.' << ((value >> 24) & 0xff)
                  << " dev " << iface << '\n';
    }
    if (!any) std::cout << "default route: none\n";
}

void print_dns() {
    std::vector<std::string> servers;
    std::ifstream in{std::string(kResolvConf)};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string keyword, server;
        if (fields >> keyword >> server && keyword == "nameserver") {
            servers.push_back(server);
        }
    }

    std::string note;
    std::error_code ec;
    const auto target = std::filesystem::read_symlink(kResolvConf, ec);
    if (!ec && !target.empty()) {
        note = " (" + std::string(kResolvConf) + " -> " + target.string() + ")";
    }

    std::cout << "dns: ";
    if (servers.empty()) {
        std::cout << "(none)";
    } else {
        for (std::size_t i = 0; i < servers.size(); ++i) {
            if (i != 0) std::cout << ", ";
            std::cout << servers[i];
        }
    }
    std::cout << note << '\n';
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

    const std::string filter = args.value("interface");
    if (!filter.empty() && !donut::net::valid_interface(filter)) {
        std::cerr << kProgram << ": invalid interface name '" << filter << "'\n";
        return 2;
    }

    std::map<std::string, NicInfo> nics;
    if (!collect_interfaces(nics)) {
        std::cerr << kProgram << ": cannot enumerate interfaces: "
                  << std::strerror(errno) << '\n';
        return 1;
    }
    if (!filter.empty() && nics.find(filter) == nics.end()) {
        std::cerr << kProgram << ": interface '" << filter << "' not found\n";
        return 1;
    }

    bool any = false;
    for (const auto& [name, info] : nics) {
        if (!filter.empty() && name != filter) continue;
        any = true;
        std::cout << name << '\n';
        std::cout << "  state: " << (info.up ? "up" : "down") << '\n';
        std::cout << "  mac:   "
                  << (info.has_mac ? mac_string(info.mac) : std::string("(none)"))
                  << '\n';
        for (const auto& address : info.ipv4) {
            std::cout << "  ipv4:  " << address << '\n';
        }
        for (const auto& address : info.ipv6) {
            std::cout << "  ipv6:  " << address << '\n';
        }
    }
    if (!any) std::cout << "(no interfaces)\n";

    print_default_routes();
    print_dns();
    return 0;
}
