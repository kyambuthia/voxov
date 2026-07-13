#if defined(VOXOV_PLATFORM_WEB)

#include "engine_net/web_posix_socket_compat.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sys/socket.h>
#include <vector>

namespace {
thread_local std::vector<std::uint8_t> socket_scratch;
thread_local int receive_budget = 0;
constexpr int kReceiveBatchSize = 4;

bool message_size(const msghdr *message, std::size_t &total) {
    if (!message || message->msg_iovlen < 0 ||
        (message->msg_iovlen > 0 && !message->msg_iov)) {
        errno = EINVAL;
        return false;
    }
    const std::size_t part_count =
        static_cast<std::size_t>(message->msg_iovlen);
    total = 0;
    for (std::size_t i = 0; i < part_count; ++i) {
        const std::size_t length = message->msg_iov[i].iov_len;
        if (length > std::numeric_limits<std::uint32_t>::max() - total) {
            errno = EMSGSIZE;
            return false;
        }
        total += length;
    }
    return true;
}

int bridge_flags(int flags) {
#if defined(MSG_NOSIGNAL)
    return flags & ~MSG_NOSIGNAL;
#else
    return flags;
#endif
}

int bridge_receive_flags(int flags) {
    int result = bridge_flags(flags);
#if defined(MSG_DONTWAIT)
    // The socket bridge does not proxy fcntl(F_SETFL), so ENet's attempt to
    // mark its UDP socket non-blocking does not reach the native descriptor.
    // Apply the equivalent behavior per receive operation instead.
    result |= MSG_DONTWAIT;
#endif
    return result;
}
} // namespace

void web_posix_socket_begin_pump() {
    receive_budget = kReceiveBatchSize;
}

// Emscripten's full POSIX WebSocket bridge implements sendto/recvfrom but its
// sendmsg/recvmsg entry points intentionally abort. wasm-ld wraps ENet's two
// scatter/gather calls here so its UDP wire protocol can use the supported
// bridge operations without modifying the vendored ENet submodule.
extern "C" ssize_t __wrap_sendmsg(int socket, const msghdr *message, int flags) {
    std::size_t total = 0;
    if (!message_size(message, total)) {
        return -1;
    }

    socket_scratch.resize(total);
    std::size_t offset = 0;
    const std::size_t part_count =
        static_cast<std::size_t>(message->msg_iovlen);
    for (std::size_t i = 0; i < part_count; ++i) {
        const iovec &part = message->msg_iov[i];
        if (part.iov_len > 0 && !part.iov_base) {
            errno = EINVAL;
            return -1;
        }
        if (part.iov_len > 0) {
            std::memcpy(socket_scratch.data() + offset, part.iov_base,
                        part.iov_len);
        }
        offset += part.iov_len;
    }

    if (message->msg_name) {
        return sendto(socket, socket_scratch.data(), total, bridge_flags(flags),
                      static_cast<const sockaddr *>(message->msg_name),
                      message->msg_namelen);
    }
    return send(socket, socket_scratch.data(), total, bridge_flags(flags));
}

extern "C" ssize_t __wrap_recvmsg(int socket, msghdr *message, int flags) {
    if (receive_budget <= 0) {
        errno = EWOULDBLOCK;
        return -1;
    }

    std::size_t capacity = 0;
    if (!message_size(message, capacity)) {
        return -1;
    }

    socket_scratch.resize(capacity);
    socklen_t address_length = message->msg_namelen;
    const ssize_t received = message->msg_name
        ? recvfrom(socket, socket_scratch.data(), capacity,
                   bridge_receive_flags(flags),
                   static_cast<sockaddr *>(message->msg_name), &address_length)
        : recv(socket, socket_scratch.data(), capacity,
               bridge_receive_flags(flags));
    if (received < 0) {
        return received;
    }
    --receive_budget;

    message->msg_namelen = message->msg_name ? address_length : 0;
    message->msg_flags = 0;
    std::size_t remaining = std::min<std::size_t>(received, capacity);
    std::size_t offset = 0;
    const std::size_t part_count =
        static_cast<std::size_t>(message->msg_iovlen);
    for (std::size_t i = 0; i < part_count && remaining > 0; ++i) {
        iovec &part = message->msg_iov[i];
        if (part.iov_len > 0 && !part.iov_base) {
            errno = EINVAL;
            return -1;
        }
        const std::size_t count = std::min<std::size_t>(part.iov_len, remaining);
        std::memcpy(part.iov_base, socket_scratch.data() + offset, count);
        offset += count;
        remaining -= count;
    }
    if (static_cast<std::size_t>(received) > capacity) {
        message->msg_flags |= MSG_TRUNC;
    }
    return received;
}

#endif
