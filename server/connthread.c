#include "connthread.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#define BLKINIT 512

static pthread_mutex_t backendLock = PTHREAD_MUTEX_INITIALIZER;


LineBuffer newLineBuffer() {
    LineBuffer lb;
    lb.index = 0;
    lb.buffersz = BLKINIT;
    lb.data = (char *)malloc(BLKINIT);
    if (lb.data == NULL)
        syslog(LOG_ERR, "ERROR in newLineBuffer::malloc(3): %m");
    
    return lb;
}

int append(LineBuffer *self, char ch) {
    if ((self->index + 1) == self->buffersz) {
        size_t dsize = self->buffersz * 2;
        void *dbuffer = realloc((void *)self->data, dsize);
        if (dbuffer == NULL) {
            syslog(LOG_ERR, "ERROR in append::realloc(3): %m");
            return -1;
        }
        self->data = (char *)dbuffer;
        self->buffersz = dsize;

        syslog(LOG_DEBUG, "Realloc'd LineBuffer to %li bytes", self->buffersz);
    }

    self->data[self->index] = ch;
    self->data[self->index + 1] = '\0';
    self->index += 1;
    return 0;
}

void reset(LineBuffer *self) {
    self->index = 0;
}

void destroy(LineBuffer *self) {
    self->index = 0;
    self->buffersz = 0;
    if (self->data) free((void *)self->data);
    self->data = NULL;
}

ConnThread *newConnThread(const char *backend) {
    static unsigned int _tid_generator = 1;

    ConnThread *ct = (ConnThread *)malloc(sizeof(ConnThread));
    if (ct == NULL) {
        syslog(LOG_ERR, "ERROR in newConnThread::malloc(3): %m");
        return NULL;
    }

    ct->cfd = -1;
    ct->fd = -1;
    ct->backend = backend;
    ct->tid = _tid_generator++;
    ct->_exitflag = 0;
    ct->_doneFlag = 0;
    ct->next = NULL;
    return ct;
}

ssize_t readLine(ConnThread *self, LineBuffer *line) {
    char ch;
    reset(line);

    while (1) {
        ssize_t numRead = read(self->cfd, &ch, 1);
        if (numRead == -1) {
            if (errno == EINTR) continue; // If just inturrupted, try again
            syslog(LOG_ERR, "ERROR in readLine::read(%i): %m", self->cfd);
            return -1;
        }
        else if (numRead == 0) break; // EOF
        else if (append(line, ch) == -1) return -1; // Append ch 
        else if (ch == '\n') break; // EOL
    }

    syslog(LOG_DEBUG, "[TID: %i] Read %li bytes", self->tid, line->index);
    return line->index;
}

ssize_t writeFile(ConnThread *self, LineBuffer *line) {
    ssize_t numWrite = write(self->fd, line->data, line->index);
    if (numWrite == -1) syslog(LOG_ERR, "ERROR in writeFile::write(2): %m");
    else syslog(LOG_DEBUG, "[TID: %i] Wrote %li (of %li) bytes to %s", 
        self->tid, numWrite, line->index, self->backend);

    return numWrite;
}

