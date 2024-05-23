#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <iostream>
#include <stdexcept>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    auto iter = _arp_cache_table.find(next_hop_ip);
    if (iter != _arp_cache_table.end()) {  // Chech if the destination Ethernet Address is alrealy know
        ArpCacheEntry arp_cache_entry = iter->second;
        // Check if the destination Ethernet Address is not timeout
        if (arp_cache_entry.ms_arrived_time + _ms_cache_lifetime > _ms_current_time) {
            EthernetFrame frame;
            frame.header().src = _ethernet_address;
            frame.header().dst = arp_cache_entry.eaddress;
            frame.header().type = EthernetHeader::TYPE_IPv4;
            frame.payload() = dgram.serialize();
            _frames_out.push(frame);
            return;
        }
        // The corresponding Ethernet Address is timout, we should remove it and use ARP to get a fresh one.
        _arp_cache_table.erase(iter);
    }

    // Queue the datagragm so it can be sent after the ARP reply is received.
    _pending_datagrams[next_hop_ip].emplace_back(_ms_current_time, dgram);

    // Check if the netwrok has already sent an ARP request about the same IP address.
    if (_pending_datagrams[next_hop_ip].size() != 1) {
        if (_pending_datagrams[next_hop_ip].front().ms_arrived_time + _ms_arp_reply_timeout > _ms_current_time) {
            // If the previous ARP request is not timeout, we simply wait for a replay to the first one and do nothing.
            return;
        } else {
            // The ARP request is timout. But we don't need to worry about that in this lab.
            // throw std::runtime_error("ARP request dose not get reply in expected time!");
        }
    }

    // The following code broadcast an ARP request for the next hop's Ethernet Address.
    EthernetFrame arp_frame;
    ARPMessage arp_request;

    arp_request.opcode = ARPMessage::OPCODE_REQUEST;
    arp_request.sender_ethernet_address = _ethernet_address;
    arp_request.sender_ip_address = _ip_address.ipv4_numeric();
    arp_request.target_ip_address = next_hop_ip;

    arp_frame.header().src = _ethernet_address;
    arp_frame.header().dst = ETHERNET_BROADCAST;
    arp_frame.header().type = EthernetHeader::TYPE_ARP;
    arp_frame.payload() = arp_request.serialize();
    _frames_out.push(arp_frame);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().type == EthernetHeader::TYPE_IPv4 && frame.header().dst == _ethernet_address) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) != ParseResult::NoError) {
            cerr << "Encounter an error when parsing ethernet frame(IPV4)" << endl;
        }
        return dgram;

    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message;
        if (arp_message.parse(frame.payload()) != ParseResult::NoError) {
            cerr << "Encounter an error when parsing ethernet frame(ARP)" << endl;
        }

        // Learn mappings from both requests and replies.
        _arp_cache_table[arp_message.sender_ip_address] =
            ArpCacheEntry(_ms_current_time, arp_message.sender_ethernet_address);

        if (arp_message.opcode == ARPMessage::OPCODE_REQUEST &&
            arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
            // If we receive an ARP request and it is intended for us,
            // we need reply this request.
            EthernetFrame arp_frame;
            ARPMessage arp_reply;

            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp_message.sender_ethernet_address;
            arp_reply.target_ip_address = arp_message.sender_ip_address;

            arp_frame.header().src = _ethernet_address;
            arp_frame.header().dst = arp_message.sender_ethernet_address;
            arp_frame.header().type = EthernetHeader::TYPE_ARP;
            arp_frame.payload() = arp_reply.serialize();
            _frames_out.push(arp_frame);
        } else if (arp_message.opcode == ARPMessage::OPCODE_REPLY) {
            // Our previous ARP request get reply.
            EthernetFrame pending_frame;
            auto iter = _pending_datagrams.find(arp_message.sender_ip_address);
            // Check if there are some datagrams need to be sent
            if (iter == _pending_datagrams.end()) {
                cerr << "ARP received but there is no datagram need to be sent" << endl;
                return {};
            }
            // Send all pending datagram
            list<PendingDatagram> &pending_datagram = iter->second;
            for (const PendingDatagram &dgram_entry : pending_datagram) {
                if (dgram_entry.ms_arrived_time + _ms_arp_reply_timeout < _ms_current_time) {
                    // The ARP reply to this datagram is timeout.
                    continue;
                }
                pending_frame.header().src = _ethernet_address;
                pending_frame.header().dst = arp_message.sender_ethernet_address;
                pending_frame.header().type = EthernetHeader::TYPE_IPv4;
                pending_frame.payload() = dgram_entry.dgram.serialize();
                _frames_out.push(pending_frame);
            }
            pending_datagram.clear();
        }
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { _ms_current_time += ms_since_last_tick; }
