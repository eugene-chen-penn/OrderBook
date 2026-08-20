#include "SpscRingQueue.hpp"
#include "LinearBoard.hpp"
#include "structs.hpp"

#include <cerrno>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace {

template <typename T>
class SharedMemoryCleanup {
public:
    explicit SharedMemoryCleanup(std::string name) : name_(std::move(name)) {
        LinearBoard<T>::unlink(name_);
    }

    ~SharedMemoryCleanup() {
        try {
            LinearBoard<T>::unlink(name_);
        } catch (...) {
        }
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};

std::string test_name(const char* suffix) {
    return "/lbt_" + std::to_string(::getpid()) + "_" + suffix;
}

} // namespace

void test_spsc_ring_queue() {
    SpscRingQueue<std::uint64_t> queue(5);
    std::uint64_t v;
    std::cout<<queue.pop(v)<<std::endl;

    // Test pushing elements into the queue
    for (std::uint64_t i = 0; i < 5; ++i) {
        if (queue.push(i)) {
            std::cout << "Pushed: " << i << std::endl;
        } else {
            std::cout << "Queue is full, could not push: " << i << std::endl;
        }
    }

    // Test popping elements from the queue
    std::uint64_t value;
    for (std::uint64_t i = 0; i < 5; ++i) {
        if (queue.pop(value)) {
            std::cout << "Popped: " << value << std::endl;
        } else {
            std::cout << "Queue is empty, could not pop." << std::endl;
        }
    }

    std::cout<<queue.pop(v)<<std::endl;

    return;
}

void test_linearBoard() {
    SharedMemoryCleanup<std::uint64_t> cleanup(test_name("i"));
    LinearBoard<std::uint64_t> board(
        cleanup.name(), 5, LinearBoard<std::uint64_t>::OpenMode::Create);
    std::uint64_t v;
    std::cout<<board.load(0,v)<<std::endl;

    // Test storing elements into the board
    for (std::size_t i = 0; i < 5; ++i) {
        board.store(i, i);
        std::cout << "Stored: " << i << " at slot: " << i << std::endl;
    }

    // Test loading elements from the board
    std::uint64_t value;
    for (std::size_t i = 0; i < 5; ++i) {
        if (board.load(i, value)) {
            std::cout << "Loaded: " << value << " from slot: " << i << std::endl;
        } else {
            std::cout << "Write window active, could not load from slot: " << i << std::endl;
        }
    }

    std::cout<<board.load(0,v)<<std::endl;

    return;
}

void test_snapshot_linearBoard() {
    SharedMemoryCleanup<MdSnapshot> cleanup(test_name("s"));
    LinearBoard<MdSnapshot> board(
        cleanup.name(), 1, LinearBoard<MdSnapshot>::OpenMode::Create);
    MdSnapshot expected{};
    expected.slot_id = 7;
    expected.hub_seq = 42;
    expected.quote.last_price = 114800;
    board.store(0, expected);

    MdSnapshot actual{};
    if (!board.load(0, actual) || actual.slot_id != expected.slot_id ||
        actual.hub_seq != expected.hub_seq ||
        actual.quote.last_price != expected.quote.last_price) {
        throw std::runtime_error("LinearBoard snapshot test failed");
    }
}

void test_cross_process_linearBoard() {
    SharedMemoryCleanup<std::uint64_t> cleanup(test_name("x"));
    LinearBoard<std::uint64_t> writer(
        cleanup.name(), 2, LinearBoard<std::uint64_t>::OpenMode::Create);
    writer.store(1, 0x123456789abcdef0ULL);

    const pid_t child = ::fork();
    if (child == -1) {
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        try {
            LinearBoard<std::uint64_t> reader(
                cleanup.name(), 2,
                LinearBoard<std::uint64_t>::OpenMode::OpenExisting);
            std::uint64_t value = 0;
            const bool valid = reader.load(1, value);
            ::_exit(valid && value == 0x123456789abcdef0ULL ? 0 : 1);
        } catch (const std::exception& error) {
            const std::string message =
                std::string("child reader error: ") + error.what() + "\n";
            static_cast<void>(
                ::write(STDERR_FILENO, message.data(), message.size()));
            ::_exit(2);
        } catch (...) {
            ::_exit(2);
        }
    }

    int status = 0;
    pid_t result;
    do {
        result = ::waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error(
            "cross-process LinearBoard read failed (status=" +
            std::to_string(status) + ")");
    }
}

int main() {
    test_linearBoard();
    test_snapshot_linearBoard();
    test_cross_process_linearBoard();
    test_spsc_ring_queue();

    return 0;
}
