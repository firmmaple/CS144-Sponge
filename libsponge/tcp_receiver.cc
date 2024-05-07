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

    if (_isn.has_value()) {  // Check if SYN has alreadly been received.
        abs_seqno = unwrap(header.seqno, _isn.value(), _checkpoint);
    } else if (header.syn) {
        // This segment is the initial one with SYN flag set, indicting the start of a TCP connection.
        // The sequence number of the first-arriving segment that has the SYN flag set is the initial sequence number.
        _isn = header.seqno;
        abs_seqno = 1;  // The absolute sequence number is 1 for the first byte of payload.
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
