#ifndef __CBUF_H__
#define __CBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Define to prevent recursive inclusion
-------------------------------------*/
#include <sys/types.h>
#include <stdbool.h>
#include "thread.h"

#define CBUF_MAX (1024*64)


typedef    struct _cbuf
{
    int32_t        size;            /* 当前缓冲区中存放的数据的个数 */
    int32_t        next_in;        /* 缓冲区中下一个保存数据的位置 */
    int32_t        next_out;        /* 从缓冲区中取出下一个数据的位置 */
    int32_t        capacity;        /* 这个缓冲区的可保存的数据的总个数 */
    mutex_t        mutex;            /* Lock the structure */
    cond_t        not_full;        /* Full -> not full condition */
    cond_t        not_empty;        /* Empty -> not empty condition */
    void        *data[CBUF_MAX];/* 缓冲区中保存的数据指针 */
}cbuf_t;


/* 初始化环形缓冲区 */
extern    int32_t        cbuf_init(cbuf_t *c);

/* 销毁环形缓冲区 */
extern    void        cbuf_destroy(cbuf_t    *c);

/* 压入数据 */
extern    int32_t        cbuf_enqueue(cbuf_t *c,void *data);

/* 取出数据 */
extern    void*        cbuf_dequeue(cbuf_t *c);


/* 判断缓冲区是否为满 */
extern    bool        cbuf_full(cbuf_t    *c);

/* 判断缓冲区是否为空 */
extern    bool        cbuf_empty(cbuf_t *c);

/* 获取缓冲区可存放的元素的总个数 */
extern    int32_t        cbuf_capacity(cbuf_t *c);


#ifdef __cplusplus
}
#endif

#endif
/* END OF FILE
---------------------------------------------------------------*/

