#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "wrapping_integers.hh"

#include <algorithm>
#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _current_retransmission_timeout{_initial_retransmission_timeout}
    , _stream(capacity)
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    size_t nread;
    uint64_t bytes_sent;  // count is in "sequence space" i.e. SYN and FIN each count for one byte
    string data;

    while (_remaining_window_size > 0) {
        TCPSegment seg;
        data = _stream.read(min(_remaining_window_size, TCPConfig::MAX_PAYLOAD_SIZE));
        nread = data.size();

        if (_next_seqno == 0) {
            seg.header().syn = true;
            // nread += 1;
        } else if (_stream.eof() && _next_seqno < _stream.bytes_written() + 2) {
            // Don't send FIN if this would make the segment exceed the receiver's window
            // But I think such implementation is wrong as window size means the real bytes receiver can accept.
            // Though flags like SYN or FlAG consume one sequence space, they are not real bytes and
            // do not consume stream index.
            // Hence I think it is safe to send FLAG when receiver's window is full.
            if (nread < _remaining_window_size)
                seg.header().fin = true;
        }

        seg.header().seqno = wrap(_next_seqno, _isn);
        seg.payload() = Buffer(std::move(data));
        bytes_sent = seg.length_in_sequence_space();
        if (bytes_sent == 0) {
            break;  // There is no data to send.
                    // Either becasue there is no bytes to read at this time or the data have all been send;
        }

        if (!_timer.is_running()) {
            _timer.start(_current_retransmission_timeout);
        }

        _segments_out.push(seg);
        _outstanding_segments.push(seg);

        _next_seqno += bytes_sent;
        _bytes_in_flight += bytes_sent;
        _remaining_window_size -= bytes_sent;  // Count Flags like SYN consuming one space in window size
                                               // But I do not think they need to consume.
                                               // The correct implementation should be _remaining_window_size -= nread;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    uint64_t abs_seqno;
    uint64_t bytes_length;
    bool has_new_data_received = false;

    if (0 == abs_ackno || abs_ackno > _next_seqno) {
        // Since the absolute seqno of SYN is 0, the absolute ackno must bigger than 0.
        // Also, the absolute ackno cannot bigger than _next_seqno.
        return;
    }

    while (!_outstanding_segments.empty()) {
        const TCPSegment seg = _outstanding_segments.front();
        abs_seqno = unwrap(seg.header().seqno, _isn, abs_ackno);
        bytes_length = seg.length_in_sequence_space();
        if (abs_seqno + bytes_length > abs_ackno) {
            break;
        }
        _outstanding_segments.pop();
        _bytes_in_flight -= bytes_length;
        has_new_data_received = true;
        cout << "remove!" << endl;
    }

    if (has_new_data_received) {
        _current_retransmission_timeout = _initial_retransmission_timeout;
        _timer.start(_current_retransmission_timeout);
        _n_consecutive_retransimissions = 0;
    }

    if (_outstanding_segments.empty()) {
        _timer.stop();
        cout << "Stop!" << endl;
    }

    if (abs_ackno + window_size > _next_seqno) {
        _latest_window_size = window_size;
        _remaining_window_size = abs_ackno + window_size - _next_seqno;
        fill_window();
    } else if (window_size == 0) {
        // When filling window, treat a '0' window size as equal to '1' but don't back off RTO
        _latest_window_size = 0;     // Since _latest_window_size=0, ROT will not be back off when timer goes off.
        _remaining_window_size = 1;  // Send a single bytes in fill_window method
        fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer.is_running()) {
        return;
    }
    _timer.elapsed(ms_since_last_tick);
    if (_timer.is_timeout()) {
        TCPSegment retransmitted_seg = _outstanding_segments.front();
        _segments_out.push(retransmitted_seg);

        if (_latest_window_size) {
            _n_consecutive_retransimissions++;
            _current_retransmission_timeout *= 2;
        }

        _timer.start(_current_retransmission_timeout);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _n_consecutive_retransimissions; }

void TCPSender::send_empty_segment() {
    TCPSegment empy_segment;
    empy_segment.header().seqno = next_seqno();
    empy_segment.payload() = Buffer("");
    _segments_out.push(empy_segment);
}
