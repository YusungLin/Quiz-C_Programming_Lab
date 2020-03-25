#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* vector with small buffer optimization */

#define STRUCT_BODY(type)                                                  \
    struct {                                                               \
        size_t size : 54, on_heap : 1, capacity : 6, flag1 : 1, flag2 : 1, \
            flag3 : 1;                                                     \
        type *ptr;                                                         \
    }

// VV1: (s & (s - 1)) == 0
#define NEXT_POWER_OF_2(s) \
    (s & (s - 1)) == 0     \
        ? s                \
        : (size_t) 1 << (64 - __builtin_clzl(s)) /* next power of 2 */

#define v(t, s, name, ...)                                              \
    union {                                                             \
        STRUCT_BODY(t);                                                 \
        struct {                                                        \
            size_t filler;                                              \
            t buf[NEXT_POWER_OF_2(s)];                                  \
        };                                                              \
    } name __attribute__((cleanup(vec_free))) = {.buf = {__VA_ARGS__}}; \
    name.size = sizeof((__typeof__(name.buf[0])[]){0, __VA_ARGS__}) /   \
                    sizeof(name.buf[0]) -                               \
                1;                                                      \
    name.capacity = sizeof(name.buf) / sizeof(name.buf[0])

#define vec_size(v) v.size
#define vec_capacity(v) \
    (v.on_heap ? (size_t) 1 << v.capacity : sizeof(v.buf) / sizeof(v.buf[0]))

#define vec_data(v) (v.on_heap ? v.ptr : v.buf) /* always contiguous buffer */

#define vec_elemsize(v) sizeof(v.buf[0])
#define vec_pos(v, n) vec_data(v)[n] /* lvalue */

#define vec_reserve(v, n) __vec_reserve(&v, n, vec_elemsize(v), vec_capacity(v))
#define vec_push_back(v, e)                                            \
    __vec_push_back(&v, &(__typeof__(v.buf[0])[]){e}, vec_elemsize(v), \
                    vec_capacity(v))

#define vec_insert(v, e, pos)                                          \
    __vec_insert(&v, &(__typeof__(v.buf[0])[]){e}, vec_elemsize(v),    \
                 vec_capacity(v), pos)

// VV2: v.size -= 1
#define vec_pop_back(v) (void) (v.size -= 1)
#define vec_empty(v) (void) (v.size = 0)

/* This function attribute specifies function parameters that are not supposed
 * to be null pointers. This enables the compiler to generate a warning on
 * encountering such a parameter.
 */
#define NON_NULL __attribute__((nonnull))

static NON_NULL void vec_free(void *p)
{
    STRUCT_BODY(void) *s = p;
    if (s->on_heap)
        free(s->ptr);
}

static inline int ilog2(size_t n)
{
    return 64 - __builtin_clzl(n) - ((n & (n - 1)) == 0);
}

static NON_NULL void __vec_reserve(void *vec,
                                   size_t n,
                                   size_t elemsize,
                                   size_t capacity)
{
    union {
        STRUCT_BODY(void);
        struct {
            size_t filler;
            char buf[];
        };
    } *v = vec;

    if (n > capacity) {
        if (v->on_heap) {
            v->ptr = realloc(v->ptr,
                             elemsize * (size_t) 1 << (v->capacity = ilog2(n)));
        } else {
            void *tmp =
                malloc(elemsize * (size_t) 1 << (v->capacity = ilog2(n)));
            memcpy(tmp, v->buf, elemsize * v->size);
            v->ptr = tmp;
            v->on_heap = 1;
        }
    }
}

static NON_NULL void __vec_push_back(void *restrict vec,
                                     void *restrict e,
                                     size_t elemsize,
                                     size_t capacity)
{
    union {
        STRUCT_BODY(char);
        struct {
            size_t filler;
            char buf[];
        };
    } *v = vec;

    if (v->on_heap) {
        if (v->size == capacity)
            v->ptr = realloc(v->ptr, elemsize * (size_t) 1 << ++v->capacity);
        memcpy(&v->ptr[v->size++ * elemsize], e, elemsize);
    } else {
        if (v->size == capacity) {
            void *tmp =
                malloc(elemsize * (size_t) 1 << (v->capacity = capacity + 1));
            memcpy(tmp, v->buf, elemsize * v->size);
            v->ptr = tmp;
            v->on_heap = 1;
            memcpy(&v->ptr[v->size++ * elemsize], e, elemsize);
        } else
            memcpy(&v->buf[v->size++ * elemsize], e, elemsize);
    }
}

static NON_NULL void __vec_insert(void *restrict vec, void *restrict e, size_t elemsize, size_t capacity, int pos) {
    union {
        STRUCT_BODY(char);
        struct {
            size_t filler;
            char buf[];
        };
    } *v = vec;

    if (pos < 0)
        return;

    size_t offset_size = elemsize * (v->size - pos);
    if (v->on_heap) {
        if (v->size == capacity)
            v->ptr = realloc(v->ptr, elemsize * (size_t) 1 << ++v->capacity);
        char *offset_data[offset_size];
        memcpy(offset_data, &v->ptr[pos * elemsize], offset_size);
        memcpy(&v->ptr[(pos + 1) * elemsize], offset_data, offset_size);
        memcpy(&v->ptr[pos * elemsize], e, elemsize);
    } else {
        if (v->size == capacity) {
            void *tmp =
                malloc(elemsize * (size_t) 1 << (v->capacity = capacity + 1));
            memcpy(tmp, v->buf, elemsize * v->size);
            v->ptr = tmp;
            v->on_heap = 1;

            char *offset_data[offset_size];
            memcpy(offset_data, &v->ptr[pos * elemsize], offset_size);
            memcpy(&v->ptr[(pos + 1) * elemsize], offset_data, offset_size);
            memcpy(&v->ptr[pos * elemsize], e, elemsize);
        } else {
            char *offset_data[offset_size];
            memcpy(offset_data, &v->ptr[pos * elemsize], offset_size);
            memcpy(&v->ptr[(pos + 1) * elemsize], offset_data, offset_size);
            memcpy(&v->ptr[pos * elemsize], e, elemsize);
        }
    }
    v->size++;
}

static NON_NULL void vec_erase(void *restrict vec, size_t elemsize, int pos) {
    union {
        STRUCT_BODY(char);
        struct {
            size_t filler;
            char buf[];
        };
    } *v = vec;

    if (v->size <= 0 || pos < 0 || pos >= v->size)
        return;

    memcpy(&v->ptr[pos * elemsize], &v->ptr[(pos + 1) * elemsize], elemsize * (v->size-- - (pos + 1)));
}

// testing code
int main()
{
    v(int, 2, vect, 1, 6);

#define display(v)                               \
    do {                                         \
        for (size_t i = 0; i < vec_size(v); i++) \
            printf("%d ", vec_pos(v, i));      \
        puts(v.on_heap ? "heap" : "stack");      \
    } while (0)

    display(vect);
    vec_insert(vect, 5, 1);
    display(vect);
    vec_insert(vect, 4, 1);
    display(vect);
    vec_insert(vect, 3, 1);
    display(vect);
    vec_insert(vect, 2, 1);
    display(vect);
    vec_erase(&vect, sizeof(int), 0);
    display(vect);
    vec_erase(&vect, sizeof(int), 1);
    display(vect);
    vec_erase(&vect, sizeof(int), 2);
    display(vect);
    vec_empty(vect);
    display(vect);

    return 0;
}
