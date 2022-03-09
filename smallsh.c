#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

bool quit = false;      // for exiting the while loop and the shell
bool fgOnly = false;    // foreground only mode
pid_t smallshpid;       // pid of the shell
int processStatus = 0;  // terminated child status
int bgProcesses[200];   // array of child pids
int bgCount = 0;        // how many current bg processes
int fgProcess;          // foreground child pid

int parseInput(char* userCommand);
int exitShell();
int changeDirectory(char* destinationDir);
int execCommand(char* inputArray[512], int count);
void printStatus();
void checkBgProcesses();
void parent_handle_sigint(int signo);
void child_handle_sigint(int signo);
void handle_sigtstp(int signo);
char* expand(char* userString);

int parseInput(char* userCommand) {
    char* inputArray[512];
    char* context;

    char* token = strtok_r(userCommand, " ", &context);

    int i = 0;
    while (token != NULL) {
        inputArray[i] = token;
        i++;
        token = strtok_r(NULL, " ", &context);
    }
    // i should contain the number of elements in array at end

    if (inputArray[0] == NULL) {
        // if empty line
        return 0;
    }

    if (strcmp(inputArray[0], "exit") == 0) {
        // exit function
        exitShell();

    } else if (strcmp(inputArray[0], "cd") == 0) {
        // change directory function
        changeDirectory(inputArray[1]);

    } else if (strcmp(inputArray[0], "status") == 0) {
        // status function
        printStatus();

    } else if (inputArray[0][0] == '#') {
        // comment line
        return 0;

    } else {
        // exec function for everything else
        execCommand(inputArray, i);
    }

    return 0;
}

void printStatus() {
    printf("exit value %d\n", processStatus);
}

int exitShell() {
    quit = true;
    int process;

    for (int i = 0; i < bgCount; i++) {
        process = bgProcesses[i];
        kill(process, 15);
    }

    return 0;
}

int changeDirectory(char* destinationDir) {
    int result;
    char cwd[200];
    if (strcmp(destinationDir, "") == 0) {
        // go to home directory
        result = chdir(getenv("HOME"));
    } else {
        result = chdir(destinationDir);
    }

    if (result != 0) {
        getcwd(cwd, sizeof(cwd));
        printf(cwd);
        perror("error\n");
    }
    return 0;
}

int execCommand(char* inputArray[512], int count) {
    int outFD;
    int inFD;
    int stdoutFD = dup(STDOUT_FILENO);
    int stdinFD = dup(STDIN_FILENO);
    int index = 0;
    int childStatus;
    char* outFile = NULL;
    char* inFile = NULL;
    char* execArray[512] = {NULL};
    bool background = false;
    pid_t childPid;

    // check for background process
    if (strcmp(inputArray[count - 1], "&") == 0) {
        if (!fgOnly) {
            background = true;
            inFile = "/dev/null\0";
            outFile = "/dev/null\0";
            bgCount++;
        }
        inputArray[count - 1] = NULL;
        count--;
    } 

    // check for redirection
    for (int i = 0; i < count; i++) {
        if (strcmp(inputArray[i], "<") == 0) {
            // input redirection
            inFile = inputArray[i + 1];
            i++;
        } else if (strcmp(inputArray[i], ">") == 0) {
            //output redirection
            outFile = inputArray[i + 1];
            i++;
        } else {
            // copies commands to separate array, except for the redirect
            // commands and filepaths
            execArray[index] = inputArray[i];
            index++;
        }
    }

    // output redirection
    if (outFile != NULL) {
        outFD = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (outFD != -1) {
            int result = dup2(outFD, 1);
            if (result == -1) {
                perror("out dup2\n");
                processStatus = 1;
            }
        } else {
            perror("out open\n");
            processStatus = 1;
        }
    }

    // input redirection
    if (inFile != NULL) {
        inFD = open(inFile, O_RDONLY);
        if (inFD != -1) {
            int result = dup2(inFD, 0);
            if (result == -1) {
                perror("in dup2\n");
                processStatus = 1;
            }
        } else {
            perror("in open\n");
            processStatus = 1;
        }
    }

    childPid = fork();

    switch(childPid){
        case -1:
            perror("fork()\n");
            processStatus = 1;
            break;

        case 0:

            // handle sigint for foreground processes
            if (!background) {
                struct sigaction SIGINT_action = {{0}};
                SIGINT_action.sa_handler = child_handle_sigint;
                sigfillset(&SIGINT_action.sa_mask);
                SIGINT_action.sa_flags = 0;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }

            // ignore signal sigtstp
            struct sigaction ignore_action = {{0}};
            ignore_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &ignore_action, NULL);

            // execute commands
            execvp(execArray[0], execArray);
            perror("execvp\n");
            processStatus = 1;
            exit(1);
            break;

        default:
            dup2(stdoutFD, STDOUT_FILENO);
            dup2(stdinFD, STDIN_FILENO);
            if (!background) {
                childPid = waitpid(childPid, &childStatus, 0);
                processStatus = childStatus;
            } else {
                printf("background process is %d\n", childPid);
                fflush(stdout);
            }
            break;
    }

    return 0;

}


