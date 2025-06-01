#include "connthread.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>

#define BLKINIT 512

static pthread_mutex_t varFileLock = PTHREAD_MUTEX_INITIALIZER;


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

ConnThread *newConnThread(const char *varfile) {
    static unsigned int _tid_generator = 1;

    ConnThread *ct = (ConnThread *)malloc(sizeof(ConnThread));
    if (ct == NULL) {
        syslog(LOG_ERR, "ERROR in newConnThread::malloc(3): %m");
        return NULL;
    }

    ct->cfd = -1;
    ct->varfile = varfile;
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
    int err, fd = -1;
    if ((fd = open(self->varfile, O_CREAT|O_WRONLY|O_APPEND, 0644)) == -1) { 
        syslog(LOG_ERR, "ERROR in writeFile::open(%s) %m", self->varfile);
        return -1;
    }

    // Obtain VARFILE lock
    if ((err = pthread_mutex_lock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeFile::pthread_mutex_lock(3p): %s", strerror(err));
        return -1;
    }
    
    ssize_t numWrite = write(fd, line->data, line->index);
    if (numWrite == -1) syslog(LOG_ERR, "ERROR in writeFile::write(2): %m");
    else syslog(LOG_DEBUG, "[TID: %i] Wrote %li (of %li) bytes to %s", 
        self->tid, numWrite, line->index, self->varfile);
    
    // Release VARFILE lock
    if ((err = pthread_mutex_unlock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeFile::pthread_mutex_unlock(3p): %s", strerror(err));
        return -1;
    }
    
    return numWrite;
}

ssize_t writeTimestamp(const char *varfile) {    
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
    if ((fd = open(varfile, O_CREAT|O_WRONLY|O_APPEND, 0644)) == -1) { 
        syslog(LOG_ERR, "ERROR in writeTimestamp::open(%s) %m", varfile);
        return -1;
    }

    // Obtain VARFILE lock
    if ((err = pthread_mutex_lock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::pthread_mutex_lock(3p): %s", strerror(err));
        return -1;
    }

    size_t slen = strlen(timestamp);
    ssize_t numWrite = write(fd, timestamp, slen);
    if (numWrite == -1) syslog(LOG_ERR, "ERROR in writeTimestamp::write(2): %m");
    else syslog(LOG_DEBUG, "Wrote \'%s\' (%li of %li bytes) to %s", timestamp, numWrite, slen, varfile);
    
    // Release VARFILE lock
    if ((err = pthread_mutex_unlock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in writeTimestamp::pthread_mutex_unlock(3p): %s", strerror(err));
        return -1;
    }
    
    return numWrite;
}

ssize_t sendFile(ConnThread *self) {
    static size_t blkx8 = BLKINIT*8;
    
    int err, fd = -1;
    if ((fd = open(self->varfile, O_RDONLY)) == -1) { 
        syslog(LOG_ERR, "ERROR in sendFile::open(%s) %m", self->varfile);
        return -1;
    }

    // Obtain VARFILE lock
    if ((err = pthread_mutex_lock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in sendFile::pthread_mutex_lock(3p): %s", strerror(err));
        return -1;
    }

    ssize_t numRead, numWrit;
    ssize_t totalSent = 0;
    ssize_t npackets = 0;
    char block[blkx8];

    while (1) {
        // Read blkx8 bytes from file
        numRead = read(fd, (void *)block, blkx8);
        if (numRead == -1) {
            if (errno == EINTR) continue; // Just inturrupted
            syslog(LOG_ERR, "ERROR in sendFile::read(%i): %m", fd);
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

    // Release VARFILE lock
    if ((err = pthread_mutex_unlock(&varFileLock)) != 0) {
        syslog(LOG_ERR, "ERROR in sendFile::pthread_mutex_unlock(3p): %s", strerror(err));
        return -1;
    }

    syslog(LOG_DEBUG, "[TID: %i] Sent %zi bytes (%zi pkts) to client", self->tid, totalSent, npackets);
    return totalSent;
}

void *connThreadMain(void *vself) {
    ConnThread *self = (ConnThread *)vself;

    // Get and log client info
    char ipaddr[INET_ADDRSTRLEN];
    const char *dst = inet_ntop(AF_INET, ((struct sockaddr *)&self->claddr)->sa_data, ipaddr, INET_ADDRSTRLEN);
    syslog(LOG_DEBUG, "[TID: %i] Accepted connection from %s", self->tid, dst ? ipaddr : "0.0.0.0");

    size_t lsz;
    ssize_t numSent;
    LineBuffer line = newLineBuffer();

    // Until EOF on cfd, read line from connection, append
    // it to VARFILE, send entire VARFILE back to client
    while (!self->_exitflag) {
        if ((lsz = readLine(self, &line)) == 0) break; // EOF
        else if (lsz == -1) break; // Read ERROR
        else if (writeFile(self, &line) != lsz) break; // Write ERROR
        else if ((numSent = sendFile(self)) == -1) break; // Send ERROR
    }
    
    destroy(&line);
    close(self->cfd); 
    syslog(LOG_DEBUG, "[TID: %i] Closed connection from %s", self->tid, ipaddr);
    self->_doneFlag = 1;
    return vself;
}
