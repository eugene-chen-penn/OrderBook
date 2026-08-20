#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

template <typename T>
class LinearBoard {
    static_assert(std::is_trivially_copyable_v<T>,
                  "LinearBoard values must be trivially copyable");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "LinearBoard requires lock-free 64-bit atomics");

public:
    enum class OpenMode {
        Create,
        OpenExisting,
    };

    // Creates a board with a unique name. The name can be passed to another
    // process via shared_memory_name() while this shared-memory object exists.
    explicit LinearBoard(std::size_t slot_count);

    // Create fails if name already exists; OpenExisting fails if it does not.
    LinearBoard(std::string name, std::size_t slot_count, OpenMode mode);
    ~LinearBoard();

    LinearBoard(const LinearBoard&) = delete;
    LinearBoard& operator=(const LinearBoard&) = delete;
    LinearBoard(LinearBoard&&) = delete;
    LinearBoard& operator=(LinearBoard&&) = delete;

    void store(std::size_t slot, const T& value); // exactly one writer
    bool load(std::size_t slot, T& out) const;    // many readers

    [[nodiscard]] std::size_t size() const noexcept { return slot_count_; }
    [[nodiscard]] const std::string& shared_memory_name() const noexcept {
        return name_;
    }

    // POSIX shared-memory names persist until explicitly unlinked. Existing
    // mappings remain valid after unlinking.
    static void unlink(std::string_view name);

private:
    static constexpr std::size_t word_count_ =
        (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

    struct alignas(64) Slot {
        std::atomic<std::uint64_t> seq{0};
        std::array<std::atomic<std::uint64_t>, word_count_> data{};
    };

    struct alignas(64) Header {
        std::uint64_t magic;
        std::uint64_t version;
        std::uint64_t slot_count;
        std::uint64_t value_size;
        std::uint64_t slot_size;
        std::uint64_t mapping_size;
    };

    static std::string normalize_name(std::string_view name);
    static std::string make_unique_name();

    std::string name_;
    std::size_t slot_count_{};
    std::size_t mapping_size_{};
    void* mapping_{nullptr};
    Slot* slots_{nullptr};
    bool unlink_on_destroy_{false};
    std::int64_t creator_pid_{-1};
};
