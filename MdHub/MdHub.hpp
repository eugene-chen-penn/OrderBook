#include "structs.hpp"
#include "Subscriber.hpp"
#include <cstdint>
#include <memory>

class MdHub {
public:
    // 撮合引擎（生产者线程）调用；满则返回 false（由引擎侧决定丢弃计数或短暂自旋）
    bool push_quote(const QuoteEvent& ev);
    std::shared_ptr<Subscriber> create_subscriber();
    std::uint64_t quotes_received() const;
    std::uint64_t events_dropped(std::uint32_t subscriber_idx) const;
};