#include "SkipList.hh"
#include "bloom.hh"
#include "LSM.hh"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <time.h>

#include <random>

#define TEST_SIZE (1 << 20)

using namespace std;

char *randstr(char *str, const int len) {
    int i;
    for (i = 0; i < len; ++i) {
        switch ((rand() % 3)) {
        case 1:
            str[i] = 'A' + rand() % 26;
            break;
        case 2:
            str[i] = 'a' + rand() % 26;
            break;
        default:
            str[i] = '0' + rand() % 10;
            break;
        }
    }
    return str;
}

void doSomething(LSM<uint64_t, string> &lsm, uint64_t key, SkipList<uint64_t, string> *memTab = nullptr) {
    switch(rand() % 2) {
        case 0: {
            char ranStr[100]; int len = rand() % 99;
            randstr(ranStr, len);
            lsm.put(key, string(ranStr, len));
            if (memTab) { memTab->put(key, string(ranStr, len)); }
            break;
        }
        case 1: {
            lsm.remove(key);
            if (memTab) { memTab->remove(key); }
            break;
        }
    }
}

void correctnessTest(LSM<uint64_t, string> &lsm, uint64_t size) {
    lsm.reset();
    SkipList<uint64_t, string> memTab;
    for (uint64_t i = 0; i < size; ++i) {
        doSomething(lsm, i, &memTab);
    }
    for (uint64_t i = 0; i < size; ++i) {
        doSomething(lsm, i, &memTab);
    }

    uint64_t cnt = 0;
    auto data = memTab.data();
    for (uint64_t i = 0; i < size; ++i) {
        string lsmGet = lsm.get(i), *memGet = memTab.get(i);
        if (memGet) {
            if (lsmGet != *memGet) {
                cout << "LSM get: "<< lsmGet << endl << "MenTable get: " << *memGet << endl;
            }
            else { ++cnt;}
        }
        else {
            if (!lsmGet.empty()) { cout << "LSM get: "<< lsmGet << endl << "MenTable get: " << endl; }
            else { ++cnt; }
        }
    }
    cout << "Correctness Test Result: " << cnt << '/' << size << " => " << double(cnt) / size * 100 << '%' << endl;

}

struct Lat {
    double putLat;
    double getLat;
    double delLat;
    uint64_t size;
};


void latencyTest(LSM<uint64_t, string> &lsm, uint64_t size) {
    lsm.reset();
    vector<Lat> latRecd;
    clock_t start;
    for (uint64_t i = 0; i < size; ++i) {
        Lat lat;
        char ranStr[100]; int len = rand() % 100;
        randstr(ranStr, len);
        string input = string(ranStr, len);
        start = clock(); bool flag = lsm.put(i, input); lat.putLat = double(clock() - start) / CLOCKS_PER_SEC * 1000;
        if (flag) {
            start = clock(); lsm.get(i); lat.getLat = double(clock() - start) / CLOCKS_PER_SEC * 1000;
            start = clock(); lsm.remove(i); lat.delLat = double(clock() - start) / CLOCKS_PER_SEC * 1000;
            latRecd.push_back(lat);
        }
    }

    double avePut = 0, aveGet = 0, aveDel = 0;
    for (auto i = latRecd.begin(); i != latRecd.end(); ++i) {
        avePut += i->putLat;
        aveGet += i->getLat;
        aveDel += i->delLat;
    }

    avePut /= latRecd.size(); aveGet /= latRecd.size(); aveDel /= latRecd.size();

    cout << "Latency Test Result: " << "Put: " << avePut << "ms " << "Get: " << aveGet << "ms " << "Delete: " << aveDel << "ms" << endl;
}

void throughputTest(LSM<uint64_t, string> &lsm, uint64_t size) {
    lsm.reset();
    uint64_t i = 0;
    vector<int> t;
    char data[100][100];
    for (int i = 0; i < 100; ++i) { randstr(data[i], 100); }

    clock_t start = clock(); uint64_t cnt = 0;
    while (i < size) {
        if (clock() - start > 200) {
            t.push_back(cnt);
            cnt = 0;
            start = clock();
        }
        lsm.put(i, string(data[i % 100], rand() % 100)); ++cnt; ++i;
    }
    cout << "Throughput Test Result:" << endl;
    cout << '[';
    uint64_t c = 0;
    for (auto &i:t) {
        cout << '[' << c++ << ',' << i << ']' << ',';
    }
    cout << ']';
}

int main() {
    srand(time(0));
    LSM<uint64_t, string> lsm("./data");
    // correctnessTest(lsm, TEST_SIZE);
    // latencyTest(lsm, TEST_SIZE);
    throughputTest(lsm, TEST_SIZE);
    return 0;
}