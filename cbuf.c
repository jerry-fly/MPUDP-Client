#include "cbuf.h"



/* 初始化环形缓冲区 */
int32_t        cbuf_init(cbuf_t *c)
{
    int32_t    ret = OPER_OK;

    if((ret = mutex_init(&c->mutex)) != OPER_OK)    
    {
#ifdef DEBUG_CBUF
    debug("cbuf init fail ! mutex init fail !\n");
#endif
        return ret;
    }

    if((ret = cond_init(&c->not_full)) != OPER_OK)    
    {
#ifdef DEBUG_CBUF
    debug("cbuf init fail ! cond not full init fail !\n");
#endif
        mutex_destroy(&c->mutex);
        return ret;
    }

    if((ret = cond_init(&c->not_empty)) != OPER_OK)
    {
#ifdef DEBUG_CBUF
    debug("cbuf init fail ! cond not empty init fail !\n");
#endif
        cond_destroy(&c->not_full);
        mutex_destroy(&c->mutex);
        return ret;
    }

    c->size     = 0;
    c->next_in    = 0;
    c->next_out = 0;
    c->capacity    = CBUF_MAX;

#ifdef DEBUG_CBUF
    debug("cbuf init success !\n");
#endif

    return ret;
}


/* 销毁环形缓冲区 */
void        cbuf_destroy(cbuf_t    *c)
{
    cond_destroy(&c->not_empty);
    cond_destroy(&c->not_full);
    mutex_destroy(&c->mutex);

#ifdef DEBUG_CBUF
    debug("cbuf destroy success \n");
#endif
}



/* 压入数据 */
int32_t        cbuf_enqueue(cbuf_t *c,void *data)
{
    int32_t    ret = OPER_OK;

    if((ret = mutex_lock(&c->mutex)) != OPER_OK)    return ret;

    /*
     * Wait while the buffer is full.
     */
    while(cbuf_full(c))
    {
#ifdef DEBUG_CBUF
    debug("cbuf is full !!!\n");
#endif
        cond_wait(&c->not_full,&c->mutex);
    }

    c->data[c->next_in++] = data;
    c->size++;
    c->next_in %= c->capacity;

    mutex_unlock(&c->mutex);

    /*
     * Let a waiting consumer know there is data.
     */
    cond_signal(&c->not_empty);

#ifdef DEBUG_CBUF
//    debug("cbuf enqueue success ,data : %p\n",data);
    debug("enqueue\n");
#endif

    return ret;
}



/* 取出数据 */
void*        cbuf_dequeue(cbuf_t *c)
{
    void     *data     = NULL;
    int32_t    ret     = OPER_OK;

    if((ret = mutex_lock(&c->mutex)) != OPER_OK)    return NULL;

       /*
     * Wait while there is nothing in the buffer
     */
    while(cbuf_empty(c))
    {
#ifdef DEBUG_CBUF
    debug("cbuf is empty!!!\n");
#endif
        cond_wait(&c->not_empty,&c->mutex);
    }

    data = c->data[c->next_out++];
    c->size--;
    c->next_out %= c->capacity;

    mutex_unlock(&c->mutex);


    /*
     * Let a waiting producer know there is room.
     * 取出了一个元素，又有空间来保存接下来需要存储的元素
     */
    cond_signal(&c->not_full);

#ifdef DEBUG_CBUF
//    debug("cbuf dequeue success ,data : %p\n",data);
    debug("dequeue\n");
#endif

    return data;
}


/* 判断缓冲区是否为满 */
bool        cbuf_full(cbuf_t    *c)
{
    return (c->size == c->capacity);
}

/* 判断缓冲区是否为空 */
bool        cbuf_empty(cbuf_t *c)
{
    return (c->size == 0);
}

/* 获取缓冲区可存放的元素的总个数 */
int32_t        cbuf_capacity(cbuf_t *c)
{
    return c->capacity;
}

