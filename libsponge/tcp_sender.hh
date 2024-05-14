#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <queue>

class Timer {
  private:
    size_t _ms_elapsed_time = 0;
    size_t _ms_timeout_threshold;
    bool _is_running = false;

  public:
    Timer(const size_t ms_timeout_threshold) : _ms_timeout_threshold(ms_timeout_threshold) {}

    void start(const size_t ms_timeout_threshold) {
        _ms_timeout_threshold = ms_timeout_threshold;
        _ms_elapsed_time = 0;
        _is_running = true;
    }

    void stop() { _is_running = false; }

    void restart() {
        _ms_elapsed_time = 0;
        _is_running = true;
    }

    void elapsed(const size_t ms_since_last_tick) { _ms_elapsed_time += ms_since_last_tick; }

    size_t time_elapsed() const { return _ms_elapsed_time; }

    bool is_running() { return _is_running; }

    bool is_timeout() { return _ms_elapsed_time >= _ms_timeout_threshold; }
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    std::queue<TCPSegment> _outstanding_segments{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    unsigned int _current_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    /*
     * [0, _base_seqno)                             - Already acknowledged bytes
     * [_base_seqno, _next_seqno)                   - Sent, not yet acknowledged bytes
     * [_next_seqno, _base_seqno + _window_size]    - Bytes to be sent.
     * [_base_seqno + _window_size, )               - Bytes illegal to be sent.
     */

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // the absolute sequence number for the first byte no yet acknowledged
    size_t _base_seqno{0};

    // Assume the receiver's initial window is 1 as we need to send SYN flags firstly.
    size_t _window_size{1};
    // 那么_window_size可不可以是0呢，反正在fill_window方法里，如果窗口大小是0 ，也会把其当作1处理？
    // 不可以，因为对于需要将窗口当作1来处理的情况，我们并不会使用指数回退并增加重传计数器
    // 但是对于一开始的SYN报文，我们是需要指数回退的，而是否需要指数回退的逻辑我们是通过判断窗口大小是否为0实现的

    Timer _timer;

    unsigned int _n_consecutive_retransimissions{0};

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
