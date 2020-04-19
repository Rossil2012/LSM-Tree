#pragma once

#include <list>
#include <string>
#include <vector>

#define STRING

using namespace std;

template<class K, class V>
struct Entry {
    Entry() {}
    Entry(const K &k, const V &v): key(k), value(v) {}
    K key;
    V value;
};

template <class T>
struct QuadListNode {
    T entry;
    QuadListNode<T> *pred;
    QuadListNode<T> *succ;
    QuadListNode<T> *above;
    QuadListNode<T> *below;
    
    QuadListNode(T e = T(), QuadListNode<T> *p = nullptr, QuadListNode<T> *s = nullptr, QuadListNode<T> *a = nullptr, QuadListNode<T> *b = nullptr)
        : entry(e), pred(p), succ(s), above(a), below(b) {}
    QuadListNode<T> *insertAsSuccAbove(const T &e, QuadListNode<T> *b = nullptr) {
        QuadListNode<T> *x = new QuadListNode<T>(e, this, succ, nullptr, b);
        succ->pred = x; succ = x;
        if (b) { b->above = x; }
        return x;
    }
};

template <class T>
class QuadList {
private:
    int _size;
    QuadListNode<T> *header;
    QuadListNode<T> *trailer;

protected:
    void init() {
        header = new QuadListNode<T>();
        trailer = new QuadListNode<T>();
        header->succ = trailer; header->pred = nullptr;
        trailer->succ = nullptr; trailer->pred = header;
        header->above = trailer->above = nullptr;
        header->below = trailer->below = nullptr;
        _size = 0;
    }

    int clear() {
        int oldSize = _size;
        while (0 < _size) { remove(header->succ); }
        return oldSize;
    }

public:
    QuadList() { init(); }
    ~QuadList() { clear(); delete header; delete trailer; }

    int size() const { return _size; }
    bool empty() const { return _size <= 0; }
    QuadListNode<T> *first() const { return header->succ; }
    QuadListNode<T> *last() const { return trailer->pred; }
    T remove(QuadListNode<T> *p) {
        p->pred->succ = p->succ; p->succ->pred = p->pred; --_size;
        T e = p->entry; delete p;
        return e;
    }
    QuadListNode<T> *insertAfterAbove(const T &e, QuadListNode<T> *p, QuadListNode<T> *b = nullptr) {
        ++_size;
        return p->insertAsSuccAbove(e, b);
    }

};


template <class K, class V>
class SkipList {
private:
    list<QuadList<Entry<K, V> > *> levels;
    uint32_t dataBytes;
    
protected:
    QuadListNode<Entry<K, V> > *skipSearch(const K &k, typename list<QuadList<Entry<K, V> > *>::iterator &q, QuadListNode<Entry<K, V> > *&p) {
        while (true) {
            while (p->succ && p->entry.key <= k) { p = p->succ; }
            p = p->pred;

            if (p->pred && k == p->entry.key) { return p; }
            
            ++q;
            if (q == levels.end()) { --q; return nullptr; }
            p = p->pred ? p->below : (*q)->first();
        }
    }

public:
    explicit SkipList(): dataBytes(0) { srand(time(0)); }
    ~SkipList() { for (auto i = levels.begin(); i != levels.end(); ++i) { delete *i; } }
    int size() { return levels.empty() ? 0 : levels.back()->size(); }
    int dataSize() { return dataBytes; }
    uint32_t put(const K &key, const V &val) {
        Entry<K, V> e = Entry<K, V>(key, val);
        if (levels.empty()) { levels.push_front(new QuadList<Entry<K, V> >()); }

        auto q = levels.begin();
        QuadListNode<Entry<K, V> > *p = (*q)->first();

        QuadListNode<Entry<K, V> > *exist = skipSearch(key, q, p);
        if (exist) { 
            /* String Limited */
            #ifdef STRING
            if (typeid(V) == typeid(string)) { dataBytes += val.size() - exist->entry.value.size(); }
            #endif
            while (p) {
                p->entry = e;
                p = p->below;
            } 
            return dataBytes;
        }
        q = levels.end();
        --q;

        QuadListNode<Entry<K, V> > *b = (*q)->insertAfterAbove(e, p);

        while (rand() & 1) {
            while (p->pred && !p->above) { p = p->pred; }

            if (p->pred) {
                p = p->above;
                --q;
            }
            else {
                if (q == levels.begin()) { levels.push_front(new QuadList<Entry<K, V> >()); }

                p = (*(--q))->first()->pred;
            }

            b = (*q)->insertAfterAbove(e, p, b);
        }

        /* String Limited */
        #ifdef STRING
        if (typeid(V) == typeid(string)) { dataBytes += val.size(); }
        #endif

        return dataBytes;
    }

    V *get(const K &key) {
        if (levels.empty()) { return nullptr; }
        auto q = levels.begin();
        QuadListNode<Entry<K, V> > *p = (*q)->first();
        QuadListNode<Entry<K, V> > *res = skipSearch(key, q, p);
        return res ? &(res->entry.value) : nullptr;
    }

    bool remove(const K &key) {
        if (levels.empty()) { return false; }

        auto q = levels.begin();
        QuadListNode<Entry<K, V> > *p = (*q)->first();

        if (!skipSearch(key, q, p)) { return false; }

        do {
            QuadListNode<Entry<K, V> > *lower = p->below;
            (*q)->remove(p);
            p = lower; ++q;
        } while(q != levels.end());

        while (!levels.empty() && levels.front()->empty()) { levels.erase(levels.begin()); }

        return true;
    }

    void reset() {
        for (auto i = levels.begin(); i != levels.end(); ++i) { delete *i; }
        levels.clear();
        srand(time(0));
        dataBytes = 0;
    }

    vector<Entry<K, V> > data() {
        vector<Entry<K, V> > ret;
        QuadListNode<Entry<K, V> > *ite = levels.back()->first();
        while (ite->succ) {
            ret.push_back(ite->entry);
            ite = ite->succ;
        }
        return ret;
    }
};