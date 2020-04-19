#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <cmath>
#include <fstream>
#include "SkipList.hh"
#include "bloom.hh"

using namespace std;
using namespace std::filesystem;

#define MEM_MAX_BYTES (1 << 21)
#define NUM_PER_LEVEL 4
#define TIMES_PER_LEVEL 2
#define MAX_SST_NUM(level) (NUM_PER_LEVEL * pow(2, (level)))
#define GENERATE_FILENAME(dir, level, inLevel) ((dir) + '/' + to_string(level) + to_string(inLevel) + ".bin")

struct Bin {
    Bin(char *_bin, uint32_t _length): bin(_bin), length(_length) {}
    char *bin;
    uint32_t length;
};

template<class K>
class Indices {
private:
    uint32_t size;
    uint32_t dataSegBias;
    vector<K> key;
    vector<uint32_t> bias;
    vector<uint32_t> length;
    bloom filter;

    uint32_t binarySearch(K a[], int n , K target) {
	    int low = 0, high = n - 1, middle;
	    while(low <= high) {
	        middle = (low + high) / 2;
            if(target == a[middle]) { return middle; }
	        else if(target > a[middle]) { low = middle + 1; }
	        else if(target < a[middle]) { high = middle - 1; }
	    }
	    return -1;
    }
public:
    explicit Indices(const Bin &bin, uint32_t _size, uint32_t _dataSegBias)
        : size(_size), dataSegBias(_dataSegBias) {
        char *indices = bin.bin;
        while (indices - bin.bin < bin.length) {
            K k = *(K *)indices; indices += sizeof(K);
            uint32_t datumBias = *(uint32_t *)indices; indices += 4;
            uint32_t datumLen = *(uint32_t *)indices; indices += 4;
            filter.insert(k);
            key.push_back(k);
            bias.push_back(datumBias);
            length.push_back(datumLen);
        }
    }

    bool find(const K &k, uint32_t *b = nullptr, uint32_t *l = nullptr) {
        if (filter.isExist(k)) {
            uint32_t pos = binarySearch(key.data(), key.size(), k);
            if (pos != -1) {
                if (b) { *b = bias.at(pos); }
                if (l) { *l = length.at(pos); }
                return true;
            }
        }

        return false;
    }

    uint32_t getSize() const { return size; }
    uint32_t getDataSegBias() const { return dataSegBias; }

    uint32_t getLowBound() const { return key.front(); }
    uint32_t getHighBound() const { return key.back(); }

};


template<class K, class V>
class SST {
private:
    vector<Entry<K, V> > data;
    uint32_t size;
    uint32_t dataSegBias;
    uint32_t dataBytes;
    char *bin;

public:
    /* From Memory to Disk */
    explicit SST(const vector<Entry<K, V> > &_data, uint32_t _dataBytes): data(_data), dataBytes(_dataBytes), bin(nullptr) { toBin(); }
    explicit SST(const SST<K, V> & ano): data(ano.data), size(ano.size), dataSegBias(ano.dataSegBias), dataBytes(ano.dataBytes) {
        bin = new char[size];
        memcpy(bin, ano.bin, size);
    }

    /* From Disk to Memory */
    explicit SST(char *_bin): bin(_bin) {
        size = *(uint32_t *)_bin;   /* Unused */
        dataSegBias = *(uint32_t *)(_bin + 4);
        char *indices = _bin + 8;
        char *datum = _bin + dataSegBias; _bin = datum;
        /* String Limited */
        #ifdef STRING
        if (typeid(V) == typeid(string)) {
            while (indices != _bin) {
                K k = *(K *)indices; indices += sizeof(K);
                indices += 4;    /* Unused Size */
                uint32_t datumLen = *(uint32_t *)indices; indices += 4;
                V v(datum, datumLen);
                datum += datumLen;
                data.push_back(Entry<K, V>(k, v));
            }

            dataBytes = datum - _bin;
        }
        #endif
    }
    ~SST() { if (bin) { delete []bin; } }

    Bin toBin() {
        uint32_t idxBytes = data.size() * (sizeof(K) + 8);
        /* Size{4} + Bias{4} + Indices{n * (sizeof(K) + dataBias{4} + dataLength{4})} + Datum{dataBytes} */
        uint32_t capacity = 8 + idxBytes + dataBytes;

        size = capacity;
        dataSegBias = 8 + idxBytes;

        if (bin) { return Bin(bin, capacity); }
        else {
            char *ret = bin = new char[capacity];
            /* String Limited */
            #ifdef STRING
            if (typeid(V) == typeid(string)) {
                /* Set Bias Segment */
                *(uint32_t *)ret = capacity; ret += 4;

                /* Set Bias Segment */
                *(uint32_t *)ret = 8 + idxBytes; ret += 4;

                /* Set Indices Segment */
                uint32_t pos = 0;
                for (uint32_t i = 0; i < data.size(); ++i) {
                    *(K *)ret = data.at(i).key; ret += sizeof(K);
                    *(uint32_t *)ret = pos; ret += 4;
                    *(uint32_t *)ret = data.at(i).value.size(); ret +=4;
                    pos += data.at(i).value.size();
                }

                /* Set Datum Segment */
                for (uint32_t i = 0; i < data.size(); ++i) {
                    V val = data.at(i).value;
                    memcpy(ret, val.c_str(), val.size());
                    ret += val.size();
                }
            }
            #endif
        }
        return Bin(bin, capacity);
    }

