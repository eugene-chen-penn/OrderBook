#include <cstdint>

using namespace std;

// 撮合引擎 → MdHub 的每一条行情（十档盘口 + 成交信息）
struct QuoteEvent {
    char     symbol[16];     // "600519.SH" 风格，'\0' 结尾
    int64_t  ts_ns;          // 引擎侧时间戳（steady_clock 纳秒）
    int64_t  bid_price[10];  // 买一到买十，定点 ×10000（真实值 = 字段/10000），无档位填 0
    int64_t  bid_volume[10];
    int64_t  ask_price[10];  // 卖一到卖十
    int64_t  ask_volume[10];
    int64_t  last_price;     // 最新成交价，定点 ×10000
    int64_t  volume;         // 累计成交量（股）
    int64_t  turnover;       // 累计成交额（定点：元×10000）
    int64_t  high_limit;     // 涨停价，定点 ×10000
    int64_t  low_limit;      // 跌停价，定点 ×10000
    uint64_t src_seq;        // 引擎侧全局递增序号
};

// 快照板槽位内容 = QuoteEvent + 中枢管理字段
struct MdSnapshot {
    QuoteEvent quote;
    uint32_t   slot_id;      // = 所在槽位号
    uint64_t   hub_seq;      // 中枢侧该合约递增序号，每发布一次 +1
    uint64_t   checksum;     // 对 quote 全字段 + hub_seq 做混合校验
};                           // （FNV-1a / Murmur 尾乘任选，读侧同算法复验）

// 发给订阅者的通知 —— 只有"哪里变了"，没有"变成什么"
struct MdEvent {
    uint32_t slot_id;
    uint64_t hub_seq;        // 发布该事件时的快照序号
    int64_t  ts_ns;          // hub 收到该条行情的时刻
};