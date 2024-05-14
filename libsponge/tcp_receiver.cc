#include "tcp_receiver.hh"

#include "tcp_header.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();
    Buffer payload = seg.payload();
    uint64_t abs_seqno;

    if (header.syn) {
        if (_isn.has_value()) {  // 判断这是否为被重传的，并且被已经被我们接收的TCP握手报文
            // 具体来说，在我们（作为server）已经收到client发来的第一次握手的报文后，client又重发了第一次握手报文。
            // 具体情况为，client向server发出第一次握手，server成功接收，并向client发出第二次握手的报文，
            // 可是随后的情况没有那么顺利，client没有收到第二次握手报文，于是重传第一次握手的TCP报文，也就是我们现在收到的这个segment。
            // 所以我们不需要做任何事情，直接返回就好（反正第二次握手对应的报文会在tick被调用时超时重传的，不用我们主动发送）
            return;
        }
        // This segment is the initial one with SYN flag set, indicting the start of a TCP connection.
        // The sequence number of the first-arriving segment that has the SYN flag set is the initial sequence number.
        _isn = header.seqno;  // 其实我们记录完_isn后可以直接return的，因为一般情况下syn报文不会含有数据，
                              // 但是继续执行也没有关系，我们的代码可以处理这种逻辑，
                              // 因为payload的size=0， 后面执行的push_substring不会插入实质性的数据
        abs_seqno = 1;              // The absolute sequence number is 1 for the first byte of payload.
    } else if (_isn.has_value()) {  // Check if SYN has alreadly been received.
        abs_seqno = unwrap(header.seqno, _isn.value(), _checkpoint);
    } else {
        // If no SYN has been received yet, ignore the segment and return immediately.
        // Continue waiting for a segment with SYN to establish the connection.
        return;
    }

    // Push the segment's payload into the reassembler
    _reassembler.push_substring(payload.copy(), abs_seqno - 1, header.fin);
    _checkpoint = abs_seqno;  // Update the checkpoint to the current absolute sequence number.
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_isn.has_value()) {
        uint64_t stream_index = _reassembler.stream_out().bytes_written();
        uint64_t abs_seqno = stream_index + 1;
        if (_reassembler.stream_out().input_ended()) {
            // Increment the absolute sequence number if the FIN flag is set,
            // as the FIN flag consumes one sequence number.
            abs_seqno += 1;
        }
        return WrappingInt32(wrap(abs_seqno, _isn.value()));
    }

    return {};  // Return an empty optional if SYN has not been received.
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
