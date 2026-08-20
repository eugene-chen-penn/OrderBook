#include "LinearBoard.hpp"
#include "structs.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr std::uint64_t kLinearBoardMagic = 0x4c494e424f415244ULL; // LINBOARD
constexpr std::uint64_t kLinearBoardVersion = 1;
std::atomic<std::uint64_t> unique_name_sequence{0};

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

void lock_fd(int fd, int operation) {
#if defined(__linux__)
    while (::flock(fd, operation) == -1) {
        if (errno != EINTR) {
            throw_system_error("flock");
        }
    }
#else
    // Linux shmfs supports flock(), which closes the create/open race. Other
    // POSIX implementations need no lock for the repository's local tests.
    (void)fd;
    (void)operation;
#endif
}

class FileDescriptor {
public:
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    ~FileDescriptor() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_;
};

} // namespace

template <typename T>
std::string LinearBoard<T>::normalize_name(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("shared-memory name must not be empty");
    }

    std::string normalized;
    if (name.front() != '/') {
        normalized.push_back('/');
    }
    normalized.append(name);

    if (normalized.size() == 1 || normalized.find('/', 1) != std::string::npos ||
        normalized.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "shared-memory name must contain one leading slash and no others");
    }
    return normalized;
}

template <typename T>
std::string LinearBoard<T>::make_unique_name() {
    const auto sequence =
        unique_name_sequence.fetch_add(1, std::memory_order_relaxed);
    return "/lb_" + std::to_string(::getpid()) + "_" +
           std::to_string(sequence);
}

template <typename T>
LinearBoard<T>::LinearBoard(std::size_t slot_count)
    : LinearBoard(make_unique_name(), slot_count, OpenMode::Create) {
    unlink_on_destroy_ = true;
}

template <typename T>
LinearBoard<T>::LinearBoard(std::string name,
                            std::size_t slot_count,
                            OpenMode mode)
    : name_(normalize_name(name)), slot_count_(slot_count) {
    if (slot_count == 0) {
        throw std::invalid_argument("slot_count must be greater than zero");
    }
    if (slot_count >
        (std::numeric_limits<std::size_t>::max() - sizeof(Header)) /
            sizeof(Slot)) {
        throw std::length_error("LinearBoard mapping is too large");
    }

    mapping_size_ = sizeof(Header) + slot_count * sizeof(Slot);
    if (mapping_size_ >
        static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::length_error("LinearBoard mapping exceeds off_t range");
    }

    const int flags = mode == OpenMode::Create
                          ? O_RDWR | O_CREAT | O_EXCL
                          : O_RDWR;
    const int raw_fd = ::shm_open(name_.c_str(), flags, S_IRUSR | S_IWUSR);
    if (raw_fd == -1) {
        throw_system_error("shm_open");
    }
    FileDescriptor fd(raw_fd);

    try {
        if (mode == OpenMode::Create) {
            lock_fd(fd.get(), LOCK_EX);
            if (::ftruncate(fd.get(), static_cast<off_t>(mapping_size_)) == -1) {
                throw_system_error("ftruncate");
            }
            creator_pid_ = static_cast<std::int64_t>(::getpid());
        } else {
            struct stat status {};
            constexpr int kInitializationAttempts = 1000;
            for (int attempt = 0; attempt < kInitializationAttempts; ++attempt) {
                lock_fd(fd.get(), LOCK_EX);
                if (::fstat(fd.get(), &status) == -1) {
                    throw_system_error("fstat");
                }
                if (status.st_size != 0 || attempt + 1 == kInitializationAttempts) {
                    break;
                }

                // An opener can observe the name after shm_open(O_EXCL) but
                // before the creator acquires flock and calls ftruncate.
                lock_fd(fd.get(), LOCK_UN);
                const struct timespec delay {0, 1'000'000};
                while (::nanosleep(&delay, nullptr) == -1 && errno == EINTR) {
                }
            }
            if (status.st_size < static_cast<off_t>(mapping_size_)) {
                throw std::runtime_error(
                    "shared-memory size (" + std::to_string(status.st_size) +
                    ") is smaller than LinearBoard layout (" +
                    std::to_string(mapping_size_) + ")");
            }
        }

        mapping_ = ::mmap(nullptr, mapping_size_, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd.get(), 0);
        if (mapping_ == MAP_FAILED) {
            mapping_ = nullptr;
            throw_system_error("mmap");
        }

        auto* const header = static_cast<Header*>(mapping_);
        slots_ = reinterpret_cast<Slot*>(
            static_cast<std::byte*>(mapping_) + sizeof(Header));

        if (mode == OpenMode::Create) {
            ::new (header) Header{kLinearBoardMagic,
                                  kLinearBoardVersion,
                                  slot_count_,
                                  sizeof(T),
                                  sizeof(Slot),
                                  mapping_size_};
            for (std::size_t i = 0; i < slot_count_; ++i) {
                ::new (static_cast<void*>(&slots_[i])) Slot();
            }
        } else if (header->magic != kLinearBoardMagic ||
                   header->version != kLinearBoardVersion ||
                   header->slot_count != slot_count_ ||
                   header->value_size != sizeof(T) ||
                   header->slot_size != sizeof(Slot) ||
                   header->mapping_size != mapping_size_) {
            throw std::runtime_error(
                "shared-memory header does not match LinearBoard type or size");
        }

        lock_fd(fd.get(), LOCK_UN);
    } catch (...) {
        if (mapping_ != nullptr) {
            ::munmap(mapping_, mapping_size_);
            mapping_ = nullptr;
            slots_ = nullptr;
        }
        if (mode == OpenMode::Create) {
            ::shm_unlink(name_.c_str());
        }
        throw;
    }
}

