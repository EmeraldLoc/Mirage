#include "socket.hpp"

bool sockaddr_in_equal(const sockaddr_in &a, const sockaddr_in &b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}