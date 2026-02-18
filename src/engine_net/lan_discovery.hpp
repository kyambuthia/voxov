#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LanHostEntry {
    std::string name;
    std::string ip;
    uint16_t port = 0;
    uint64_t last_seen_ms = 0;
};

class LanDiscovery {
public:
    bool start_host(uint16_t game_port, const std::string &host_name);
    bool start_client();
    void stop();
    void pump();
    bool pop_host(LanHostEntry &out_host);
    const std::vector<LanHostEntry> &hosts() const;

private:
    bool open_socket();
    void close_socket();
    void send_query_broadcast();
    void send_beacon_broadcast();
    void send_beacon_to(uint32_t host_be, uint16_t port_be);
    void handle_receive();
    void upsert_host(const std::string &ip, const std::string &name, uint16_t port);
    uint64_t now_ms() const;

    bool mode_host = false;
    bool mode_client = false;
    bool running = false;
    uint16_t game_port = 0;
    std::string host_name;
    uint64_t last_broadcast_ms = 0;
    uint64_t last_query_ms = 0;
    std::vector<LanHostEntry> discovered_hosts;
    std::vector<LanHostEntry> pending_hosts;
#ifdef _WIN32
    uintptr_t sock = static_cast<uintptr_t>(-1);
#else
    int sock = -1;
#endif
};
