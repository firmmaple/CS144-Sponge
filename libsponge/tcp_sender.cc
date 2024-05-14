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

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _base_seqno; }

void TCPSender::fill_window() {
    size_t nread;
    uint64_t bytes_sent;  // count is in "sequence space" i.e. SYN and FIN each count for one byte
    string data;
    size_t end_seqno = _base_seqno + _window_size;

    if (_window_size == 0) {
        end_seqno += 1;
        // end_seqno = _next_seqno + 1;
        // 思考这样一个问题，我们能不能使用end_seqno=_next_seqno+1，似乎这样也能实现目的？
        // 不行，在回答这个问题前，首先我们需要知道为什么窗口大小为0视作窗口大小为1
        // 假设我方（A）现在收到了对方（B）的ack包，B的窗口大小为0，并且A先前发送的所有数据都被ack了
        // 那这时候A就被卡住了，因为所有报文都被B接受了，B不会继续发送ACK包，我们永远不会接受B的最新窗口大小了
        // 所以A必须主动发送一个1字节的报文，尽管又可能会被B拒接（B的窗口大小没有更新，还是0），
        // 但是B会继续向我们发送ack包，告知最新的窗口大小。另外，就算这个1字节的报文被拒绝，我们稍后也会超时重传，所以没有关系。
        //
        // end_seqno+=1对应的逻辑为，在B的ACK包告诉A“我收到了你的新数据”，随后A更新_base_seqno前，像这样1字节的报文只能发送一次
        // end_seqno=_next_seqno+1对应的逻辑则是，只要A接受到一个窗口为0的ACK报文，我就发送一个1字节的报文
        // 想象这样一种情况，在完成三次握手后，
        // A发送报文4 [ACK] Seq=1, Ack=1, Win=1, Len=1 （报文4的数据将会把B的窗口填满）
        // B收到了报文4，并发送报文5 [ACK] Seq=1, Ack=2, Win=0, Len=0，用于确认报文4，不含payload
        // 随后B发送报文6，[ACK] Seq=1, Ack=2, Win=0, Len=1，报文6包含实际的payload
        // A接受到报文4后，由于win=0，会送一个报文7用于探测，[ACK] Seq=2, Ack=1, Win=1, Len=1
        // 随后A有接收到报文6，由于报文6的win也等于0，所以会发送一个报文8继续探测，[ACK] Seq=3, Ack=2, Win=0, Len=1
        // 但是报文8完全是没有必要发送的，并且如果B返回的segment还是0，A还是会继续发送
        //
        // 使用end_seqno=_next_seqno+1会导致test120和132失败，不清楚具体原因，但似是上面的框架会发送一个rst包
        // 具体原因需要进一步调试分析，最好是把上层代码也看一下。
        //
        // 另外一点就是，使用end_seqno=_next_seqno+1会没发通过test17的测试，因为调用end_input_stream的时候应为窗口大小还是0，会
        // 多发送一个包
    }

    while (_next_seqno < end_seqno) {
        TCPSegment seg;
        size_t remaining_window_size = end_seqno - _next_seqno;
        data = _stream.read(min(remaining_window_size, TCPConfig::MAX_PAYLOAD_SIZE));
        nread = data.size();

        if (_next_seqno == 0) {  // No byte has been sent yet.
            seg.header().syn = true;
        } else if (_stream.eof() && _next_seqno < _stream.bytes_written() + 2) {
            // Stream has reached EOF, but FIN flag hasn't been sent yet
            if (nread < remaining_window_size)
                // Don't send FIN if this would make the segment exceed the receiver's window
                // But I think such implementation is wrong as window size means the real bytes receiver can accept.
                // Though flags like SYN or FlAG consume one sequence space, they are not real bytes and
                // do not consume stream index.
                // Hence I think it is safe to send FLAG when receiver's window is full.
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

    while (!_outstanding_segments.empty()) {  // Check whether there is any outstanding data acknowledged
        const TCPSegment seg = _outstanding_segments.front();
        abs_seqno = unwrap(seg.header().seqno, _isn, abs_ackno);
        bytes_length = seg.length_in_sequence_space();
        if (abs_seqno + bytes_length > abs_ackno) {
            // Since the ouststanding data in the queue is sorted by seqno,
            // there is no need to check following outstanding data.
            break;
        }
        _outstanding_segments.pop();
        _base_seqno += bytes_length;
        has_new_data_received = true;
    }

    if (has_new_data_received) {
        _current_retransmission_timeout = _initial_retransmission_timeout;
        _timer.start(_current_retransmission_timeout);
        _n_consecutive_retransimissions = 0;
    }

    if (_outstanding_segments.empty()) {
        _timer.stop();
    }

    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer.is_running()) {
        return;
    }
    _timer.elapsed(ms_since_last_tick);
    if (_timer.is_timeout()) {
        // cerr << "[sender] Time out start" << endl;
        TCPSegment retransmitted_seg = _outstanding_segments.front();
        _segments_out.push(retransmitted_seg);

        if (_window_size) {
            _n_consecutive_retransimissions++;
            // cerr << "[sender] retx: " << _n_consecutive_retransimissions << endl;
            _current_retransmission_timeout *= 2;
        }

        _timer.start(_current_retransmission_timeout);
        // cerr << "[sender] Time out end" << endl;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _n_consecutive_retransimissions; }

void TCPSender::send_empty_segment() {
    TCPSegment empy_segment;
    empy_segment.header().seqno = next_seqno();
    empy_segment.payload() = Buffer("");
    _segments_out.push(empy_segment);
}
