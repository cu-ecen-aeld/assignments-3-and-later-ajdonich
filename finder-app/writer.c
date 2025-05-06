#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#define MAX_EXPR_LEN 512

int main(int argc, char *argv[]) {
    // Init syslog params
    openlog(argv[0], LOG_PID, LOG_USER);

    // Validate command line args
    if (argc != 3) {
        // printf("usage: writer <path/to/file> <expression>\n");
        syslog(LOG_ERR, "usage: writer <path/to/file> <expression>");
        exit(EXIT_FAILURE);
    }

    int openFlags = O_CREAT | O_WRONLY | O_TRUNC;
    mode_t filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

    // Open output file
    int fd = open(argv[1], openFlags, filePerms);
    if (fd == -1) {
        // printf("ERROR: %s. Unable to open/create: %s\n", strerror(errno), argv[1]);
        syslog(LOG_ERR, "ERROR: %m. Unable to open/create: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // Write to output file
    size_t numWrite = strnlen(argv[2], MAX_EXPR_LEN);
    if (write(fd, argv[2], numWrite) != numWrite)  {
        // printf("ERROR: %s. Unable to write %zi bytes to: %s", strerror(errno), numWrite, argv[1]);
        syslog(LOG_ERR, "ERROR: %m. Unable to write %zi bytes to: %s", numWrite, argv[1]);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    exit(EXIT_SUCCESS);
}
