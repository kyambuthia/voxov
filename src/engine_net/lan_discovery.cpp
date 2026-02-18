#include "engine_net/lan_discovery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr uint16_t kDiscoveryPort = 47777;
constexpr uint64_t kBroadcastIntervalMs = 1000;
constexpr uint64_t kQueryIntervalMs = 1200;
constexpr uint64_t kHostExpiryMs = 5000;

#pragma pack(push, 1)
struct DiscoveryPacket {
    char magic[6];
    uint8_t type = 0;      // 1=beacon, 2=query
    uint16_t game_port_be = 0;
    uint8_t name_len = 0;
    char name[48];
};
#pragma pack(pop)

constexpr std::array<char, 6> kMagic = {'V', 'O', 'X', 'O', 'V', '2'};

#ifdef _WIN32
bool winsock_started = false;
#endif
}

bool LanDiscovery::start_host(uint16_t port, const std::string &name) {
    stop();
    game_port = port;
    host_name = name.empty() ? std::string("VOXOV Host") : name;
    mode_host = true;
    mode_client = false;
    if (!open_socket()) {
        return false;
    }
    running = true;
    last_broadcast_ms = 0;
    send_beacon_broadcast();
    return true;
}

bool LanDiscovery::start_client() {
    stop();
    mode_host = false;
    mode_client = true;
    if (!open_socket()) {
        return false;
    }
    running = true;
    last_query_ms = 0;
    send_query_broadcast();
    return true;
}

void LanDiscovery::stop() {
    running = false;
    mode_host = false;
    mode_client = false;
    game_port = 0;
    host_name.clear();
    discovered_hosts.clear();
    pending_hosts.clear();
    close_socket();
}

void LanDiscovery::pump() {
    if (!running) {
        return;
    }

    handle_receive();

    const uint64_t now = now_ms();
    if (mode_host && now - last_broadcast_ms >= kBroadcastIntervalMs) {
        send_beacon_broadcast();
    }
    if (mode_client && now - last_query_ms >= kQueryIntervalMs) {
        send_query_broadcast();
    }

    discovered_hosts.erase(
        std::remove_if(discovered_hosts.begin(), discovered_hosts.end(), [now](const LanHostEntry &h) {
            return now - h.last_seen_ms > kHostExpiryMs;
        }),
        discovered_hosts.end());
}

bool LanDiscovery::pop_host(LanHostEntry &out_host) {
    if (pending_hosts.empty()) {
        return false;
    }
    out_host = pending_hosts.front();
    pending_hosts.erase(pending_hosts.begin());
    return true;
}

const std::vector<LanHostEntry> &LanDiscovery::hosts() const {
    return discovered_hosts;
}

bool LanDiscovery::open_socket() {
#ifdef _WIN32
    if (!winsock_started) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        winsock_started = true;
    }
#endif

    sock = ::socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        return false;
    }

    int yes = 1;
    setsockopt(static_cast<int>(sock), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
    setsockopt(static_cast<int>(sock), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof(yes));

#ifdef _WIN32
    u_long nonblock = 1;
    ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &nonblock);
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(kDiscoveryPort);

    if (::bind(static_cast<int>(sock), reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr)) < 0) {
        close_socket();
        return false;
    }

    return true;
}

void LanDiscovery::close_socket() {
#ifdef _WIN32
    if (sock != static_cast<uintptr_t>(-1) && sock != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(sock));
    }
    sock = static_cast<uintptr_t>(-1);
#else
    if (sock >= 0) {
        close(sock);
    }
    sock = -1;
#endif
}

void LanDiscovery::send_query_broadcast() {
    if (sock < 0) {
        return;
    }

    DiscoveryPacket p{};
    std::memcpy(p.magic, kMagic.data(), kMagic.size());
    p.type = 2;

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    to.sin_port = htons(kDiscoveryPort);

    sendto(static_cast<int>(sock), reinterpret_cast<const char *>(&p), sizeof(p), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
    last_query_ms = now_ms();
}

void LanDiscovery::send_beacon_broadcast() {
    if (sock < 0 || !mode_host) {
        return;
    }

    DiscoveryPacket p{};
    std::memcpy(p.magic, kMagic.data(), kMagic.size());
    p.type = 1;
    p.game_port_be = htons(game_port);
    p.name_len = static_cast<uint8_t>(std::min<size_t>(host_name.size(), sizeof(p.name) - 1));
    std::memcpy(p.name, host_name.data(), p.name_len);

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    to.sin_port = htons(kDiscoveryPort);

    sendto(static_cast<int>(sock), reinterpret_cast<const char *>(&p), sizeof(p), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
    last_broadcast_ms = now_ms();
}

void LanDiscovery::send_beacon_to(uint32_t host_be, uint16_t port_be) {
    if (sock < 0 || !mode_host) {
        return;
    }

    DiscoveryPacket p{};
    std::memcpy(p.magic, kMagic.data(), kMagic.size());
    p.type = 1;
    p.game_port_be = htons(game_port);
    p.name_len = static_cast<uint8_t>(std::min<size_t>(host_name.size(), sizeof(p.name) - 1));
    std::memcpy(p.name, host_name.data(), p.name_len);

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = host_be;
    to.sin_port = port_be;

    sendto(static_cast<int>(sock), reinterpret_cast<const char *>(&p), sizeof(p), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
}

void LanDiscovery::handle_receive() {
    if (sock < 0) {
        return;
    }

    while (true) {
        DiscoveryPacket p{};
        sockaddr_in from{};
#ifdef _WIN32
        int from_len = sizeof(from);
#else
        socklen_t from_len = sizeof(from);
#endif
        const int n = recvfrom(
            static_cast<int>(sock),
            reinterpret_cast<char *>(&p),
            sizeof(p),
            0,
            reinterpret_cast<sockaddr *>(&from),
            &from_len);
        if (n <= 0) {
            break;
        }
        if (n < static_cast<int>(sizeof(p.magic) + 1)) {
            continue;
        }
        if (std::memcmp(p.magic, kMagic.data(), kMagic.size()) != 0) {
            continue;
        }

        if (p.type == 2) {
            if (mode_host) {
                send_beacon_to(from.sin_addr.s_addr, from.sin_port);
            }
            continue;
        }
        if (p.type != 1 || !mode_client) {
            continue;
        }

        const size_t name_len = std::min<size_t>(p.name_len, sizeof(p.name));
        std::string name(p.name, p.name + name_len);
        if (name.empty()) {
            name = "VOXOV Host";
        }

        char ip_buffer[64]{};
        if (!inet_ntop(AF_INET, &from.sin_addr, ip_buffer, sizeof(ip_buffer))) {
            continue;
        }

        upsert_host(std::string(ip_buffer), name, ntohs(p.game_port_be));
    }
}

void LanDiscovery::upsert_host(const std::string &ip, const std::string &name, uint16_t port) {
    const uint64_t now = now_ms();
    for (LanHostEntry &e : discovered_hosts) {
        if (e.ip == ip && e.port == port) {
            e.name = name;
            e.last_seen_ms = now;
            return;
        }
    }

    LanHostEntry e{};
    e.ip = ip;
    e.name = name;
    e.port = port;
    e.last_seen_ms = now;
    discovered_hosts.push_back(e);
    pending_hosts.push_back(e);
}

uint64_t LanDiscovery::now_ms() const {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
