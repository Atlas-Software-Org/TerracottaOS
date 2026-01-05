#include <mem/mem.hpp>
#include <cstddef>
#include <cstdio>

extern "C" {

void* memset(void* dest, int value, size_t count) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    for (size_t i = 0; i < count; i++) {
        d[i] = static_cast<unsigned char>(value);
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t count) {
    const unsigned char* s = static_cast<const unsigned char*>(src);
    unsigned char* d = static_cast<unsigned char*>(dest);
    if (src == nullptr) return nullptr;
    if (dest == nullptr) return nullptr;
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t count) {
    const unsigned char* s = static_cast<const unsigned char*>(src);
    unsigned char* d = static_cast<unsigned char*>(dest);

    if (d == s || count == 0)
        return dest;

    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

int memcmp(const void* ptr1, const void* ptr2, size_t count) {
    const unsigned char* a = static_cast<const unsigned char*>(ptr1);
    const unsigned char* b = static_cast<const unsigned char*>(ptr2);
    if (!a || !b) return -1;
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i])
            return (a[i] < b[i]) ? -1 : 1;
    }
    return 0;
}

}

namespace mem {

void* memset(void* dest, int value, size_t count) {
    return ::memset(dest, value, count);
}

void* memcpy(void* dest, const void* src, size_t count) {
    return ::memcpy(dest, src, count);
}

void* memmove(void* dest, const void* src, size_t count) {
    return ::memmove(dest, src, count);
}

int memcmp(const void* ptr1, const void* ptr2, size_t count) {
    return ::memcmp(ptr1, ptr2, count);
}

}

#include <panic.hpp>

void* operator new(size_t size) {
    void* ptr = mem::heap::malloc(size);
    if (!ptr) panic((char*)"K_OUT_OF_MEM");
    return ptr;
}

void operator delete(void* ptr) {
    mem::heap::free(ptr);
}

void* operator new[](size_t size) {
    void* ptr = mem::heap::malloc(size);
    if (!ptr) panic((char*)"K_OUT_OF_MEM");
    return ptr;
}

void operator delete[](void* ptr) {
    mem::heap::free(ptr);
}

void operator delete(void* ptr, uint64_t size) {
    (void)size;
    mem::heap::free(ptr);
}

void operator delete [](void* ptr, uint64_t size) {
    (void)size;
    mem::heap::free(ptr);
}
