#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace donut::net {

// True if s is a dotted-quad IPv4 address, e.g. "192.168.1.1".
bool is_ipv4(std::string_view s);

// Parses "<ipv4>[/<prefix>]". If no prefix is given it defaults to 24.
// Returns false if the address is invalid or the prefix is outside 0..32.
bool parse_cidr(std::string_view s, std::string& ip, int& prefix);

// Splits a comma-separated list into items. Returns false on empty items
// or a trailing comma.
bool split_csv(std::string_view s, std::vector<std::string>& out);

// True if name is a syntactically valid Linux interface name: shorter than
// IFNAMSIZ (16) and only letters, digits, '-', '_' and '.'.
bool valid_interface(std::string_view name);

// Finds the first IPv4 address and prefix length of a network interface.
// Returns false if the interface has no IPv4 address; prefix is set to -1
// when the system reports no netmask for it.
bool interface_ipv4(std::string_view iface, std::string& ip, int& prefix);

}  // namespace donut::net
