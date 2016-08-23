#include "threadpool.h"
//#include <sys/epoll.h>
//#include <sys/fcntl.h>
//#include <sys/sysinfo.h>
//#include <unistd.h>

#include <errno.h>
#include <string.h>
#include "simple_log.h"
//#include "epoll_socket.h"

Task::Task(void (*fn_ptr)(void*), void* arg) : m_fn_ptr(fn_ptr), m_arg(arg)
{}

Task::~Task() {}

void Task::run() {
    (*m_fn_ptr)(m_arg);
    /*
    LOG_DEBUG("start handle read task");
    EpollContext *epoll_context = (EpollContext *) event.data.ptr;
    int fd = epoll_context->fd;

    EpollSocketWatcher *es = (EpollSocketWatcher *) watcher;
    int ret = es->on_readable(epollfd, event);
    if (ret == READ_CLOSE) {
        close_and_release(epollfd, event, *es);
        return;
    }
    if (ret == READ_CONTINUE) {
        event.events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    } else if (ret == READ_OVER) { // READ_OVER
        event.events = EPOLLOUT | EPOLLONESHOT;
        epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    } else {
        LOG_ERROR("unkonw ret!");
    }
    */
}

ThreadPool::ThreadPool() {
    m_scb = NULL;
    m_task_size_limit = -1;
    m_pool_size = 0;
    m_pool_state = -1;
}

ThreadPool::~ThreadPool()
{
    // Release resources
    if (m_pool_state != STOPPED) {
        destroy_threadpool();
    }
}

// We can't pass a member function to pthread_create.
// So created the wrapper function that calls the member function
// we want to run in the thread.
    extern "C"
void* ss_start_thread(void* arg)
{
    ThreadPool* tp = (ThreadPool *)arg;
    if (tp->m_scb != NULL) {
        tp->m_scb();
    } else {
        LOG_DEBUG("thread start cb is null");
    }
    tp->execute_thread();
    return NULL;
}

int ThreadPool::start()
{
    if (m_pool_size == 0) {
        LOG_ERROR("pool size must be set!");
        return -1;
    }
    if (m_pool_state == STARTED) {
        return 0;
    }
    m_pool_state = STARTED;
    //int ret = -1;
    for (int i = 0; i < m_pool_size; i++) {
        //pthread_t tid;
        DWORD tid;
        //ret = pthread_create(&tid, NULL, start_thread, (void*) this);
        HANDLE ret = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ss_start_thread, (void*)this, 0, &tid);
        //if (ret != 0) {
        if (ret == NULL) {
            //LOG_ERROR("pthread_create() failed: %d", ret);
            LOG_ERROR("pthread_create() failed: %d", ::GetLastError());
            return -1;
        }
        //m_threads.push_back(tid);
        m_threads.push_back(ret);
    }
    LOG_DEBUG("%d threads created by the thread pool", m_pool_size);

    return 0;
}

void ThreadPool::set_thread_start_cb(thread_start_callback f) {
    m_scb = f;
}

void ThreadPool::set_task_size_limit(int size) {
    m_task_size_limit = size;
}

void ThreadPool::set_pool_size(int pool_size) {
    m_pool_size = pool_size;
}

int ThreadPool::destroy_threadpool()
{
    // Note: this is not for synchronization, its for thread communication!
    // destroy_threadpool() will only be called from the main thread, yet
    // the modified m_pool_state may not show up to other threads until its 
    // modified in a lock!
    m_task_mutex.lock();
    m_pool_state = STOPPED;
    m_task_mutex.unlock();
    LOG_INFO("Broadcasting STOP signal to all threads...");
    m_task_cond_var.broadcast(); // notify all threads we are shttung down

    //int ret = -1;
    for (int i = 0; i < m_pool_size; i++) {
        void* result;
        //ret = pthread_join(m_threads[i], &result);
        DWORD ret = ::WaitForSingleObject(m_threads[i], INFINITE);
        // ret => WAIT_OBJECT_0
        LOG_DEBUG("pthread_join() returned %d", ret);
        m_task_cond_var.broadcast(); // try waking up a bunch of threads that are still waiting
    }
    LOG_INFO("%d threads exited from the thread pool", m_pool_size);
    return 0;
}

void* ThreadPool::execute_thread()
{
    Task *task = NULL;
    LOG_DEBUG("Starting thread :%u", ::GetCurrentThreadId());
    while(true) {
        // Try to pick a task
        LOG_DEBUG("Locking: %u", ::GetCurrentThreadId());
        m_task_mutex.lock();

        // We need to put pthread_cond_wait in a loop for two reasons:
        // 1. There can be spurious wakeups (due to signal/ENITR)
        // 2. When mutex is released for waiting, another thread can be waken up
        //    from a signal/broadcast and that thread can mess up the condition.
        //    So when the current thread wakes up the condition may no longer be
        //    actually true!
        while ((m_pool_state != STOPPED) && (m_tasks.empty())) {
            // Wait until there is a task in the queue
            // Unlock mutex while wait, then lock it back when signaled
            LOG_DEBUG("Unlocking and waiting: %u", ::GetCurrentThreadId());
            m_task_cond_var.wait(m_task_mutex.get_mutex_ptr());
            LOG_DEBUG("Signaled and locking: %u", ::GetCurrentThreadId());
        }

        // If the thread was woken up to notify process shutdown, return from here
        if (m_pool_state == STOPPED) {
            LOG_DEBUG("Unlocking and exiting: %u", ::GetCurrentThreadId());
            m_task_mutex.unlock();
            //pthread_exit(NULL);
            ::ExitThread(0);
        }

        task = m_tasks.front();
        m_tasks.pop_front();
        LOG_DEBUG("Unlocking: %u", ::GetCurrentThreadId());
        m_task_mutex.unlock();

        //cout << "Executing thread " << ::GetCurrentThreadId() << endl;
        // execute the task
        task->run(); //
        //cout << "Done executing thread " << ::GetCurrentThreadId() << endl;
        delete task;
        task = NULL;
    }
    return NULL;
}

int ThreadPool::add_task(Task *task)
{
    m_task_mutex.lock();

    if (m_task_size_limit > 0 && (int) m_tasks.size() > m_task_size_limit) {
        LOG_WARN("task size reach limit:%d", m_task_size_limit);
        return -1;
    }
    m_tasks.push_back(task);

    m_task_cond_var.signal(); // wake up one thread that is waiting for a task to be available

    m_task_mutex.unlock();

    return 0;
}
