#pragma once

#include <stddef.h>
#include <string.h>

class bbk_string {
public:
    static const size_t capacity = 384;

    bbk_string() { data_[0] = 0; }
    bbk_string(const char *text) { assign(text); }
    bbk_string(const bbk_string &other) { assign(other.data_); }

    bbk_string &operator=(const bbk_string &other) {
        if (this != &other) assign(other.data_);
        return *this;
    }
    bbk_string &operator=(const char *text) {
        assign(text);
        return *this;
    }
    bbk_string &operator+=(const char *suffix) {
        size_t used = length();
        if (!suffix || used >= capacity - 1) return *this;
        size_t room = capacity - 1 - used;
        strncat(data_, suffix, room);
        data_[capacity - 1] = 0;
        return *this;
    }
    bbk_string &operator+=(const bbk_string &suffix) {
        return *this += suffix.c_str();
    }

    const char *c_str() const { return data_; }
    char *data() { return data_; }
    bool empty() const { return data_[0] == 0; }
    size_t length() const { return strlen(data_); }
    size_t size() const { return length(); }
    void clear() { data_[0] = 0; }

private:
    char data_[capacity];

    void assign(const char *text) {
        if (!text) {
            data_[0] = 0;
            return;
        }
        strncpy(data_, text, capacity - 1);
        data_[capacity - 1] = 0;
    }
};

inline bool operator==(const bbk_string &left, const char *right) {
    return strcmp(left.c_str(), right ? right : "") == 0;
}

inline bool operator!=(const bbk_string &left, const char *right) {
    return !(left == right);
}

typedef bbk_string string;
