#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define LPORT 9000
#define BACKLOG 50
#define VARFILE "/var/tmp/aesdsocketdata"
#define BLKINIT 512

// Global SIGINT/SIGTERM flag
int _exitflag = 0;

typedef struct {
    size_t index;
    size_t buffersz;
    char *data;
} LineBuffer;

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

void exitSigHandler(int sig) {
    _exitflag = (sig == SIGINT || sig == SIGTERM);
}

int becomeDaemon() {
    pid_t pid = getpid();

    // Fork and exit parent to go to background
    switch (fork()) {
    case -1: 
        syslog(LOG_ERR, "ERROR in becomeDaemon::fork(2): %m");
        return -1;
    case 0: break;               // Child continues
    default: exit(EXIT_SUCCESS); // Parent terminates
    }

    // Start new session to shed controlling terminal
    if (setsid() == -1) {
        syslog(LOG_ERR, "ERROR in becomeDaemon::setsid(2): %m");
        return -1;
    }

    // Fork once more to shed session leader role
    switch (fork()) {
    case -1: 
        syslog(LOG_ERR, "ERROR in becomeDaemon::fork(2): %m");
        return -1;
    case 0: break;
    default: exit(EXIT_SUCCESS);
    }

    int fd;
    if ((fd = open("/dev/null", O_RDWR)) == -1){
        syslog(LOG_ERR, "ERROR in becomeDaemon::open(/dev/null): %m");
        return -1;
    }
    // Point all STDXXX to /dev/null 
    else if ((dup2(fd, STDIN_FILENO) != STDIN_FILENO) || 
        (dup2(fd, STDOUT_FILENO) != STDOUT_FILENO) || 
        (dup2(fd, STDERR_FILENO) != STDERR_FILENO)) {
        syslog(LOG_ERR, "ERROR in becomeDaemon::dup2(2): %m");
        return -1;
    }

    pid_t dpid = getpid();
    syslog(LOG_DEBUG, "Daemon tranformation complete (PID: %i => %i)", pid, dpid);

    // No other files should be open yet except syslog
    // Server listen port is bound should be inherited

    chdir("/");
    closelog();
    openlog(NULL, LOG_PID, LOG_USER);
    return 0;
}

int tcpListen() {
    // Create a IPv4 TCP/steaming socket
    int sfd = socket(AF_INET , SOCK_STREAM, 0);
    if (sfd == -1) {
        syslog(LOG_ERR, "ERROR in tcpListen::socket(2): %m");
        return -1;
    }
    
    // Set reuseaddr
    int optval = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "ERROR in tcpListen::setsockopt(SO_REUSEADDR): %m");
        close(sfd);
        return -1;
    }

    // Server address struct (for tcp://0.0.0.0:LPORT)
    struct sockaddr_in svaddr;
    memset(&svaddr, 0, sizeof(svaddr));
    svaddr.sin_family = AF_INET;         // IPv4
    svaddr.sin_port = htons(LPORT);      // Port in network byte order
    svaddr.sin_addr.s_addr = INADDR_ANY; // Wildcard address: 0.0.0.0

    // Bind server address to socket
    if(bind(sfd, (struct sockaddr *)&svaddr, sizeof(svaddr)) == -1) {
        syslog(LOG_ERR, "ERROR in tcpListen::bind(%i, %i): %m", sfd, svaddr.sin_port);
        close(sfd);
        return -1;
    }

    // Mark socket as passive
    if(listen(sfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "ERROR in tcpListen::listen(%i): %m", sfd);
        close(sfd);
        return -1;
    }

    syslog(LOG_DEBUG, "Server listening on sfd: %i", sfd);
    return sfd;
}

ssize_t readLine(int fd, LineBuffer *line) {
    char ch;
    reset(line);

    while (1) {
        ssize_t numRead = read(fd, &ch, 1);
        if (numRead == -1) {
            if (errno == EINTR) continue; // If just inturrupted, try again
            syslog(LOG_ERR, "ERROR in readLine::read(%i): %m", fd);
            return -1;
        }
        else if (numRead == 0) break; // EOF
        else if (append(line, ch) == -1) return -1; // Append ch 
        else if (ch == '\n') break; // EOL
    }

    syslog(LOG_DEBUG, "Read %li bytes", line->index);
    return line->index;
}

ssize_t writeFile(int fd, LineBuffer *line) {    
    ssize_t numWrite = write(fd, line->data, line->index);
    if (numWrite == -1) syslog(LOG_ERR, "ERROR in writeFile::write(2): %m");
    else syslog(LOG_DEBUG, "Wrote %li (of %li) bytes to %s", numWrite, line->index, VARFILE);
    return numWrite;
}

