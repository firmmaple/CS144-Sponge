#include "stream_reassembler.hh"

#include <iostream>
#include <string>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), _buf(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t max_idx = _base_index + _capacity - _output.buffer_size();  // Index of the first unacceptable bytes
    // Calculate the actual start and end index since the start and end index of data may exceed _base_index and max_idx
    size_t seg_start_idx = std::max(index, _base_index);
    size_t seg_end_idx = std::min(index + data.size(), max_idx);
    size_t pos, i;

    if (eof) {
        _eof_index = index + data.size();
    }

    // Store data segment in _buf
    for (i = seg_start_idx; i < seg_end_idx; i++) {
        pos = i % _capacity;
        if (_buf[pos].is_filled == false) {
            _buf[pos].is_filled = true;
            _buf[pos].byte_value = data[i - index];
            ++_unassembled_bytes_cnt;
        }
    }

    // Update _last_index to the last unassembled byte
    if (_last_index < seg_end_idx)
        _last_index = seg_end_idx;

    // Write reassembled bytes to _output once the bytes with correct indexes have arrived
    if (_base_index == seg_start_idx) {
        string assembled_bytes;
        for (i = _base_index; i < _last_index; i++) {
            pos = i % _capacity;
            if (_buf[pos].is_filled == false)
                break;
            assembled_bytes.push_back(_buf[pos].byte_value);
            --_unassembled_bytes_cnt;
            _buf[pos].is_filled = false;
        };
        _base_index = i;  // Update _base_index to the first unassembled byte
        _output.write(assembled_bytes);
        if (_base_index == _eof_index) {  // All bytes have been reassembled
            _output.end_input();
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes_cnt; }

bool StreamReassembler::empty() const { return _base_index == _last_index; }

// int main() {
//     StreamReassembler reassembler(8);
//     string str;
//
//     str = "abc";
//     reassembler.push_substring(str, 0, 0);
//     cout << reassembler.stream_out().read(3) << endl;
//
//     str = "ghX";
//     reassembler.push_substring(str, 6, 1);
//     // cout << reassembler.stream_out().read(8) << endl;
//
//     str = "cdefg";
//     reassembler.push_substring(str, 2, 0);
//     cout << reassembler.stream_out().read(8) << endl;
//     cout<< reassembler.stream_out().input_ended();
// }
