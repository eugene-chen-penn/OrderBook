class ILinearBoard {
    public:
        virtual ~ILinearBoard() = default;

        /**
         * @brief 写入/更新指定索引的数据
         * @param index 数据槽的整数索引 (0 到 capacity-1)
         * @param data 要写入的 POD 数据指针
         */
        virtual void update(uint32_t index, const void* data) = 0;

        /**
         * @brief 读取指定索引的最新数据 (基于 SeqLock 无锁读取)
         * @param index 槽的整数索引
         * @param outData 用于接收数据的外部预分配内存
         * @return 如果读取成功返回 true，如果索引越界或未初始化返回 false
         */
        virtual bool read(uint32_t index, void* outData) const = 0;

        /**
         * @brief 获取 Board 容量
         */
        virtual size_t capacity() const = 0;

        /**
         * @brief 获取每个元素的大小
         */
        virtual size_t elementSize() const = 0;
    };

 /**
     * @brief 高性能自旋锁
     * 强制 64 字节对齐，防止将其嵌入结构体时与其他变量发生缓存行伪共享 (False Sharing)
     */
    class alignas(64) SpinLock {
        public:
        SpinLock() = default;
        ~SpinLock() = default;

        // 禁用拷贝和赋值
        SpinLock(const SpinLock&) = delete;
        SpinLock& operator=(const SpinLock&) = delete;

        /**
         * @brief 获取锁 (自旋等待)
         */
        void lock() noexcept {
            // 使用 acquire 语义确保锁获取后的内存读写不会被重排到获取锁之前
            while (m_flag.test_and_set(std::memory_order_acquire)) {
                // 忙等优化：使用 pause 指令，防止 CPU 功耗过高和流水线乱序惩罚
#if defined(__x86_64__) || defined(__i386__)
                _mm_pause();
#elif defined(__aarch64__)
                __asm__ volatile("yield" ::: "memory");
#endif
            }
        }

        /**
         * @brief 尝试获取锁
         * @return 成功返回 true，被占用返回 false
         */
        bool try_lock() noexcept {
            return !m_flag.test_and_set(std::memory_order_acquire);
        }

        /**
         * @brief 释放锁
         */
        void unlock() noexcept {
            // 使用 release 语义确保锁释放前的内存读<E5AD98><E8AFBB><EFBFBD>已经完成
            m_flag.clear(std::memory_order_release);
        }

        private:
        std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
    };

/**
 * @brief 顺序锁 (Seqlock)
 * 读端完全无锁，写端使用自带的 SpinLock 互斥。
 */
class alignas(64) SeqLock {
public:
    SeqLock() = default;

    // ================= 写端 API =================

    void write_lock() noexcept {
        m_writerMutex.lock(); // 先锁住其他写线程
        // 序列号加1，变成奇数，表示正在写入
        m_seq.fetch_add(1, std::memory_order_release);
    }

    void write_unlock() noexcept {
        // 序列号再加1，变成偶数，表示写入完成
        m_seq.fetch_add(1, std::memory_order_release);
        m_writerMutex.unlock();
    }

    // ================= 读端 API =================

    /**
     * @brief 读开始，返回当前序列号
     */
    size_t read_begin() const noexcept {
        size_t seq;
        while (true) {
            seq = m_seq.load(std::memory_order_acquire);
            // 如果是偶数，说明当前没有人在写，可以安全读取
            if ((seq & 1) == 0) {
                break;
            }
            // 奇数说明有写线程正在写入，读者自旋退让
#if defined(__x86_64__) || defined(__i386__)
            _mm_pause();
#endif
        }
        return seq;
    }

    /**
     * @brief 读结束验证
     * @param beginSeq read_begin() 返回的序列号
     * @return true表示读取过程中被篡改，需要重试；false表示安全，数据有效
     */
    bool read_retry(size_t beginSeq) const noexcept {
        // 加入 acquire 内存屏障，防止读操作被重排到 seq.load 之后
        std::atomic_thread_fence(std::memory_order_acquire);
        return m_seq.load(std::memory_order_relaxed) != beginSeq;
    }

private:
    SpinLock m_writerMutex;          // 保护多个写线程
    std::atomic<size_t> m_seq{0};    // 顺序计数器 (偶数: 稳定, 奇数: 写入中)
};

/**
 * @brief 顺手提供一个泛型包装类，用于包裹平凡类型 (Trivially Copyable)
 * 极简用法：
 * SeqData<Tick> tickData;
 * tickData.store(newTick); // 写端
 * Tick t = tickData.load(); // 读端
 */
template <typename T>
    requires std::is_trivially_copyable_v<T>
class SeqData {
public:
    void store(const T& val) noexcept {
        m_lock.write_lock();
        m_data = val;
        m_lock.write_unlock();
    }

    T load() const noexcept {
        T copy;
        size_t seq;
        do {
            seq = m_lock.read_begin();
            copy = m_data; // 复制数据
        } while (m_lock.read_retry(seq)); // 如果读取时被修改，则重试
        return copy;
    }

private:
    SeqLock m_lock;
    T m_data;
};