ssize_t writeTimestamp(const char *backend) {    
    static char timestamp[64];

    struct tm *lt;
    time_t t = time(NULL);
    if ((lt = localtime(&t)) == NULL) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::localtime(2): %m");
        return -1;
    }
    else if (strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", lt) == 0) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::strftime: returned 0");
        return -1;
    }

    int err, fd = -1;
    if ((fd = open(backend, O_CREAT|O_WRONLY|O_APPEND, 0644)) == -1) { 
        syslog(LOG_ERR, "ERROR in writeTimestamp::open(%s) %m", backend);
        return -1;
    }

    // Obtain BACKEND lock
    if ((err = pthread_mutex_lock(&backendLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::pthread_mutex_lock(3p): %s", strerror(err));
        close(fd);
        return -1;
    }

    size_t slen = strlen(timestamp);
    ssize_t numWrite = write(fd, timestamp, slen);
    if (numWrite == -1) syslog(LOG_ERR, "ERROR in writeTimestamp::write(2): %m");
    else syslog(LOG_DEBUG, "Wrote \'%s\' (%li of %li bytes) to %s", timestamp, numWrite, slen, backend);
    
    // Release BACKEND lock
    if ((err = pthread_mutex_unlock(&backendLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::pthread_mutex_unlock(3p): %s", strerror(err));
        close(fd);
        return -1;
    }

    close(fd);
    return numWrite;
}

ssize_t sendFile(ConnThread *self, int whence) {
    static size_t blkx8 = BLKINIT*8;
    
    ssize_t numRead, numWrit;
    ssize_t totalSent = 0;
    ssize_t npackets = 0;
    char block[blkx8];

    // Offset always 0, really just to enable seek to beginning of backend
    // (for send after write) or to leave at SET_CUR (for send after ioctl)
    if (lseek(self->fd, 0, whence) == -1) {
        syslog(LOG_ERR, "ERROR in sendFile::lseek(%i): %m", self->fd);
        return -1;
    }

    while (1) {
        // Read blkx8 bytes from file
        if ((numRead = read(self->fd, (void *)block, blkx8)) == -1) {
            if (errno == EINTR) continue; // Just inturrupted
            syslog(LOG_ERR, "ERROR in sendFile::read(%i): %m", self->fd);
            break;
        }
        
        if (numRead == 0) // EOF
            break; 

        // Send numRead bytes to client
        numWrit = write(self->cfd, (void *)block, numRead);
        totalSent += numWrit;
        npackets += 1;

        if (numWrit != numRead) {
            syslog(LOG_ERR, "ERROR in sendFile::write(%i): %m", self->cfd);
            break;
        }
    }

    syslog(LOG_DEBUG, "[TID: %i] Sent %zi bytes (%zi pkts) to client", self->tid, totalSent, npackets);
    return totalSent;
}

int acquireBackend(ConnThread *self) {
    int err = -1;
    if ((self->fd = open(self->backend, O_CREAT|O_RDWR|O_APPEND, 0644)) == -1) {
        syslog(LOG_ERR, "ERROR in acquireBackend::open(%s) %m", self->backend);
    }
    else if ((err = pthread_mutex_lock(&backendLock)) != 0) {
        syslog(LOG_ERR, "ERROR in acquireBackend::pthread_mutex_lock(3p): %s", strerror(err));
        close(self->fd);
        self->fd = -1;
    }

    return err;
}

int releaseBackend(ConnThread *self) {
    int err;
    if ((err = pthread_mutex_unlock(&backendLock)) != 0)
        syslog(LOG_ERR, "ERROR in releaseBackend::pthread_mutex_unlock(3p): %s", strerror(err));
    
    close(self->fd);
    self->fd = -1;
    return err;  
}

// This ioctl command translates to a backend lseek call to 
// the offset corresponding to the aesd_seekto object params
int sendIoctl(ConnThread *self, struct aesd_seekto *pSeekObj) {
    int err;
    if ((err = ioctl(self->fd, AESDCHAR_IOCSEEKTO, pSeekObj)) == -1) 
        syslog(LOG_ERR, "ERROR in sendIoctl::ioctl(2): %m");
    else syslog(LOG_DEBUG, "[TID: %i] Sent ioctl obj [%u, %u] to %s", self->tid, 
        pSeekObj->write_cmd, pSeekObj->write_cmd_offset, self->backend);
    
    return err;
}

// Extracts ioctl aesd_seekto object if line matches: AESDCHAR_IOCSEEKTO:X,Y
int matchIoctl(ConnThread *self, LineBuffer *line, struct aesd_seekto *pSeekObj) {
    regex_t regex;
    regmatch_t groups[3];
    char digit[64];
    size_t n;

    regcomp(&regex, "^AESDCHAR_IOCSEEKTO:([0-9]+),([0-9]+)", REG_EXTENDED);      
    if (regexec(&regex, line->data, 3, groups, 0) == REG_NOMATCH) return 0;

    // Populate aesd_seekto object
    n = groups[1].rm_eo - groups[1].rm_so;
    memset((void *)digit, 0, n+1); // Ensure final '\0'
    stpncpy(digit, &line->data[groups[1].rm_so], n);
    pSeekObj->write_cmd = (uint32_t)atoi((const char *)digit);

    n = groups[2].rm_eo - groups[2].rm_so;
    memset((void *)digit, 0, n+1); // Ensure final '\0'
    stpncpy(digit, &line->data[groups[2].rm_so], n);
    pSeekObj->write_cmd_offset = (uint32_t)atoi((const char *)digit);

    syslog(LOG_DEBUG, "[TID: %i] Extracted aesd_seekto obj: [%u, %u]", 
        self->tid, pSeekObj->write_cmd, pSeekObj->write_cmd_offset);
    return 1;
}

void *connThreadMain(void *vself) {
    ConnThread *self = (ConnThread *)vself;

    // Get and log client info
    char ipaddr[INET_ADDRSTRLEN];
    const char *dst = inet_ntop(AF_INET, ((struct sockaddr *)&self->claddr)->sa_data, ipaddr, INET_ADDRSTRLEN);
    syslog(LOG_DEBUG, "[TID: %i] Accepted connection from %s", self->tid, dst ? ipaddr : "0.0.0.0");

    int isioctl;
    size_t lsz;
    ssize_t numSent;
    struct aesd_seekto seekObj;
    LineBuffer line = newLineBuffer();
    
    // Until EOF on cfd, read lines from connection
    while (!self->_exitflag) {
        if ((lsz = readLine(self, &line)) == 0) break; // EOF
        else if (lsz == -1) break; // Read ERROR
        else if (acquireBackend(self) != 0)  break; // Open/lock backend ERROR

        // If ioctl cmd line, send back content only from new lseek offset
        if (matchIoctl(self, &line, &seekObj)) {
            if (sendIoctl(self, &seekObj) == -1) break; // Ioctl ERROR
            else if ((numSent = sendFile(self, SEEK_CUR)) == -1) break; // Send ERROR
            else if (releaseBackend(self) != 0) break; // Close/unlock backend ERROR
        }
        // If standard line, write it to backend and send back entire content  
        else {
            if (writeFile(self, &line) != lsz) break; // Write ERROR
            else if ((numSent = sendFile(self, SEEK_SET)) == -1) break; // Send ERROR
            else if (releaseBackend(self) != 0) break; // Close/unlock backend ERROR
        }
    }
    
    destroy(&line);
    close(self->cfd);
    if (self->fd != -1) releaseBackend(self);
    syslog(LOG_DEBUG, "[TID: %i] Closed connection from %s", self->tid, ipaddr);
    self->_doneFlag = 1;
    return vself;
}
