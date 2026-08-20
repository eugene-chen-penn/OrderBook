#include "structs.hpp"
#include <vector>

class MdHub;

class Subscriber {
public:
    explicit Subscriber(MdHub& hub);
    void subscribe(const std::vector<std::string>& symbols); // 空列表 = 订阅全部
    MdEvent next_event();                                    // 阻塞等待（条件变量唤醒可以，
                                                             // 但不得锁住板/队列公共路径）
    bool read_snapshot(uint32_t slot_id, MdSnapshot& out);    // 含重试；false = 一直处于写入窗口
};