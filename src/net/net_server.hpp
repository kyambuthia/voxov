#pragma once

class NetServer {
public:
    NetServer();
    ~NetServer();

    void start(int port);
    void stop();
    void pump();
};
