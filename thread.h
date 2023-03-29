#ifndef __THREAD_H__
#define __THREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Define to prevent recursive inclusion
-------------------------------------*/
#include <sys/types.h>
#include <pthread.h>


#define OPER_OK 0


typedef    struct _mutex
{
    pthread_mutex_t        mutex;
}mutex_t;


typedef    struct _cond
{
    pthread_cond_t        cond;
}cond_t;


typedef    pthread_t        tid_t;
typedef    pthread_attr_t    attr_t;
typedef    void*    (* thread_fun_t)(void*);


typedef    struct _thread
{
    tid_t            tid;
    cond_t            *cv;
    int32_t            state;
    int32_t            stack_size;
    attr_t         attr;
    thread_fun_t    fun;
}thread_t;



/* mutex */
extern    int32_t        mutex_init(mutex_t    *m);
extern    int32_t        mutex_destroy(mutex_t    *m);
extern    int32_t        mutex_lock(mutex_t    *m);
extern    int32_t        mutex_unlock(mutex_t    *m);


/* cond */
extern    int32_t        cond_init(cond_t    *c);
extern    int32_t        cond_destroy(cond_t    *c);
extern    int32_t        cond_signal(cond_t *c);
extern    int32_t        cond_wait(cond_t    *c,mutex_t *m);



/* thread */
/* 线程的创建，其属性的设置等都封装在里面 */
extern    int32_t        thread_create(thread_t *t);
//extern    int32_t        thread_init(thread_t    *t);

#define    thread_join(t, p)     pthread_join(t, p)
#define    thread_self()        pthread_self()
#define    thread_sigmask        pthread_sigmask


#ifdef __cplusplus
}
#endif

#endif
/* END OF FILE
---------------------------------------------------------------*/

