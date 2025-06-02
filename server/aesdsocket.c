#include "connthread.h"

#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/time.h>


#define LPORT 9000
#define BACKLOG 50
#define VARFILE "/var/tmp/aesdsocketdata"

// Global signal handler flag
volatile sig_atomic_t _exitflag = 0;  // SIGINT/SIGTERM
volatile sig_atomic_t _timerflag = 0; // SIGALRM

void exitSigHandler(int sig) {
    _exitflag = (_exitflag || sig == SIGINT || sig == SIGTERM);
}

void timerSigHandler(int sig) {
    _timerflag = (sig == SIGALRM);
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

    // No other files should be open yet except syslog
    // Server listen port is bound should be inherited

    if (chdir("/") == -1) {
        syslog(LOG_ERR, "ERROR in becomeDaemon::chdir(\"/\"): %m");
        return -1;
    }
    closelog();
    openlog(NULL, LOG_PID, LOG_USER);
    syslog(LOG_DEBUG, "Daemon tranformation complete (PID: %i => %i)", pid, getpid());
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

ConnThread *appendThread(ConnThread *head, ConnThread *ct) {
    if (!head) head = ct;
    else {
        ConnThread *node = head;
        while (node->next) node = node->next;
        node->next = ct;
    }
    return head;
}

ConnThread *pruneDoneThreads(ConnThread *head) {
    ConnThread *prv = NULL, *node = head;
    int err, pcnt = 0;

    while (node) {
        ConnThread *nxt = node->next;
        if (!node->_doneFlag) prv = node;
        else {
	    syslog(LOG_DEBUG, "Joining thread %i pruneDoneThreads", node->tid);
            if ((err = pthread_join(node->thread, NULL)) != 0)
                syslog(LOG_ERR, "ERROR in pruneDoneThreads::pthread_join(3): %s", strerror(err));

            free(node);           
            if (!prv) head = nxt;
            else prv->next = nxt;
            node = nxt;
	    pcnt += 1;
        }
        node = nxt;
    }

    if (pcnt) syslog(LOG_DEBUG, "Pruned %i ConnThread nodes", pcnt);
    return head;
}

ConnThread *termAllThreads(ConnThread *head) {
    int err, pcnt = 0;
    while (head) {
        head->_exitflag = 1;
	syslog(LOG_DEBUG, "Joining thread %i in termAllThreads", head->tid);
        if ((err = pthread_join(head->thread, NULL)) != 0)
            syslog(LOG_ERR, "ERROR in termAllThreads::pthread_join(3): %s", strerror(err));

        ConnThread *nxt = head->next;
        free(head);
        head = nxt;
	pcnt += 1;
    }

    syslog(LOG_DEBUG, "Terminated %i ConnThread nodes", pcnt);
    return NULL;
}

int eventLoop(int fd, int sfd) {
    ConnThread *head = NULL;

    int err;
    fd_set rfds;
    struct timeval tv;
    int retstatus = 0;

    // Set 10 sec timestamp signal timer
    struct itimerval tstampinv;
    tstampinv.it_value.tv_sec = 10;
    tstampinv.it_value.tv_usec = 0;
    tstampinv.it_interval.tv_sec = 10;
    tstampinv.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &tstampinv, NULL) == -1) {
        syslog(LOG_ERR, "ERROR in eventLoop::setitimer(2): %m");
        return -1;
    }

    while (!_exitflag) { 
        FD_ZERO(&rfds);
        FD_SET(sfd, &rfds);
        tv.tv_sec = 2; // select loop interval
        tv.tv_usec = 0;
        
        // Select on listen socket to avoid blocking signal deliveries
        int ready = select(sfd+1, &rfds, NULL, NULL, &tv);
        if (ready == -1) {
            if (_exitflag) break;
            else if (!_timerflag) {
                syslog(LOG_ERR, "ERROR in eventLoop::select(2): %m");
                retstatus = -1;
                break;
            }
        }
        else if (ready) {
            // Accept new client connection
            ConnThread *ct = newConnThread(VARFILE);
            socklen_t addrlen = sizeof(struct sockaddr_storage);
            if ((ct->cfd = accept(sfd, (struct sockaddr *)&ct->claddr, &addrlen)) == -1) {
                syslog(LOG_ERR, "ERROR in eventLoop::accept(2): %m");
                retstatus = -1;
                free(ct);
                break;
            }
            // Create thread for new connection
            else if ((err = pthread_create(&ct->thread, NULL, connThreadMain, ct)) != 0) {
                syslog(LOG_ERR, "ERROR in eventLoop::pthread_create(3): %s", strerror(err));
                retstatus = -1;
                free(ct);
                break;
            }
            else head = appendThread(head, ct);
        }
        
        if (_timerflag && writeTimestamp(VARFILE) == -1) {
            retstatus = -1;
            break;
        }
        _timerflag = 0;

        // Prune every loop iteration
	    head = pruneDoneThreads(head);
    }

    if (_exitflag) { 
        head = termAllThreads(head);
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
    
    // Add signal handler for SIGINT/SIGTERM/SIGALRM
    if ((signal(SIGINT, exitSigHandler) == SIG_ERR) || 
        (signal(SIGTERM, exitSigHandler) == SIG_ERR) || 
        (signal(SIGALRM, timerSigHandler) == SIG_ERR)) {
        syslog(LOG_ERR, "ERROR in main::signal(SIGINT/SIGTERM/SIGALRM): %m");
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
    // Loop forever
    else if (eventLoop(fd, sfd) == -1)  {
        status = EXIT_FAILURE;
    }

    closelog(); 
    if (sfd != -1) close(sfd);
    if (!keepvarfile) remove(VARFILE);
    exit(status);
}