    Bin toIndexBin() {
        if (!bin) { toBin(); }
        uint32_t idxCapacity = data.size() * (sizeof(K) + 8);
        return Bin(bin + 8, idxCapacity);
    }

    vector<Entry<K, V> > &vecData() {
        return data;
    }

    uint32_t getSize() const { return size; }
    uint32_t getDataSegBias() const { return dataSegBias; }

    K getLowBound() const { return data.front().key; }
    K getHighBound() const { return data.back().key; }
        
};

template <class K>
class IndicesTab {
private:
    string Dir;
    vector<Indices<K> > chaosLevel;
    vector<vector<Indices<K> > > orderedLevel;
public:
    explicit IndicesTab(const string &_dir): Dir(_dir) {
        path dir(_dir);
        if (!exists(dir)) { assert(create_directory(dir)); }

        /* Read Indices From Disk */
        directory_iterator SSTs(dir);
        int level = 0, inLevel = 0;
        bool found = true;
        while (found) {
            string nextFile = GENERATE_FILENAME(_dir, level, inLevel);
            found = exists(nextFile);
            if (found) {
                if (orderedLevel.size() < level) {
                    orderedLevel.push_back(vector<Indices<K> >());
                }
                ifstream in(nextFile); assert(in);
                char prefixBuf[8];
                in.read(prefixBuf, 8);

                uint32_t idxReadNum = *(uint32_t *)(prefixBuf + 4) - 8;
                char *idxBuff = new char[idxReadNum];
                in.read(idxBuff, idxReadNum);
                in.close();
                Indices<K> idx(Bin(idxBuff, idxReadNum), *(uint32_t *)prefixBuf, *(uint32_t *)(prefixBuf + 4));
                if (level == 0 && inLevel < NUM_PER_LEVEL) { chaosLevel.push_back(idx); }
                else if (inLevel < MAX_SST_NUM(level)) { orderedLevel.back().push_back(idx); }
                ++inLevel;
                if (inLevel == MAX_SST_NUM(level)) {
                    ++level; inLevel = 0;
                    orderedLevel.push_back(vector<Indices<K> >());
                }
                delete []idxBuff;
            }
            else if (inLevel != 0 || level == 0) { 
                ++level; inLevel = 0; found = true;
            }
        }
    }

    bool insert(const Indices<K> &idx, string &filename) {
        if (chaosLevel.size() == NUM_PER_LEVEL) { return false; }
        filename = GENERATE_FILENAME(Dir, 0, chaosLevel.size());
        chaosLevel.push_back(idx);
        return true;
    }

    vector<Indices<K> > *rLevel(uint32_t levelNum) {
        if (levelNum == 0) { return &chaosLevel; }
        else { return &orderedLevel[levelNum - 1]; }
    }

    uint32_t getHeight() const { return 1 + orderedLevel.size(); }

    bool find(const K &key, 
              string *filename = nullptr, uint32_t *dataSegBias= nullptr,
              uint32_t *bias = nullptr, uint32_t *length = nullptr) {
        for (auto i = chaosLevel.rbegin(); i != chaosLevel.rend(); ++i) {
            if (i->find(key, bias, length)) {
                if (*length == 0) { return false; }
                else { 
                    *filename = GENERATE_FILENAME(Dir, 0, chaosLevel.size() - 1 - (i - chaosLevel.rbegin()));
                    *dataSegBias = i->getDataSegBias();
                    return true; 
                }
            }
        }

        for (auto i = orderedLevel.begin(); i != orderedLevel.end(); ++i) {
            for (auto j = i->begin(); j != i->end(); ++j) {
                if (j->find(key, bias, length)) {
                    if (*length == 0) { return false; }
                    *filename = GENERATE_FILENAME(Dir, i - orderedLevel.begin() + 1, j - i->begin());
                    *dataSegBias = j->getDataSegBias();
                    return true; 
                }
            }
        }

        return false;
    }

    void addNewLevel() { orderedLevel.push_back(vector<Indices<K> >()); }
    void clear() { chaosLevel.clear(); orderedLevel.clear(); }
};

template<class K, class V>
class LSM {
private:
    string Dir;
    SkipList<K, V> memTab;
    IndicesTab<K> indices;

