#include "byte_stream.hh"

#include <algorithm>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _buf(capacity), _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t nwritten = 0;         // Number of bytes successfully written to _buf during iteration
    size_t nleft = data.size();  // Number of bytes left to write
    size_t n_to_write;           // Number of bytes to write in one iteration

    while (nleft > 0 && _size < _capacity) {  // Continue writing while there is data left and _buf is not full.
        // Calculate the number of bytes that can be written in this iteration.
        // Since _buf is a Circular Queue, the amount to write, n_to_write, depends on the position of _head and _tail.
        if (_head <= _tail) {
            // Since we have checked the size in the while statement, _head == _tail means the buffer is empty .
            // If _size equals _capacity, it would indicate the buffer is full, but that condition is handled by
            // the while loop guard.
            // If _head <= _tail, write until data runs out or until reaching the end of _buf.
            // If _tail reach the _buf's end, _tail will be wrap to 0 and we will continue writing the remaining data in
            // the next iteration.
            n_to_write = std::min(nleft, _capacity - _tail);
        } else {
            // Otherwise, when _tail < _head, write until data runs out or
            // the _tail reaches _head, which means _buf is full.
            n_to_write = std::min(nleft, _head - _tail);
        }

        // Copy the data from input string to _buf starting at _tail.
        std::copy(data.begin() + nwritten, data.begin() + nwritten + n_to_write, _buf.begin() + _tail);
        _tail = (_tail + n_to_write) % _capacity;  // Update _tail , wrapping around if reaching the end of _buf
        _size += n_to_write;
        nwritten += n_to_write;
        nleft -= n_to_write;
    }
    _write_cnt += nwritten;
    return nwritten;
}

//! \param[in] len bytes will be copied from the output side of the buffer
// peek_output is the counterpart of a write operation, but it does not consume data from the buffer
string ByteStream::peek_output(const size_t len) const {
    // Output size is determined by the samllest value between the current buffer size and the size need to read.
    string output(std::min(len, _size), '\0');
    size_t nread = 0, nleft = len, n_to_read;
    size_t head = _head, size = _size;  // Since the function is const, we need to create some local copy

    while (nleft > 0 && size > 0) {
        if (head < _tail) {
            n_to_read = std::min(nleft, _tail - head);
        } else {
            // Note in this iteration, if head == _tail, it means _buf is full.
            // Though head == _tail can also mean _buf is emtpy, but this case is excluded by the while condition.
            n_to_read = std::min(nleft, _capacity - head);
        }
        std::copy(_buf.begin() + head, _buf.begin() + head + n_to_read, output.begin() + nread);
        head = (head + n_to_read) % _capacity;
        size -= n_to_read;
        nread += n_to_read;
        nleft -= n_to_read;
    }

    return output;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t nremoved = std::min(len, _size);
    _head = (_head + nremoved) % _capacity;
    _read_cnt += nremoved;
    _size -= nremoved;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string output = peek_output(len);
    pop_output(len);
    return output;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _size; }

bool ByteStream::buffer_empty() const { return _size == 0; }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _write_cnt; }

size_t ByteStream::bytes_read() const { return _read_cnt; }

size_t ByteStream::remaining_capacity() const { return _capacity - _size; }

// Codes used for test
// #include <iostream>
// int main(void) {
//     ByteStream bs(10);
//     string str = "123456";
//
//     bs.write(str);
//     std::cout << str.size() << endl;
//     std::cout << bs.read(str.size()) << endl;
//
//     str = "789A";
//     bs.write(str);
//     std::cout << str.size() << endl;
//     std::cout << bs.read(str.size()) << endl;
//
//     str = "BCDE";
//     bs.write(str);
//     std::cout << str.size() << endl;
//     std::cout << bs.read(str.size()) << endl;
// }
