#include <cstdint>
#include <string>
#include <unordered_map>

using namespace std;

class SymbolDict {
public:
    uint32_t register_symbol(const char* symbol);  // 重复注册返回相同槽位号
    bool lookup(const char* symbol, uint32_t& slot_id) const;
    bool lookup(uint32_t slot_id, char (&symbol)[16]) const;
    uint32_t count() const;
private:
    std::unordered_map<char8_t[16], uint32_t> entries_;
    uint32_t count_;
};