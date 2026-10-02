# Donut (David's Open Networking Utility Toolkit)

A toolkit of small Linux utilities. Every program lives in its own directory
under `tools/` with its own `CMakeLists.txt`; the master `CMakeLists.txt`
discovers and builds all of them in one go. Target: C++20 on Ubuntu Server
24.04 / 26.04.

## Build

    cmake -B build
    cmake --build build

Requires CMake >= 3.16 and a C++20 compiler. Binaries land in `build/bin/`.

Add a new tool by creating `tools/<name>/CMakeLists.txt` — it is picked up
automatically. Shared code (CLI/usage/version helpers, exec, IP parsing)
lives in `libs/donut` as the `donut_common` library.

## Tools

### changeip

Change the IPv4 address of a network interface, switch it between static
addressing and DHCP, and optionally set its default gateway and DNS servers.
DNS defaults to Cloudflare `1.1.1.1` and Google `8.8.8.8`.

    sudo ./build/bin/donut-changeip -i enp0s3 -a 192.168.1.50/24 -g 192.168.1.1
    sudo ./build/bin/donut-changeip -i enp0s3 -a 192.168.1.50 -d 1.1.1.1,8.8.8.8
    sudo ./build/bin/donut-changeip -i enp0s3 --mode dhcp
    ./build/bin/donut-changeip --dry-run -i enp0s3 -a 192.168.1.50/24 --no-dns
    ./build/bin/donut-changeip --help
    ./build/bin/donut-changeip --version

Behavior:

- Must run as root (except `--help`, `--version`, `--dry-run`).
- `--mode dhcp` configures the interface for DHCP; a static address is
  configured with `--address` (which implies `--mode static`). `--address`
  and `--gateway` are rejected in DHCP mode.
- If netplan is installed, the configuration is written to
  `/etc/netplan/90-donut-changeip-<iface>.yaml` (one file per interface, so
  configuring a second interface does not touch the first) and applied with
  `netplan apply`. The `90-` prefix makes it sort after distro defaults (e.g.
  `50-cloud-init.yaml`), so the donut settings win for that interface; other
  netplan files are not touched. DNS servers go into the netplan file and
  `/etc/resolv.conf` is managed by netplan/systemd-resolved, not edited.
- Without netplan, the tool falls back to direct `ip` commands (`dhclient`
  for DHCP mode) and writes `/etc/resolv.conf` itself: it is backed up to
  `/etc/resolv.conf.donut.bak` first, and if it is a symlink
  (systemd-resolved stub) it is replaced with a regular file.
- An address without a prefix length defaults to `/24`.
- IPv6 addresses on the interface are left alone; only IPv4 is reconfigured.

### changemac

Change the MAC (hardware) address of a network interface.

    sudo ./build/bin/donut-changemac -i enp0s3 -m aa:bb:cc:dd:ee:ff
    sudo ./build/bin/donut-changemac -i enp0s3 --random
    ./build/bin/donut-changemac --dry-run -i enp0s3 -m aa:bb:cc:dd:ee:ff
    ./build/bin/donut-changemac --help
    ./build/bin/donut-changemac --version

Behavior:

- Must run as root (except `--help`, `--version`, `--dry-run`).
- The MAC address is case-insensitive and normalized to lowercase
  `xx:xx:xx:xx:xx:xx` form.
- `--random` generates a fresh random MAC instead: locally administered and
  unicast (first byte `x2`, `x6`, `xa` or `xe`), so it never collides with a
  real vendor OUI.
- The interface is taken down while the address is set and brought back up
  afterwards; its IP configuration is kept.

### netinfo

Show all network interfaces with their state, MAC and addresses, plus the
default route and the configured DNS servers, in one compact view. Read-only,
no root required.

    ./build/bin/donut-netinfo
    ./build/bin/donut-netinfo -i enp0s3
    ./build/bin/donut-netinfo --help
    ./build/bin/donut-netinfo --version

### scan

Find devices on the local network: sends an ARP request to every address of a
subnet and reports who answers, with IP and MAC address.

    sudo ./build/bin/donut-scan -i enp0s3
    sudo ./build/bin/donut-scan -i enp0s3 --subnet 192.168.1.0/26
    ./build/bin/donut-scan --help
    ./build/bin/donut-scan --version

Behavior:

- Must be run as root (raw sockets), except `--help`, `--version`.
- Scans the interface's own subnet by default; `--subnet` picks another one
  (a bare address defaults to `/24`). Subnets larger than 4096 addresses are
  rejected.
- `--wait` sets how long to listen for replies after the sweep (default
  2 seconds, range 1..60).
