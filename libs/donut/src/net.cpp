#include <donut/net.hpp>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <bit>

namespace donut::net {
namespace {

bool to_int(std::string_view digits, int& value) {
    if (digits.empty()) return false;
    value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (value > 255) return false;
    }
    return true;
}

}  // namespace

bool is_ipv4(std::string_view s) {
    if (s.empty() || s.size() > 15) return false;

    int octets = 0;
    std::size_t pos = 0;
    while (true) {
        const std::size_t dot = s.find('.', pos);
        const std::string_view octet =
            dot == std::string_view::npos ? s.substr(pos) : s.substr(pos, dot - pos);
        int value = 0;
        if (octet.empty() || octet.size() > 3 || !to_int(octet, value)) return false;
        ++octets;
        if (dot == std::string_view::npos) break;
        pos = dot + 1;
        if (pos == s.size()) return false;
    }
    return octets == 4;
}

bool parse_cidr(std::string_view s, std::string& ip, int& prefix) {
    const std::size_t slash = s.find('/');
    const std::string_view addr = slash == std::string_view::npos ? s : s.substr(0, slash);
    if (!is_ipv4(addr)) return false;
    if (slash == std::string_view::npos) {
        ip.assign(addr);
        prefix = 24;
        return true;
    }
    if (s.find('/', slash + 1) != std::string_view::npos) return false;

    const std::string_view digits = s.substr(slash + 1);
    if (digits.empty() || digits.size() > 2) return false;
    int value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value > 32) return false;
    ip.assign(addr);
    prefix = value;
    return true;
}

bool split_csv(std::string_view s, std::vector<std::string>& out) {
    std::size_t pos = 0;
    while (pos <= s.size()) {
        const std::size_t comma = s.find(',', pos);
        const std::string_view item =
            comma == std::string_view::npos ? s.substr(pos) : s.substr(pos, comma - pos);
        if (item.empty()) return false;
        out.emplace_back(item);
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return !out.empty();
}

bool valid_interface(std::string_view name) {
    if (name.empty() || name.size() >= 16) return false;  // Linux IFNAMSIZ - 1
    for (const char c : name) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool interface_ipv4(std::string_view iface, std::string& ip, int& prefix) {
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return false;

    bool found = false;
    for (const ifaddrs* ifa = ifs; ifa != nullptr && !found; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (iface != ifa->ifa_name) continue;

        char buf[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) == nullptr) continue;

        ip.assign(buf);
        prefix = -1;
        if (ifa->ifa_netmask != nullptr) {
            const auto* mask = reinterpret_cast<const sockaddr_in*>(ifa->ifa_netmask);
            const unsigned int bits = ntohl(mask->sin_addr.s_addr);
            prefix = std::popcount(bits);
        }
        found = true;
    }
    freeifaddrs(ifs);
    return found;
}

}  // namespace donut::net
