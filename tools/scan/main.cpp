#include <donut/cli.hpp>
#include <donut/net.hpp>
#include <donut/version.h>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kProgram = "donut-scan";
constexpr std::string_view kToolVersion = SCAN_VERSION;
constexpr std::string_view kSummary =
    "Find devices on the local network: sends an ARP request to every address of a subnet and reports who answers, with their MAC address.";
constexpr std::string_view kUsageLine =
    "donut-scan -i <iface> [--subnet <cidr>] [options]";

// Safety cap so a typo does not flood a /8 with millions of requests.
constexpr int kMaxAddresses = 4096;
constexpr int kDefaultWait = 2;

const donut::cli::Option kOptions[] = {
    {"interface", 'i', "<iface>", "network interface to scan from", true},
    {"subnet", 's', "<cidr>",
     "subnet to scan (default: the interface's own; a bare address defaults to /24)",
     false},
    {"wait", 'w', "<seconds>", "how long to wait for replies (default: 2)", false},
    {"help", 'h', "", "show this help and exit", false},
    {"version", 'v', "", "show version information and exit", false},
};

uint32_t parse_ipv4_word(const std::string& dotted) {
    in_addr addr{};
    if (inet_pton(AF_INET, dotted.c_str(), &addr) != 1) return 0;
    return ntohl(addr.s_addr);
}

std::string ipv4_string(uint32_t host_order) {
    return std::to_string((host_order >> 24) & 0xff) + '.' +
           std::to_string((host_order >> 16) & 0xff) + '.' +
           std::to_string((host_order >> 8) & 0xff) + '.' +
           std::to_string(host_order & 0xff);
}

