#include "threading.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Optional: use these functions to add debug or error prints to your application
// #define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* const thread_param)
{
    DEBUG_LOG("thread started");
    struct thread_data* const data = (struct thread_data*) thread_param;
    if (data == NULL)
    {
        ERROR_LOG("thread_param is NULL");
        return thread_param;
    }

    usleep(data->wait_to_obtain_ms * 1000);

    DEBUG_LOG("thread locking mutex");
    int res = pthread_mutex_lock(data->mutex);
    if (res != 0)
    {
        ERROR_LOG("pthread_mutex_lock failed with error %d", res);
        return thread_param;
    }

    usleep(data->wait_to_release_ms * 1000);

    DEBUG_LOG("thread unlocking mutex");
    res = pthread_mutex_unlock(data->mutex);
    if (res != 0)
    {
        ERROR_LOG("pthread_mutex_unlock failed with error %d", res);
        return thread_param;
    }

    data->thread_complete_success = true;

    DEBUG_LOG("thread completed successfully");
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t* const thread,
                                  pthread_mutex_t* const mutex,
                                  const int wait_to_obtain_ms,
                                  const int wait_to_release_ms)
{
    DEBUG_LOG("start_thread_obtaining_mutex started");

    struct thread_data* const data = calloc(1, sizeof(struct thread_data));
    if (data == NULL)
    {
        ERROR_LOG("malloc failed");
        return false;
    }

    data->mutex = mutex;
    data->thread_complete_success = false;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    
    const int res = pthread_create(thread, NULL, threadfunc, data);
    if (res != 0)
    {
        ERROR_LOG("pthread_create failed with error %d", res);
        free(data);
        return false;
    }

    return true;
}
