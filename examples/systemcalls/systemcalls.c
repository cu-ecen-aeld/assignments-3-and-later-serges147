#include "systemcalls.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    const int result = system(cmd);
    if (result < 0)
    {
        perror("system");
    }
    
    return result == 0;
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

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i = 0; i < count; ++i)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    const int pid = fork();
    if (pid == -1)
    {
        perror("fork");
        va_end(args);
        return false;
    }
    if (pid == 0)
    {
        // `execv` returns only if an error has occurred
        execv(command[0], command);
        perror("execv");
        va_end(args);
        abort();
    }
    va_end(args);

    int wstatus = 0;
    const int wait_res = wait(&wstatus);
    if (wait_res < 0)
    {
        perror("wait");
        return false;
    }
    
    return WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) == 0);
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i = 0; i < count; ++i)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    const int output_fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (output_fd < 0)
    {
        perror("open");
        va_end(args);
        return false;
    }
    
    const int child_pid = fork();
    switch (child_pid)
    {
    case -1:
        
        perror("fork");
        close(output_fd);
        va_end(args);
        return false;
    
    case 0: // child is here
        
        if (dup2(output_fd, 1) < 0)
        {
            perror("dup2");
            abort();
        }
        close(output_fd);
        
        // `execv` returns only if an error has occurred
        execv(command[0], command);
        
        perror("execvp");
        abort();
    
    default: // parent is here

        break;
    }
    close(output_fd);
    va_end(args);

    int wstatus = 0;
    const int wait_res = wait(&wstatus);
    if (wait_res < 0)
    {
        perror("wait");
        return false;
    }

    return WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) == 0);
}
