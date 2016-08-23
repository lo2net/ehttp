#ifndef _H_THREADPOOL
#define _H_THREADPOOL

//#include <pthread.h>
#include <windows.h> // CRITICAL_SECTION

#include <deque>
#include <vector>
//#include <sys/epoll.h>

const int STARTED = 0;
const int STOPPED = 1;

class Mutex
{
    public:
        Mutex()
        {
            //pthread_mutex_init(&m_lock, NULL);
            ::InitializeCriticalSection(&m_lock);

            is_locked = false;
        }
        ~Mutex()
        {
            while(is_locked);
            unlock(); // Unlock Mutex after shared resource is safe
            //pthread_mutex_destroy(&m_lock);
            ::DeleteCriticalSection(&m_lock);

        }
        void lock()
        {
            //pthread_mutex_lock(&m_lock);
            ::EnterCriticalSection(&m_lock);

            is_locked = true;
        }
        void unlock()
        {
            is_locked = false; // do it BEFORE unlocking to avoid race condition
            //pthread_mutex_unlock(&m_lock);
            ::LeaveCriticalSection(&m_lock);
        }
    //pthread_mutex_t* get_mutex_ptr()
    CRITICAL_SECTION* get_mutex_ptr()
        {
            return &m_lock;
        }
    private:
    //pthread_mutex_t m_lock;
    CRITICAL_SECTION m_lock;
    
        volatile bool is_locked;
};

class CondVar
{
    public:
        CondVar() {
            //pthread_cond_init(&m_cond_var, NULL);
            ::InitializeConditionVariable(&m_cond_var);
        }
        ~CondVar() {
            //pthread_cond_destroy(&m_cond_var);
        }
     //void wait(pthread_mutex_t* mutex) {pthread_cond_wait(&m_cond_var, mutex); }
    void wait(CRITICAL_SECTION* mutex) {
        //pthread_cond_wait(&m_cond_var, mutex);
        ::SleepConditionVariableCS(&m_cond_var, mutex, INFINITE);
    }
    void signal() {
        //pthread_cond_signal(&m_cond_var);
        ::WakeConditionVariable(&m_cond_var);
    }
    void broadcast() {
        //pthread_cond_broadcast(&m_cond_var);
        ::WakeAllConditionVariable(&m_cond_var);
    }
private:
    //pthread_cond_t m_cond_var;
    CONDITION_VARIABLE m_cond_var;
};

//template<class TClass>
class Task
{
    public:
        //  Task(TCLass::* obj_fn_ptr); // pass an object method pointer
        Task(void (*fn_ptr)(void*), void* arg); // pass a free function pointer
        virtual ~Task();
        virtual void run();
private:
        //  TClass* _obj_fn_ptr;
        void (*m_fn_ptr)(void*);
        void* m_arg;

    //    int epollfd;
    //epoll_event event;
    //    void *watcher;
};

typedef void (*thread_start_callback)();

class ThreadPool
{
    public:
        ThreadPool();
        ~ThreadPool();
        int start();
        int destroy_threadpool();
        void* execute_thread();
        int add_task(Task *task);
        void set_thread_start_cb(thread_start_callback f);
        void set_task_size_limit(int size);
        void set_pool_size(int pool_size);
        thread_start_callback m_scb;
    private:
        int m_pool_size;
        Mutex m_task_mutex;
        CondVar m_task_cond_var;
    //std::vector<pthread_t> m_threads; // storage for threads
    std::vector<HANDLE> m_threads;
        std::deque<Task*> m_tasks;
        volatile int m_pool_state;
        int m_task_size_limit;
};

#endif /* _H_THREADPOOL */
