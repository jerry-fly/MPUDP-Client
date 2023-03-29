#include "thread.h"




/* mutex */
int32_t        mutex_init(mutex_t    *m)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_mutex_init(&m->mutex, NULL)) != 0)
        ret = -1;

    return ret;
}


int32_t        mutex_destroy(mutex_t    *m)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_mutex_destroy(&m->mutex)) != 0)
        ret = -1;

    return ret;
}



int32_t        mutex_lock(mutex_t    *m)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_mutex_lock(&m->mutex)) != 0)
        ret = -1;

    return ret;
}



int32_t        mutex_unlock(mutex_t    *m)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_mutex_unlock(&m->mutex)) != 0)
        ret = -1;
    
    return ret;
}






/* cond */
int32_t        cond_init(cond_t    *c)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_cond_init(&c->cond, NULL)) != 0)
        ret = -1;

    return ret;
}



int32_t        cond_destroy(cond_t    *c)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_cond_destroy(&c->cond)) != 0)
        ret = -1;
    
    return ret;
}



int32_t        cond_signal(cond_t *c)
{
    int32_t        ret = OPER_OK;


    if((ret = pthread_cond_signal(&c->cond)) != 0)
        ret = -1;
    
    return ret;
}




int32_t        cond_wait(cond_t    *c,mutex_t *m)
{
    int32_t        ret = OPER_OK;

    if((ret = pthread_cond_wait(&c->cond, &m->mutex)) != 0)
        ret = -1;    
    
    return ret;
}

