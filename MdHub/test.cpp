#include "SpscRingQueue.hpp"
#include "LinearBoard.hpp"
#include "structs.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

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

// A mixing constant used purely to make the checksum field derive from
// counter in a way that isn't a trivial copy of it.
constexpr std::uint64_t kChecksumMixConstant = 0x9e3779b97f4a7c15ULL;

// Builds a snapshot whose fields all encode the same counter value in
// several independent words spread across the (multi-word) struct. A torn
// read that mixes words from two different store() calls will make these
// fields disagree, which is exactly what test_concurrent_linearBoard checks
// for.
MdSnapshot make_consistent_snapshot(std::uint64_t counter) {
    MdSnapshot snapshot{};
    snapshot.quote.ts_ns = static_cast<std::int64_t>(counter);
    snapshot.quote.last_price = static_cast<std::int64_t>(counter);
    snapshot.quote.volume = static_cast<std::int64_t>(counter);
    snapshot.quote.src_seq = counter;
    snapshot.slot_id = static_cast<std::uint32_t>(counter & 0xffffffffULL);
    snapshot.hub_seq = counter;
    snapshot.checksum = counter ^ kChecksumMixConstant;
    return snapshot;
}

bool is_self_consistent(const MdSnapshot& snapshot) {
    const std::uint64_t counter = snapshot.hub_seq;
    return snapshot.quote.ts_ns == static_cast<std::int64_t>(counter) &&
           snapshot.quote.last_price == static_cast<std::int64_t>(counter) &&
           snapshot.quote.volume == static_cast<std::int64_t>(counter) &&
           snapshot.quote.src_seq == counter &&
           snapshot.slot_id ==
               static_cast<std::uint32_t>(counter & 0xffffffffULL) &&
           snapshot.checksum == (counter ^ kChecksumMixConstant);
}

void track_min(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (value < current &&
           !target.compare_exchange_weak(current, value,
                                          std::memory_order_relaxed)) {
    }
}

void track_max(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value,
                                          std::memory_order_relaxed)) {
    }
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

// Genuine multi-threaded (in-process) concurrency test for LinearBoard's
// seqlock. One writer thread hammers store() on a single slot while several
// reader threads concurrently hammer load() on the same slot. Every field in
// MdSnapshot that a successful load() returns must encode the same counter
// value (see make_consistent_snapshot/is_self_consistent above); if load()
// ever returns a torn mix of two different store() calls, this test catches
// it via a field mismatch, since the payload spans many 64-bit words.
void test_concurrent_linearBoard() {
    SharedMemoryCleanup<MdSnapshot> cleanup(test_name("c"));
    LinearBoard<MdSnapshot> board(
        cleanup.name(), 1, LinearBoard<MdSnapshot>::OpenMode::Create);

    // Prime the slot synchronously before any reader/writer thread starts.
    // A freshly-constructed slot's default state (seq=0, all-zero payload)
    // is a legitimate, non-torn seqlock state, but it doesn't correspond to
    // any value make_consistent_snapshot() produces (checksum(0) != 0), so
    // it would look "inconsistent" to is_self_consistent(). A reader that
    // sneaks in before the writer thread's first store() would then observe
    // it and produce a false positive. Priming here removes that race: by
    // the time any thread starts, the slot already holds a real, encoded
    // value.
    board.store(0, make_consistent_snapshot(0));

    constexpr std::uint64_t kWriterIterations = 200'000;
    constexpr int kReaderCount = 4;
    constexpr int kReaderDrainIterations = 2'000;

    std::atomic<bool> writer_running{true};
    std::atomic<bool> inconsistency_found{false};
    std::atomic<std::uint64_t> total_reads{0};
    std::atomic<std::uint64_t> total_failed_loads{0};
    std::atomic<std::uint64_t> min_value_seen{
        std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> max_value_seen{0};

    std::thread writer([&]() {
        for (std::uint64_t i = 0; i < kWriterIterations; ++i) {
            board.store(0, make_consistent_snapshot(i));
        }
        writer_running.store(false, std::memory_order_release);
    });

    auto reader_loop = [&]() {
        std::uint64_t local_reads = 0;
        std::uint64_t local_failed_loads = 0;

        auto attempt_load = [&]() {
            MdSnapshot snapshot{};
            if (!board.load(0, snapshot)) {
                ++local_failed_loads;
                return;
            }
            ++local_reads;
            if (!is_self_consistent(snapshot)) {
                inconsistency_found.store(true, std::memory_order_relaxed);
                return;
            }
            track_min(min_value_seen, snapshot.hub_seq);
            track_max(max_value_seen, snapshot.hub_seq);
        };

        while (writer_running.load(std::memory_order_acquire)) {
            attempt_load();
        }
        // A short drain after the writer finishes, so readers also get a
        // chance to observe the final stored value.
        for (int i = 0; i < kReaderDrainIterations; ++i) {
            attempt_load();
        }

        total_reads.fetch_add(local_reads, std::memory_order_relaxed);
        total_failed_loads.fetch_add(local_failed_loads,
                                      std::memory_order_relaxed);
    };

    std::vector<std::thread> readers;
    readers.reserve(static_cast<std::size_t>(kReaderCount));
    for (int i = 0; i < kReaderCount; ++i) {
        readers.emplace_back(reader_loop);
    }

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }

    if (inconsistency_found.load()) {
        throw std::runtime_error(
            "LinearBoard concurrent test observed a torn/inconsistent read: "
            "a successful load() returned fields that disagree on the "
            "stored counter value");
    }

    if (total_reads.load() == 0) {
        throw std::runtime_error(
            "LinearBoard concurrent test observed zero successful reads");
    }

    const std::uint64_t min_seen = min_value_seen.load();
    const std::uint64_t max_seen = max_value_seen.load();
    if (min_seen == std::numeric_limits<std::uint64_t>::max() ||
        max_seen <= min_seen) {
        throw std::runtime_error(
            "LinearBoard concurrent test did not observe multiple distinct "
            "values; readers and writer may not have actually overlapped");
    }

    std::cout << "Concurrent LinearBoard test: successful_reads="
              << total_reads.load()
              << " failed_loads=" << total_failed_loads.load()
              << " distinct_value_range=[" << min_seen << ", " << max_seen
              << "]" << std::endl;
}

int main() {
    test_linearBoard();
    test_snapshot_linearBoard();
    test_cross_process_linearBoard();
    test_concurrent_linearBoard();
    test_spsc_ring_queue();

    return 0;
}
