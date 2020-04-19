#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <bitset>

using namespace std;

#define MAX_FILTER_LEN 10000

using namespace std;

class bloom
{
private:
    uint32_t hash1(uint64_t num) {
        return num;
    }

    uint32_t hash2(uint64_t num) {
        uint64_t hash = num;
        hash = (hash<<16)^(hash<<32)^(hash<<48)^(hash>>16)^(hash>>32)^(hash>>48);
        return hash;
    }

    uint32_t hash3(uint64_t num) {
        uint64_t hash = num;
        hash = (hash<<8)^(hash<<24)^(hash<<40)^(hash>>8)^(hash>>24)^(hash>>40);
        return hash;
}
public:
    explicit bloom() {}
    void insert(uint64_t num) { 
        bitSet.set(hash1(num) % MAX_FILTER_LEN, 1);
        bitSet.set(hash2(num) % MAX_FILTER_LEN, 1);
        bitSet.set(hash3(num) % MAX_FILTER_LEN, 1);
    }
    bool isExist(uint64_t num) {
        return bitSet.test(hash1(num) % MAX_FILTER_LEN) &&
               bitSet.test(hash2(num) % MAX_FILTER_LEN) &&
               bitSet.test(hash3(num) % MAX_FILTER_LEN);
    }
    void clear() { bitSet.reset(); }

protected:
    bitset<MAX_FILTER_LEN> bitSet;
};


