#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _timer.time_elapsed(); }

void TCPConnection::segment_received(const TCPSegment &seg) {
    const TCPHeader header = seg.header();

    if (header.rst) {
        cout << "[segment_received]: RST received" << endl;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _is_rst_received_or_sent = true;
        return;
    }

    if (header.fin) {
        bool outbound_fin_sent =
            _sender.stream_in().eof() && _sender.next_seqno_absolute() == (_sender.stream_in().bytes_written() + 2);
        if (!outbound_fin_sent) {
            _linger_after_streams_finish = false;
        }
    }

    if (!_receiver.stream_out().eof()) {
        _receiver.segment_received(seg);
    }

    _timer.restart();
    if (header.ack) {
        _sender.ack_received(header.ackno, header.win);
    } else if (header.syn) {  // 作为接受方接受到SYN，会在之后到_send_all_segments中被统一发送
        _sender.fill_window();
    }

    if (seg.length_in_sequence_space()) {
        if (_sender.segments_out().empty())
            _sender.send_empty_segment();
        _send_all_segments();
    } else if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 &&
               (header.seqno.raw_value() < _receiver.ackno().value().raw_value() ||
                _receiver.ackno().value().raw_value() + static_cast<uint32_t>(_receiver.window_size()) <
                    header.seqno.raw_value())) {
        _sender.send_empty_segment();
        _send_all_segments();
    }
}

bool TCPConnection::active() const {
    if (_is_rst_received_or_sent)
        return false;

    bool inbound_ended = _receiver.stream_out().eof();
    bool outbound_fin_sent =
        _sender.stream_in().eof() && _sender.next_seqno_absolute() == (_sender.stream_in().bytes_written() + 2);
    bool outbound_fin_acked = bytes_in_flight() == 0;

    if (inbound_ended && outbound_fin_sent && outbound_fin_acked) {
        if (_linger_after_streams_finish) {
            return time_since_last_segment_received() < 10 * _cfg.rt_timeout;
        } else {
            return false;
        }
    }

    return true;
}

size_t TCPConnection::write(const string &data) {
    size_t nwritten;

    nwritten = _sender.stream_in().write(data);
    _sender.fill_window();

    _send_all_segments();

    return nwritten;
}

void TCPConnection::_send_all_segments() {
    TCPSegment seg;

    while (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
        seg.header().win = _receiver.window_size();
        _sender.segments_out().pop();
        _segments_out.push(seg);
        // cerr << "[_send_all_segments]: send segment seqno: " << seg.header().seqno << endl;
    }
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _send_all_segments();
    _timer.elapsed(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        TCPSegment rst_segment;
        WrappingInt32 seqno = _sender.next_seqno();

        if (!_segments_out.empty()) {
            seqno = _segments_out.front().header().seqno;
            _segments_out = std::queue<TCPSegment>();
        }
        cerr << "[tick]: send RST" << endl;
        rst_segment.header().seqno = seqno;
        rst_segment.header().rst = true;
        rst_segment.payload() = Buffer("");
        _segments_out.push(rst_segment);
        _is_rst_received_or_sent = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    _send_all_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _segments_out.push(_sender.segments_out().front());
    _sender.segments_out().pop();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            TCPSegment rst_segment;
            rst_segment.header().seqno = _sender.next_seqno();
            rst_segment.header().rst = true;
            rst_segment.payload() = Buffer("");
            _segments_out.push(rst_segment);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
