#pragma once

class NetClient {
public:
    NetClient();
    ~NetClient();

    void connect(const char *address, int port);
    void disconnect();
    void pump();
};