ssize_t sendFile(int fd, int cfd) {
    static size_t blkx8 = BLKINIT*8;

    // Seek to beginning of file
    if (lseek(fd, 0, SEEK_SET) == -1) {
        syslog(LOG_ERR, "ERROR in sendFile::lseek(%i): %m", fd);
        return -1;
    }

    ssize_t numRead;
    size_t totalSent = 0;
    size_t npackets = 0;
    char block[blkx8];

    while (1) {
        // Read blocksz bytes from file
        numRead = read(fd, (void *)block, blkx8);
        if (numRead == -1) {
            if (errno == EINTR) continue; // Just inturrupted
            syslog(LOG_ERR, "ERROR in sendFile::read(%i): %m", fd);
            return -1;
        }
        
        if (numRead == 0) // EOF
            break; 

        // Send BLKINCR bytes to client
        if (write(cfd, (void *)block, numRead) != numRead) {
            syslog(LOG_ERR, "ERROR in sendFile::write(%i): %m", cfd);
            return -1;
        }
        totalSent += numRead;
        npackets += 1;
    }

    syslog(LOG_DEBUG, "Sent %zi bytes (%zi pkts) to client", totalSent, npackets);
    return totalSent;
}

int eventLoop(int fd, int sfd) {
    int retstatus = 0;
    int cfd = -1;
    size_t lsz;
    ssize_t numSent;
    char ipaddr[INET_ADDRSTRLEN];

    // Just selecting on sfd
    fd_set rfds;
    struct timeval tv;
    int ready;

    while (!_exitflag) { 
        FD_ZERO(&rfds);
        FD_SET(sfd, &rfds);    
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        // Select to prevent block on accept preventing SIGINT/SIGTERM delivery
        if ((ready = select(sfd+1, &rfds, NULL, NULL, &tv)) == 0) continue;
        else if (ready == -1) {
            syslog(LOG_ERR, "ERROR in eventLoop::select(2): %m");
            retstatus = -1;
            break;
        }

        // Accept new client connection
        struct sockaddr_storage claddr;
        socklen_t addrlen = sizeof(struct sockaddr_storage);
        cfd = accept(sfd, (struct sockaddr *)&claddr, &addrlen);
        if (cfd == -1) {
            syslog(LOG_ERR, "ERROR in eventLoop::accept(2): %m");
            retstatus = -1;
            break;
        }

        // Get and log client info
        const char *dst = inet_ntop(AF_INET, ((struct sockaddr *)&claddr)->sa_data, ipaddr, INET_ADDRSTRLEN);
        syslog(LOG_DEBUG, "Accepted connection from %s", dst ? ipaddr : "0.0.0.0");
        LineBuffer line = newLineBuffer();
        retstatus = 0;

        // Until EOF on cfd
        while (1) {
            // Read line from connection 
            lsz = readLine(cfd, &line);
            if (lsz == 0) break; // EOF
            else if (lsz == -1) {
                retstatus = -1;
                break;
            }
            // Append line to VARFILE
            else if (writeFile(fd, &line) != lsz) {
                retstatus = -1;
                break;
            }
            // Send entire VARFILE back to client
            else if ((numSent = sendFile(fd, cfd)) == -1) {
                retstatus = -1;
                break;
            }
        }

        close(cfd); 
        destroy(&line);        
        syslog(LOG_DEBUG, "Closed connection from %s", ipaddr);
    }

    if (_exitflag) { 
        syslog(LOG_DEBUG, "Caught signal, exiting");
        retstatus = 0;
    }
    return retstatus;
}

int main(int argc, char *argv[]) {
    int opt;
    int isdaemon = 0;
    int keepvarfile = 0;
    int fd = -1, sfd = -1;
    int status = EXIT_SUCCESS;

    // Handle command line 
    while ((opt = getopt(argc, argv, "dk")) != -1) {
        switch (opt) {
        case 'd':
            isdaemon = 1;
            break;
        case 'k':
            keepvarfile = 1;
            break;
        default: /* '?' */
            printf("usage: aesdsocket [-d] [-k]\n");
            exit(EXIT_FAILURE);
        }
    }
    
    // Init syslog params
    openlog(NULL, LOG_PID, LOG_USER);
    remove(VARFILE); // In case -k was used previously

    // Add signal handler for SIGINT/SIGTERM
    if ((signal(SIGINT, exitSigHandler) == SIG_ERR) || 
        (signal(SIGTERM, exitSigHandler) == SIG_ERR)) {
        syslog(LOG_ERR, "ERROR in main::signal(SIGINT/SIGTERM): %m");
        status = EXIT_FAILURE;
    }
    // Listen for clients
    else if ((sfd = tcpListen()) == -1) {
        status = EXIT_FAILURE;
    }
    // Fork to daemon only after binding listen port 
    else if (isdaemon && (becomeDaemon() == -1))  {
        status = EXIT_FAILURE;
    }
    // Open var tmp output file
    else if ((fd = open(VARFILE, O_CREAT|O_RDWR|O_APPEND, 0644)) == -1) { 
        syslog(LOG_ERR, "ERROR in main::open(%s) %m", VARFILE);
        status = EXIT_FAILURE;
    }
    // Loop forever
    else if (eventLoop(fd, sfd) == -1)  {
        status = EXIT_FAILURE;
    }

    closelog(); 
    if (sfd != -1) 
        close(sfd);
    if (fd != -1) {
        close(fd);
        if (!keepvarfile && remove(VARFILE) == -1) {
            syslog(LOG_ERR, "ERROR in main::remove(%s) %m", VARFILE);
            status = EXIT_FAILURE;
        }
    }
    exit(status);
}

