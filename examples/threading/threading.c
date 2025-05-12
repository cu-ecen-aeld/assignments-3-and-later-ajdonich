#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param) {
    struct timeval initial, final;
    gettimeofday(&initial, NULL);

    struct thread_data *param = (struct thread_data*)thread_param;
    param->thread_complete_success = false;
    
    syslog(LOG_DEBUG, "wait_to_obtain_us: %uus", param->wait_to_obtain_us);
    syslog(LOG_DEBUG, "wait_to_release_us: %uus", param->wait_to_release_us);
    syslog(LOG_DEBUG, "pmutex: %p", (void*)param->mutex);

    int err;
    if (usleep(param->wait_to_obtain_us) == -1)
        syslog(LOG_ERR, "ERROR in (obtain) usleep(%u): %m", param->wait_to_obtain_us);
    else if ((err = pthread_mutex_lock(param->mutex)) != 0)
        syslog(LOG_ERR, "ERROR in pthread_mutex_lock(3p): %s", strerror(err));
    else if (usleep(param->wait_to_release_us) == -1)
        syslog(LOG_ERR, "ERROR in (release) usleep(%u): %m", param->wait_to_release_us);
    else if ((err = pthread_mutex_unlock(param->mutex)) != 0)
        syslog(LOG_ERR, "ERROR in pthread_mutex_unlock(3p): %s", strerror(err));

    gettimeofday(&final, NULL);
    unsigned int elapsed = ((final.tv_sec * 1e3 + final.tv_usec * 1e-3) - 
        (initial.tv_sec * 1e3 + initial.tv_usec * 1e-3));

    syslog(LOG_DEBUG, "SUCCESS threadfunc executed in %ums", elapsed);
    param->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, 
    int wait_to_obtain_ms, int wait_to_release_ms) 
{
    openlog(NULL, LOG_PID, LOG_USER);
    struct thread_data *param = (struct thread_data*)malloc(sizeof(struct thread_data));
    if (param == NULL) {
        syslog(LOG_ERR, "ERROR in malloc(3): %m");
        return false;
    }

    param->thread_complete_success = false;
    param->wait_to_obtain_us = wait_to_obtain_ms * 1000;
    param->wait_to_release_us = wait_to_release_ms * 1000;
    param->mutex = mutex;

    if (pthread_create(thread, NULL, threadfunc, (void*)param) != 0) {
        syslog(LOG_ERR, "ERROR in pthread_create(3): %m");
        return false;
    }

    return true;
}