// One ARP request for an IPv4 target: ethernet header + ARP payload.
std::array<unsigned char, 42> build_request(const unsigned char sha[6],
                                            const unsigned char spa[4],
                                            uint32_t target) {
    std::array<unsigned char, 42> p{};
    for (int i = 0; i < 6; ++i) {
        p[static_cast<std::size_t>(i)] = 0xff;                     // broadcast
        p[static_cast<std::size_t>(i + 6)] = sha[i];              // sender MAC
    }
    p[12] = 0x08;
    p[13] = 0x06;  // ethertype: ARP
    p[14] = 0x00;
    p[15] = 0x01;  // hardware type: ethernet
    p[16] = 0x08;
    p[17] = 0x00;  // protocol type: IPv4
    p[18] = 6;
    p[19] = 4;  // address sizes
    p[20] = 0x00;
    p[21] = 0x01;  // operation: request
    std::memcpy(p.data() + 22, sha, 6);
    std::memcpy(p.data() + 28, spa, 4);
    // target MAC stays zeroed; the target fills it in on reply
    p[38] = (target >> 24) & 0xff;
    p[39] = (target >> 16) & 0xff;
    p[40] = (target >> 8) & 0xff;
    p[41] = target & 0xff;
    return p;
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
    const std::string subnet_arg = args.value("subnet");

    int wait_seconds = kDefaultWait;
    if (args.has("wait")) {
        const std::string wait_arg = args.value("wait");
        try {
            std::size_t used = 0;
            wait_seconds = std::stoi(wait_arg, &used);
            if (used != wait_arg.size()) throw std::invalid_argument("");
        } catch (const std::exception&) {
            std::cerr << kProgram << ": invalid --wait '" << wait_arg
                      << "', expected 1..60\n";
            return 2;
        }
        if (wait_seconds < 1 || wait_seconds > 60) {
            std::cerr << kProgram << ": invalid --wait '" << wait_arg
                      << "', expected 1..60\n";
            return 2;
        }
    }

    if (!donut::net::valid_interface(iface)) {
        std::cerr << kProgram << ": invalid interface name '" << iface << "'\n";
        return 2;
    }

    // Raw sockets need CAP_NET_RAW; there is nothing to dry-run here.
    if (geteuid() != 0) {
        std::cerr << kProgram << ": must be run as root\n";
        return 1;
    }

    std::string my_ip;
    int my_prefix = -1;
    if (!donut::net::interface_ipv4(iface, my_ip, my_prefix) || my_prefix < 0) {
        std::cerr << kProgram << ": no usable IPv4 address on " << iface << '\n';
        return 1;
    }

    std::string subnet_ip;
    int prefix = my_prefix;
    if (subnet_arg.empty()) {
        subnet_ip = my_ip;
    } else if (!donut::net::parse_cidr(subnet_arg, subnet_ip, prefix) || prefix < 0) {
        std::cerr << kProgram << ": invalid subnet '" << subnet_arg
                  << "', expected <ipv4>[/<prefix>]\n";
        return 2;
    }
    if (prefix < 20) {
        std::cerr << kProgram << ": subnet /" << prefix << " is larger than "
                  << kMaxAddresses << " addresses, refusing to scan; use a narrower"
                  << " --subnet\n";
        return 2;
    }

    const uint32_t mask = prefix == 0 ? 0u : (~0u << (32 - prefix));
    const uint32_t network = parse_ipv4_word(subnet_ip) & mask;
    const uint32_t my_addr = parse_ipv4_word(my_ip);
    const uint64_t count = 1ULL << (32 - prefix);

    // Look up the interface index and our MAC address.
    int inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (inet_sock < 0) {
        std::cerr << kProgram << ": socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (ioctl(inet_sock, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << kProgram << ": unknown interface '" << iface << "'\n";
        close(inet_sock);
        return 1;
    }
    const int ifindex = ifr.ifr_ifindex;
    if (ioctl(inet_sock, SIOCGIFHWADDR, &ifr) < 0) {
        std::cerr << kProgram << ": cannot get MAC of " << iface << ": "
                  << std::strerror(errno) << '\n';
        close(inet_sock);
        return 1;
    }
    unsigned char my_mac[6];
    std::memcpy(my_mac, ifr.ifr_hwaddr.sa_data, 6);
    close(inet_sock);

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock < 0) {
        std::cerr << kProgram << ": cannot open raw socket: "
                  << std::strerror(errno) << '\n';
        return 1;
    }
    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ARP);
    sll.sll_ifindex = ifindex;
    if (bind(sock, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0) {
        std::cerr << kProgram << ": cannot bind to " << iface << ": "
                  << std::strerror(errno) << '\n';
        close(sock);
        return 1;
    }

    std::cout << kProgram << ": scanning " << ipv4_string(network) << '/' << prefix
              << " on " << iface << " (source " << my_ip << ", waiting "
              << wait_seconds << "s)\n";

    // Send a request to every host address of the subnet, except our own.
    unsigned char spa[4];
    in_addr source{};
    inet_pton(AF_INET, my_ip.c_str(), &source);
    std::memcpy(spa, &source.s_addr, 4);
    for (uint64_t offset = 1; offset + 1 < count; ++offset) {
        const uint32_t target = network + offset;
        if (target == my_addr) continue;
        const auto request = build_request(my_mac, spa, target);
        sendto(sock, request.data(), request.size(), 0,
               reinterpret_cast<const sockaddr*>(&sll), sizeof(sll));
    }

    // Collect replies until the deadline.
    timeval tv{0, 250 * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(wait_seconds);
    std::map<uint32_t, std::array<unsigned char, 6>> hosts;
    while (std::chrono::steady_clock::now() < deadline) {
        unsigned char buf[2048];
        const ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 42) continue;  // timeout or short frame
        if (buf[12] != 0x08 || buf[13] != 0x06) continue;  // not ARP
        if (buf[20] != 0x00 || buf[21] != 0x02) continue;  // not a reply
        const uint32_t ip = (static_cast<uint32_t>(buf[28]) << 24) |
                            (static_cast<uint32_t>(buf[29]) << 16) |
                            (static_cast<uint32_t>(buf[30]) << 8) |
                            static_cast<uint32_t>(buf[31]);
        if ((ip & mask) != network) continue;  // outside the scanned subnet
        if (ip == my_addr) continue;
        if (std::memcmp(buf + 22, my_mac, 6) == 0) continue;  // our own echo
        std::array<unsigned char, 6> mac{};
        std::memcpy(mac.data(), buf + 22, 6);
        hosts.emplace(ip, mac);
    }
    close(sock);

    if (hosts.empty()) {
        std::cout << "(no responses)\n";
    } else {
        for (const auto& [ip, mac] : hosts) {
            std::cout << ipv4_string(ip) << "  ";
            for (std::size_t i = 0; i < mac.size(); ++i) {
                if (i != 0) std::cout << ':';
                std::cout << std::hex << std::setfill('0') << std::setw(2)
                          << static_cast<int>(mac[i]) << std::dec << std::setfill(' ');
            }
            std::cout << '\n';
        }
    }
    std::cout << kProgram << ": " << hosts.size() << " host(s) responded\n";
    return 0;
}
