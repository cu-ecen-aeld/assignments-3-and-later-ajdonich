#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd) {
    openlog(NULL, LOG_PID, LOG_USER);
    int status = system(cmd);
    if (cmd == NULL) {
        syslog(LOG_DEBUG, "system(3), shell %s available:", status != 0 ? "IS" : "NOT");
        return (status != 0);
    } 
    else if (status == -1) {
        syslog(LOG_ERR, "ERROR in system(3): %m");
        return false;
    } 
    else if (status == 127) {
        syslog(LOG_ERR, "ERROR in system(3): could not invoke shell");
        return false;
    }

    syslog(LOG_DEBUG, "system(%s) completed with exit code: %i", cmd, status);
    closelog();

    return (status == 0);
}

// Helper for do_exec (with outfd <= 0, does not redirect) or do_exec_redirect 
bool forkexecwait(int count, char *command[], int outfd) {
    int status;
    pid_t child;
    bool result = true;
    switch (child = fork()) {
    case -1:
        // Fork failed
        syslog(LOG_ERR, "ERROR in fork(2): %m");
        result = false;
        break;
        
    case 0:
        // Child process
        if (outfd > 0) {
            // Redirect STDOUT and STDERR to outfd, then close(outfd)
            if ((dup2(outfd, STDOUT_FILENO) == -1) || (dup2(outfd, STDERR_FILENO) == -1)){
               syslog(LOG_ERR, "ERROR in dup2(2): %m");
               result = false;
               break;
            }
            close(outfd);
        }

        char cmdline[256];
        strcpy(cmdline, command[0]);
        for (int i=1; i<count; ++i) {
            strcat(cmdline, " ");
            strcat(cmdline, command[i]);
        }
        syslog(LOG_DEBUG, "execv(%s)", cmdline);
        execv(command[0], command);

        // Get to here only if execv failed. Must explicitly exit()
        // with FAILURE to relay bad status to parent in waitpid(3p)
        syslog(LOG_ERR, "ERROR in execv(3): %m");
        closelog();
        exit(errno);
        break;

    default:
        // Parent process
        if (waitpid(child, &status, WUNTRACED) == -1) {
            syslog(LOG_ERR, "ERROR in waitpid(3p): %m");
            result = false;
        }
        else if (WIFSIGNALED(status)) {
            int signal = WTERMSIG(status);
            syslog(LOG_ERR, "child killed by signal: %i (%s)", signal, strsignal(signal));
            result = false;
        }
        else if (WIFEXITED(status)) {
            int exitcode = WEXITSTATUS(status);
            syslog(LOG_DEBUG, "child completed with exit code : %i", exitcode);
            result = (exitcode == 0);
        }
        break;
    }

    return result;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...) {
    openlog(NULL, LOG_PID, LOG_USER);

    va_list args;
    va_start(args, count);
    char* command[count+1];
    command[count] = NULL;
    for(int i=0; i<count; i++)
        command[i] = va_arg(args, char*);

    bool result = forkexecwait(count, command, -1);
    va_end(args);
    closelog();

    return result;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...) {
    openlog(NULL, LOG_PID, LOG_USER);
    syslog(LOG_DEBUG, "STDOUT redirect to: %s", outputfile);

    va_list args;
    va_start(args, count);
    char * command[count+1];
    command[count] = NULL;
    for(int i=0; i<count; i++)
        command[i] = va_arg(args, char *);

    // Open output file and fork
    bool result = true;
    mode_t perms = S_IRUSR|S_IWUSR | S_IRGRP|S_IWGRP | S_IROTH|S_IWOTH;
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, perms);
    if (fd != -1) result = forkexecwait(count, command, fd);
    else {
        syslog(LOG_ERR, "ERROR in open(%s): %m", outputfile);
        result = false;
    }   
    va_end(args);
    closelog();
    close(fd);

    return result;
}