template <typename T>
LinearBoard<T>::~LinearBoard() {
    if (mapping_ != nullptr) {
        ::munmap(mapping_, mapping_size_);
    }
    if (unlink_on_destroy_ &&
        creator_pid_ == static_cast<std::int64_t>(::getpid())) {
        ::shm_unlink(name_.c_str());
    }
}

template <typename T>
void LinearBoard<T>::store(std::size_t slot, const T& value) {
    if (slot >= slot_count_) {
        throw std::out_of_range("slot index out of range");
    }

    std::array<std::uint64_t, word_count_> words{};
    std::memcpy(words.data(), &value, sizeof(T));

    Slot& target = slots_[slot];
    target.seq.fetch_add(1, std::memory_order_relaxed);
    // Paired with the reader's acquire fence: if a reader observes any new
    // payload word, its final sequence load must also observe this odd epoch.
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < word_count_; ++i) {
        target.data[i].store(words[i], std::memory_order_relaxed);
    }
    target.seq.fetch_add(1, std::memory_order_release);
}

template <typename T>
bool LinearBoard<T>::load(std::size_t slot, T& out) const {
    if (slot >= slot_count_) {
        throw std::out_of_range("slot index out of range");
    }

    const Slot& target = slots_[slot];
    const std::uint64_t first = target.seq.load(std::memory_order_acquire);
    if ((first & 1U) != 0) {
        return false;
    }

    std::array<std::uint64_t, word_count_> words{};
    for (std::size_t i = 0; i < word_count_; ++i) {
        words[i] = target.data[i].load(std::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t second = target.seq.load(std::memory_order_relaxed);
    if (first != second) {
        return false;
    }

    std::memcpy(&out, words.data(), sizeof(T));
    return true;
}

template <typename T>
void LinearBoard<T>::unlink(std::string_view name) {
    const std::string normalized = normalize_name(name);
    if (::shm_unlink(normalized.c_str()) == -1 && errno != ENOENT) {
        throw_system_error("shm_unlink");
    }
}

template class LinearBoard<std::uint64_t>;
template class LinearBoard<MdSnapshot>;
