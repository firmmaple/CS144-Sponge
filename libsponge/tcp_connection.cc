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
    // cerr << "\n[conn] segment_received" << endl;
    // cerr << " ********** " + seg.header().to_string() << endl;
    const TCPHeader header = seg.header();

    if (header.rst) {  // check if RST is received
        // cerr << "[segment_received]: RST received" << endl;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _is_rst_received_or_sent = true;
        return;
    }

    if (header.fin) {
        // 在接受到fin报文后，我们通过需要判断到底是谁（我们还是对面）先关闭连接的
        // 1.如果是我们先关闭连接，那么就要保持_linger_after_streams_finish为true，即需要在TCP4步挥手后等待一段时间
        // 毕竟TCP4步挥手的最后一个ACK报文是我们发送的，我们没法确定对面是不是真的收到了这个ACK报文，
        // 所以我们就等待足够的时间，如果对面没有重传FIN报文，就说明对面已经成功接受到最后的ACk报文，这时候我们就可以安全关闭啦。
        // 2.如果是对面先关闭连接，那么我们需要将_linger_after_streams_finish设置为false，
        // 因为如果我们发送FIN报文，并成功收到对方的ACK，就说明TCP4步挥手已经成功了，这时我们直接关闭就好，不需要等待
        //
        // 那问题来了，我们怎么判断是谁最先关闭连接呢？
        // 判断条件是，如果我收到了fin报文（header.fin为true），并且我还没有发送fin报文（outbount_fin_sent为false），
        // 那就说明是对方先关闭连接的。
        bool outbound_fin_sent =
            _sender.stream_in().eof() && _sender.next_seqno_absolute() == (_sender.stream_in().bytes_written() + 2);
        if (!outbound_fin_sent) {  // 如果是对方先关闭连接，那么我们就不需要等待啦
            _linger_after_streams_finish = false;
        }
    }

    if (!_receiver.stream_out().eof()) {
        // 如果peer发送fin报文后，我们的ack报文对方没有收到，那么peer会再次发送fin报文
        // 这时候我们没有必要再recerive了
        // 但感觉也没有必要用if判断一下，我们的segment_received应该足够robust，能够处理这种情况
        // 不过我还没有测试过就是了
        _receiver.segment_received(seg);
    }

    _timer.restart();
    if (header.ack) {  // 除了最初的第一次TCP握手报文，其他报文都会有ack都为true
        _sender.ack_received(header.ackno, header.win);
    } else if (header.syn) {  // 作为接受方接受到SYN（也就是说，我们充当tcp连接的server）
        bool syn_sent_but_not_yet_received =
            _sender.next_seqno_absolute() != 0 && _sender.next_seqno_absolute() == _sender.bytes_in_flight();
        if (_is_server && syn_sent_but_not_yet_received) {
            // 处理TCP第二次握手丢失的情况
            // 在我们（作为server）已经收到client发来的第一次握手的报文后，client又重发了第一次握手报文。
            // 具体情况为，client向server发出第一次握手，server成功接收，并向client发出第二次握手的报文，
            // 可是随后的情况没有那么顺利，client没有收到第二次握手报文，于是重传第一次握手的TCP报文，也就是我们现在收到的这个segment。
            // 所以我们不需要做任何事情，直接返回就好（反正第二次握手对应的报文会在tick被调用时超时重传的，不用我们主动发送）
            return;
        }
        // 再插一嘴，这里的_is_server并不是多此一举，在测试用例中有一种情况，
        // 我们作为client已经向peer发送了第一次握手报文，但是还未确认，这时peer也一client的身份向我们发送了第一次握手报文
        // 这时候我们还是得返回一个报文应答peer，如果没有_is_server判断，我们会直接丢弃peer发送过来的第一次握手报文
        // 没法通过测试用例（说实话我觉得这个测试用例很不符合逻辑，直接丢弃不就完事了）

        _sender.fill_window();  // 生成TCP第二次握手的ACK报文，会在之后的_send_all_segments中被发送
    }

    if (seg.length_in_sequence_space()) {  // 只要接受的报文占据了任何序列号（报文包含payload，或者报文设置了SYN标志等），
                                           // 我们都应该确保至少有一个报文被发送过于，用于ACK
        if (_sender.segments_out().empty())  // 如果当前没有现成的待发送报文，我们就发送一个ACK空包
            _sender.send_empty_segment();
        _send_all_segments();
    } else if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 &&
               seg.header().seqno == (_receiver.ackno().value() - 1)) {  // Response to keep-alive segment
        _sender.send_empty_segment();
        _send_all_segments();
    }
}

