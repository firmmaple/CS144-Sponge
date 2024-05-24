#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // The code is not strong yet, need to check whether this is a duplicate entry.
    _route_table.emplace_back(route_prefix, prefix_length, next_hop, interface_num);
}

std::string uint32_to_ip(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    ssize_t index = -1;
    int8_t longest_prefix_length = -1;

    if (_route_table.empty())
        return;

    // The router decrements the datagram’s TTL (time to live).
    // If the TTL was zero already, or hits zero after the decrement, the router should drop the datagram.
    if (dgram.header().ttl <= 1)
        return;
    dgram.header().ttl--;

    const uint32_t dest_ip = dgram.header().dst;
    // Search the routing table to find the route that not only match the datagram's destination address,
    // but also has the biggest value of prefix length.
    for (size_t i = 0; i < _route_table.size(); i++) {
        const RouteEntry route_entry = _route_table[i];
        uint8_t shift_cnt = 32 - route_entry.prefix_length;
        // 这里存在一个特殊情况，当我们匹配默认路由0.0.0.0/0时，由于dest_ip>>32等价与dest_ip>>0，
        // 右移32并不会将dest_ip变为0，因此我们需要判断这种特殊情况
        if ((dest_ip >> shift_cnt) == (route_entry.route_prefix >> shift_cnt) or route_entry.prefix_length == 0) {
            if (longest_prefix_length < route_entry.prefix_length) {  // Record the current longest prefix length
                longest_prefix_length = route_entry.prefix_length;
                index = i;
            }
        }
    }

    // If no routes matched, the router drops the datagram.
    if (longest_prefix_length == -1) {
        return;
    }

    RouteEntry route_entry = _route_table[index];
    // The next hop address is the datagram's destination address, if next_hop is empty.
    Address next_hop(Address::from_ipv4_numeric(dgram.header().dst));
    if (route_entry.next_hop.has_value()) {
        next_hop = route_entry.next_hop.value();
    }

    interface(route_entry.interface_num).send_datagram(dgram, next_hop);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