    SST<K, V> readSST(const string &filename) {
        ifstream in(filename); assert(in);
        char prefixBuf[8];
        in.read(prefixBuf, 8);
        in.seekg(0, ios::beg);
        char *bin = new char[*(uint32_t *)prefixBuf];
        in.read(bin, *(uint32_t *)prefixBuf);
        in.close();
        return SST<K, V>(bin);
    }

    /* Find and Get Bounds of SSTs Intersected */
    void findIntersectSST(vector<SST<K, V> > &merge, vector<Indices<K> > &curL,
                          uint32_t bmin, uint32_t bmax, uint32_t levelN) {
        vector<int> toBeDeleted;
        for (auto i = curL.begin(); i != curL.end(); ++i) {
            int inLevel = i - curL.begin();
            string filename = GENERATE_FILENAME(Dir, levelN, inLevel);
            SST<K, V> tmp = readSST(filename);
            if (tmp.getHighBound() < bmin || tmp.getLowBound() > bmax) { continue; }
            merge.push_back(tmp);
            std::filesystem::remove(filename);
            toBeDeleted.push_back(inLevel);
        }
        vector<int> toBeMoved(curL.size());
        int cnt = 0;
        for (int i = 0; i < curL.size(); ++i) {
            if (binary_search(toBeDeleted.begin(), toBeDeleted.end(), i)) { toBeMoved[i] = -1; ++cnt; continue; }
            toBeMoved[i] = cnt;
        }
        for (int i = 0; i < curL.size(); ++i) {
            if (toBeMoved[i] > 0) {
                rename(GENERATE_FILENAME(Dir, levelN, i), GENERATE_FILENAME(Dir, levelN, i - toBeMoved[i]));
            }
        }
        vector<Indices<K> > newL;
        for (auto i = curL.begin(); i != curL.end(); ++i) {
            if (!binary_search(toBeDeleted.begin(), toBeDeleted.end(), i - curL.begin())) {
                newL.push_back(*i);
            }
        }
        curL = newL;
        merge = mergeSort(merge);
    }

    /* Newer SSTs Should Be Place Ahead 
     * In Level 0, eg. 03 is Newer than 02 Newer than 01 
     * Level 1 is Newer than Level 2 Newer than ... */
    vector<SST<K, V> > mergeSort(vector<SST<K, V> > &vec) {
        /* Merge */
        vector<Entry<K , V> > aftMerge[2];
        aftMerge[0] = vec.front().vecData();
        for (auto i = vec.begin() + 1; i != vec.end(); ++i) {
            vector<Entry<K , V> > &to = aftMerge[(i - vec.begin()) % 2],
                                  &from = aftMerge[(i - vec.begin() + 1) % 2],
                                  &cur = i->vecData();
            to.clear();
            auto pa = from.begin(), pc = cur.begin();
            while (pa != from.end() && pc != cur.end()) {
                if (pa->key == pc->key) {
                    while (pc != cur.end() && pc->key == pa->key) { ++pc; }
                    to.push_back(*(pa++));
                }
                else { to.push_back(pa->key < pc->key ? *(pa++) : *(pc++)); }
            }
            if (pa == from.end()) {
                for (; pc != cur.end(); ++pc) { to.push_back(*pc); }
            }
            else {
                for (; pa != from.end(); ++pa) { to.push_back(*pa); }
            }
        }
        vector<Entry<K, V> > &final = aftMerge[(vec.size() + 1) % 2];

        /* Division into SSTs */
        vector<SST<K, V> > ret;
        uint32_t curSize = 8;
        for (auto i = final.begin(); i != final.end(); curSize = 8) {
            auto start = i;
            #ifdef STRING
            while(i != final.end() && curSize < MEM_MAX_BYTES) { curSize += sizeof(K) + 8 + i->value.size(); ++i; }
            #endif
            vector<Entry<K, V> > tmp; tmp.assign(start, i);
            ret.push_back(SST<K, V>(tmp, curSize));
        }

        return ret;
    }