// based on this answer at stack overflow:
// https://stackoverflow.com/a/779960
char* expand(char* userString) {
    char* ptr; // pointer to place in string;
    char* temp; // for various things
    char* toReplace = "$$";
    char* result;
    char pidString[12];
    int count; // how many replacements
    int repLen = strlen(toReplace);
    int pidLen;
    int lenBetween;

    // if nothing is entered
    if (strlen(userString) == 0) {
        return userString;
    }

    // get pid string
    sprintf(pidString, "%d", smallshpid);
    pidLen = strlen(pidString);

    ptr = userString;
    for (count = 0; (temp = strstr(ptr, toReplace)); count++) {
        ptr = temp + repLen;
    }

    // allocate space for expanded string
    temp = result = malloc(strlen(userString) + (pidLen - repLen) * count + 1);

    // if malloc fails
    if (!result) {
        return NULL;
    }

    // loop through original string, replacing instances of $$ with pid
    while (count--) {
        ptr = strstr(userString, toReplace);
        lenBetween = ptr - userString;
        temp = strncpy(temp, userString, lenBetween) + lenBetween;
        temp = strcpy(temp, pidString) + pidLen;
        userString += lenBetween + repLen;
    }

    // copy rest of string
    strcpy(temp, userString);

    return result;
}

int main() {
    smallshpid = getpid();

    char* prompt = ": ";
    char* userCommand;
    size_t len = 0;
    ssize_t read = 0;

    // register signal handlers
    struct sigaction sigint_action = {{0}};
    sigint_action.sa_handler = parent_handle_sigint;
    sigfillset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    struct sigaction sigtstp_action = {{0}};
    sigtstp_action.sa_handler = handle_sigtstp;
    sigfillset(&sigtstp_action.sa_mask);
    sigtstp_action.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp_action, NULL);


    while (!quit) {
        printf("%s", prompt);
        read = getline(&userCommand, &len, stdin);
        if (read == -1) {
            clearerr(stdin);
            continue;
        }
        userCommand[strlen(userCommand)-1] = '\0';
        char *commandString = expand(userCommand);
        if (commandString != NULL) {
            parseInput(commandString);
        } else {
            parseInput(userCommand);
        }

        fflush(0);
        free(commandString);
        checkBgProcesses();
        
    }
    return 0;
}

void checkBgProcesses() {
    // check if any background processes have finished
    int exitValue = 0;
    int process;
    int index = 0;
    int temp[200] = {0};

    for (int i = 0; i < bgCount; i++) {
        process = bgProcesses[i];
        process = waitpid(process, &exitValue, WNOHANG);
        if (process != 0 && WIFEXITED(exitValue)) {
            printf("background pid %d is done: exit value %d\n", process, WEXITSTATUS(exitValue));
            processStatus = WEXITSTATUS(exitValue);
            bgCount--;
        } else if (process != 0 && WIFSIGNALED(exitValue)) {
            printf("background pid %d is done: terminated by signal %d\n", process, WTERMSIG(exitValue));
            processStatus = WTERMSIG(exitValue);
            bgCount--;
        } else if (process == 0 && (bgProcesses[i] != 0)) {
            temp[index] = bgProcesses[i];
            index++;
        }
    }
    memcpy(bgProcesses, temp, sizeof(bgProcesses));
}

// signal handling
void child_handle_sigint(int signo) {
    processStatus = signo;
}

void parent_handle_sigint(int signo) {
    char* message = "terminated by signal 2\n";
    write(STDOUT_FILENO, message, 23);
}

void handle_sigtstp(int signo) {
    if (fgOnly == false) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 49);
        fgOnly = true;
    } else {
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 29);
        fgOnly = false;
    }
}