bool TCPConnection::active() const {
    if (_is_rst_received_or_sent)
        return false;

    // Prereq #1 The inbound stream has been fully assembled and has ended.
    bool inbound_ended = _receiver.stream_out().eof();
    // Prereq #2 The outbound stream has been ended by the local application and fully sent (including the fact that it
    // ended, i.e. a segment with fin ) to the remote peer.
    bool outbound_fin_sent =
        _sender.stream_in().eof() && _sender.next_seqno_absolute() == (_sender.stream_in().bytes_written() + 2);
    // Prereq #3 The outbound stream has been fully acknowledged by the remote peer.
    bool outbound_fin_acked = outbound_fin_sent && bytes_in_flight() == 0;

    if (inbound_ended && outbound_fin_sent && outbound_fin_acked) {  // Check if Prerequisites #1 through #3 are true
        if (_linger_after_streams_finish) {
            // 由于是我们主动关闭连接，因此需要判断我们是否等待了足够多时间，即是否满足 Prereq #4的Option A
            return time_since_last_segment_received() < 10 * _cfg.rt_timeout;
        } else {  // 如果是对方主动关闭连接，那么只要Prereq #1-#3都满足，我们就能成功关闭
            return false;
        }
    }

    return true;
}

size_t TCPConnection::write(const string &data) {
    // cerr << "[conn] write" << endl;
    // cerr << "[conn] data(" << data.size() << ") = " << data << endl;
    size_t nwritten;

    nwritten = _sender.stream_in().write(data);
    _sender.fill_window();

    _send_all_segments();

    return nwritten;
}

void TCPConnection::_send_rst_segment() {
    TCPSegment rst_segment;

    _sender.send_empty_segment();
    rst_segment = _sender.segments_out().front();
    _sender.segments_out().pop();

    if (!_segments_out.empty()) {
        // 因为发送rst报文的优先级是最高的，所以需要丢弃其他未发送的segments，
        // 然后直接发送rst报文，另外需要重新设置rst报文的seqno
        rst_segment.header().seqno = _segments_out.front().header().seqno;
        _segments_out = std::queue<TCPSegment>();
    } else {
    }

    rst_segment.header().rst = true;
    _segments_out.push(rst_segment);
}

void TCPConnection::_send_all_segments() {
    // cerr << "[conn] send_all_segments, lenth: " << _sender.segments_out().size() << endl;
    TCPSegment seg;

    // 发送或重传TCP握手的第一个报文
    if (!_sender.segments_out().empty() && !_receiver.ackno().has_value()) {
        // cerr << "[conn] send_all_segments resend SYN start" << endl;
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        _segments_out.push(std::move(seg));
        // cerr << "[conn] send_all_segments resend SYN end" << endl;
        return;
    }

    while (!_sender.segments_out().empty()) {  // Send all segments(new segments or outstanding segments)
        // cerr << "[conn] send_all_segments in while loop start" << endl;
        seg = _sender.segments_out().front();
        seg.header().ack = true;  // 除TCP握手的第一个报文和RST外，所有其他报文的ACk都为true
        seg.header().ackno = _receiver.ackno().value();
        seg.header().win = _receiver.window_size();
        _sender.segments_out().pop();
        _segments_out.push(seg);
        // cerr << "[conn] send_all_segments in while loop end" << endl;
    }
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // cerr << "[conn] tick " << ms_since_last_tick << endl;
    _sender.tick(ms_since_last_tick);
    _send_all_segments();
    _timer.elapsed(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        // cerr << "[conn] send rst!" << endl;
        _send_rst_segment();
        _is_rst_received_or_sent = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    }
}

void TCPConnection::end_input_stream() {
    // cerr << "[conn] end_input_stream" << endl;
    _sender.stream_in().end_input();
    _sender.fill_window();
    _send_all_segments();
}

void TCPConnection::connect() {
    // cerr << "[conn] connect" << endl;
    _is_server = false;
    _sender.fill_window();
    _send_all_segments();
}

TCPConnection::~TCPConnection() {
    try {
        // cerr << "[conn-] ~TCPConnection" << endl;
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _send_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
