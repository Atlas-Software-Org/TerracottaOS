#ifndef TC_STRING_H
#define TC_STRING_H 1

#define STRING_H 1

#include <mem/mem.hpp>
#include <stddef.h>

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static inline char *strcpy(char *__restrict dst, const char *__restrict src) {
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static inline char *strncpy(char *__restrict dst, const char *__restrict src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

static inline char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

static inline char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

static inline size_t strnlen(const char *s, size_t max) {
    size_t len = 0;
    while (len < max && s[len]) len++;
    return len;
}

static inline char* strdup(char* src) {
    char* new_string = (char*)mem::heap::malloc(strlen(src));
    strcpy(new_string, src);
    return new_string;
}

#endif