    /* Do Compaction */
    void compact(SST<K, V> &sst) {
        vector<SST<K, V> > merge;
        vector<Indices<K> > *curL = indices.rLevel(0), *nextL;
        uint32_t nNextL;

        merge.push_back(sst);
        for (auto i = curL->rbegin(); i != curL->rend(); ++i) {
            string curFilename = GENERATE_FILENAME(Dir, 0, curL->size() - 1 - (i - curL->rbegin()));
            SST<K, V> tmp = readSST(curFilename);
            merge.push_back(tmp);
            std::filesystem::remove(path(curFilename));
        }
        curL->clear();
        merge = mergeSort(merge);
        uint32_t bmin = merge.front().getLowBound();
        uint32_t bmax = merge.back().getHighBound();

        /* No Level 1 */
        if (indices.getHeight() < 2) { 
            indices.addNewLevel();
            nextL = indices.rLevel(nNextL = 1);
            for (auto i = merge.begin(); i != merge.end(); ++i) {
                nextL->push_back(Indices<K>(i->toIndexBin(), i->getSize(), i->getDataSegBias()));
                ofstream out(GENERATE_FILENAME(Dir, nNextL, i - merge.begin()));
                out.write(i->toBin().bin, i->toBin().length);
                out.close();
            }
            return;
        }
        
        
        nextL = indices.rLevel(nNextL = 1);
        while (true) {
            findIntersectSST(merge, *nextL, bmin, bmax, nNextL);
            uint32_t remAvail = MAX_SST_NUM(nNextL) - nextL->size();
            int toNextL = merge.size() - remAvail;

            /* If Space Remained, Fill Them */
            if (remAvail) {
                int n = merge.size() < remAvail ? merge.size() : remAvail; 
                for (int i = 0; i < n; ++i) {
                    auto ite = merge.rbegin();
                    ofstream out(GENERATE_FILENAME(Dir, nNextL, nextL->size()));
                    out.write(ite->toBin().bin, ite->toBin().length);
                    out.close();
                    nextL->push_back(Indices<K>(ite->toIndexBin(), ite->getSize(), ite->getDataSegBias()));
                    merge.pop_back();
                }
            }

            /* If SSTs Overflow the Level */
            if (toNextL > 0) {
                /* Next Level Exists */
                if (indices.getHeight() > nNextL + 1) {
                    nextL = indices.rLevel(++nNextL);
                    bmin =  merge.front().getLowBound();
                    bmax = merge.back().getHighBound();
                }
                /* Does Not Exist */
                else {
                    indices.addNewLevel();
                    nextL = indices.rLevel(++nNextL);
                    for (auto i = merge.rbegin(); i - merge.rbegin() < toNextL; ++i) {
                        nextL->push_back(Indices<K>(i->toIndexBin(), i->getSize(), i->getDataSegBias()));
                        ofstream out(GENERATE_FILENAME(Dir, nNextL, i - merge.rbegin()));
                        out.write(i->toBin().bin, i->toBin().length);
                        out.close();
                    }
                    break;
                }
            }
            else { break; }
        }
    }

    /* If Compact, Return false; If Not, Return True. */
    bool dump(SST<K, V> &sst) {
        Bin b = sst.toIndexBin();
        string filename;
        bool doNotCompact = indices.insert(Indices<K>(b, sst.getSize(), sst.getDataSegBias()), filename);
        if (doNotCompact) {
            ofstream out(filename);  assert(out);
            b = sst.toBin();
            out.write(b.bin, b.length);
            out.close();
            return true;
        }
        else { compact(sst); return false; }
    }

    bool getFromDisk(const K &key, V *value = nullptr) {
        string filename;
        uint32_t dataSegBias, bias, length;
        bool found = indices.find(key, &filename, &dataSegBias, &bias, &length);
        if (found) {
            ifstream in(filename);
            in.seekg(dataSegBias + bias);
            char valueBuff[length];
            in.read(valueBuff, length);
            in.close();
            #ifdef STRING
            *value = V(valueBuff, length);
            #endif
            return true;
        }
        else { return false; }
    }
public:
    explicit LSM(const string &dir): Dir(dir), indices(dir) { 
        path _dir(dir);
        if (!exists(_dir)) { assert(create_directory(_dir)); }
    }

    ~LSM() {
        if (memTab.size()) {
            SST<K, V> sst(memTab.data(), memTab.dataSize());
            dump(sst);
        }
    }

    /* If Compact, Return false; If Not, Return True. */
    bool put(const K &key, const V &val) {
        if (memTab.put(key, val) + 8 + memTab.size() * (sizeof(K) + 8) >= MEM_MAX_BYTES) {
            SST<K, V> sst(memTab.data(), memTab.dataSize());
            if (!dump(sst)) { return false; }
            memTab.reset();
        }

        return true;
    }

    V get(const K &key) {
        V *memGet = memTab.get(key);
        if (memGet) { return *memGet; }

        V diskGet;
        if (getFromDisk(key, &diskGet)) { return diskGet; }

        return V();
    }

    void reset() {
        memTab.reset();
        indices.clear();
        path p(Dir);
        remove_all(p);
        assert(create_directory(p));
    }

    bool remove(const K &key) {
        V *memGet = memTab.get(key);
        if (memGet) {
            #ifdef STRING
            if ((*memGet).empty()) { return false; }
            else {
                memTab.put(key, "");
            }
            #endif
        }
        else {
            string filename;
            uint32_t dataSegBias, bias, length;
            bool found = indices.find(key, &filename, &dataSegBias, &bias, &length);
            if (found) {
                memTab.put(key, "");
            }
            else {
                return false;
            }
        }
        return true;
    }
};