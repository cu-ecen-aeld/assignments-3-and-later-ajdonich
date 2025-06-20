#ifndef CONNTHREAD_H
#define CONNTHREAD_H

#include "../aesd-char-driver/aesd_ioctl.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 
    Simple append-only buffer struct with automatic memory
    realloc (buffer length doubles) when size is exceeded.
    Null terminating byte is always automatically appended. 
    Call reset() to move append index to beginning of buffer.
    Call destroy() to free buffer after use.
*/
typedef struct {
    size_t index;
    size_t buffersz;
    char *data;
} LineBuffer;

LineBuffer newLineBuffer();
int append(LineBuffer *self, char ch);
void reset(LineBuffer *self);
void destroy(LineBuffer *self);

/* 
    Main ConnThread struct for TCP-connection-per-thread design.
    Maintains all data needed by thread and fcns to read/write 
    TCP connections and BACKEND and exit/done communication.
    Also maintains *next pointer for use in Singly Linked List.
*/
typedef struct ConnThread ConnThread;

struct ConnThread {
    int cfd, fd;
    const char *backend;
    unsigned int tid;
    pthread_t thread;
    struct sockaddr_storage claddr;

    sig_atomic_t _exitflag;
    sig_atomic_t _doneFlag;

    ConnThread *next;
};

ConnThread *newConnThread(const char *backend);
ssize_t readLine(ConnThread *self, LineBuffer *line);
ssize_t writeFile(ConnThread *self, LineBuffer *line);
ssize_t writeTimestamp(const char *backend);
ssize_t sendFile(ConnThread *self, int whence);
void *connThreadMain(void *vself);

int acquireBackend(ConnThread *self);
int releaseBackend(ConnThread *self);
int sendIoctl(ConnThread *self, struct aesd_seekto *pSeekObj);
int matchIoctl(ConnThread *self, LineBuffer *line, struct aesd_seekto *pSeekObj);

#endif /* CONNTHREAD_H */
