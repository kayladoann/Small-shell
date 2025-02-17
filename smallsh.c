#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_BCKGRD_PROCESSES 1001
#define DEV_NULL "/dev/null"

int backgroundFlag = 0;
int backFlag = 0;
int foregroundFlag = 0;
int currentExitStatus = 0;


pid_t backgroundProcesses[MAX_BCKGRD_PROCESSES];
int numBackgroundProcesses = 0;

int statusState = 0;

struct commandLine
{
    char *command;
    int argc;
    char *argv[514]; //Tokenize to get arguments (max 512 total arguments) (argv[513] Should always be NULL) (argv[0] == char* command)
    char *inputFile;
    char *outputFile;
    int backgroundProcess; // = 1 means that it is one, = 0 means that it is not one
};

void freeCommandLine(struct commandLine *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    if (cmd->command != NULL)
    {
        free(cmd->command);
        cmd->command = NULL;
    }

    for (int i = 0; i < cmd->argc; i++)
    {
        if (cmd->argv[i] != NULL)
        {
            free(cmd->argv[i]);
            cmd->argv[i] = NULL;
        }
    }

    if (cmd->inputFile != NULL)
    {
        free(cmd->inputFile);
        cmd->inputFile = NULL;
    }

    if (cmd->outputFile != NULL)
    {
        free(cmd->outputFile);
        cmd->outputFile = NULL;
    }

    free(cmd);
    cmd = NULL;
}

char *processPIDString(char *str)
{
    // Basic reformatting
    if (str == NULL)
    {
        return NULL;
    }

    if (str[strlen(str) - 1] == '\n')
    {
        str[strlen(str) - 1] = '\0';
    }

    // Objetive is to replace any $$ substrings with current PID

    // Get curr process ID
    pid_t pid = getpid();

    // A string for the pid 
    char pid_str[15];
    memset(pid_str, '\0', sizeof(pid_str));
    sprintf(pid_str, "%d", (int)pid);       // Converts int to str

    // We need to allocate a new string

    // Count how many dollar signs substring there are
    int newLen = strlen(str);

    for (int i = 0; i < strlen(str) - 1; i++)
    {
        if (str[i] == '$' && str[i + 1] == '$')
        {
            i++;
            newLen += (strlen(pid_str) - 2);
        }
    }

    char *newStr = calloc(newLen + 1, sizeof(char));

    int j = 0;
    for (int i = 0; i < strlen(str); i++)
    {
        if (str[i] == '$' && str[i + 1] == '$')
        {
            i++;
            for (int k = 0; k < strlen(pid_str); k++)
            {
                newStr[j] = pid_str[k];
                j++;
            }
        }
        else
        {
            newStr[j] = str[i];
            j++;
        }
    }

    free(str);

    return newStr;
}

void printCommandLine(struct commandLine *cmd)
{
    if (cmd != NULL)
    {
        printf("Command: %s\n", cmd->command);
        printf("Argument Count (argc): %d\n", cmd->argc);

        printf("Arguments (argv):\n");
        for (int i = 0; i < cmd->argc; ++i)
        {
            printf("  argv[%d]: %s\n", i, cmd->argv[i]);
        }

        printf("Input File: %s\n", cmd->inputFile);
        printf("Output File: %s\n", cmd->outputFile);
        printf("Background Process: %s\n", cmd->backgroundProcess ? "true" : "false");
    }
}

struct commandLine *create_commandLine()
{
    struct commandLine *cmdLine = malloc(sizeof(struct commandLine));

    cmdLine->command = NULL;
    cmdLine->argc = 0;
    for (int i = 0; i < 513; i++)
    {
        cmdLine->argv[i] = NULL;
    }
    cmdLine->inputFile = NULL;
    cmdLine->outputFile = NULL;
    cmdLine->backgroundProcess = 0;

    return cmdLine;
}

struct commandLine *commandPrompt()
{
    char *commandInput = NULL;
    size_t bufferSize = 0;

    printf(": ");
    fflush(stdout);
    ssize_t bytesRead = getline(&commandInput, &bufferSize, stdin);

    if (bytesRead == -1 || bytesRead <= 1)
    {
        clearerr(stdin);
        return NULL;
    }

    commandInput = processPIDString(commandInput);

    if (commandInput[0] == '#')
    {
        return NULL;
    }

    struct commandLine *cmdLine = create_commandLine();

    const char *delim = " ";
    char *token = NULL;
    char *saveptr = NULL;

    token = strtok_r(commandInput, delim, &saveptr);
    cmdLine->command = calloc(strlen(token) + 1, sizeof(char));
    cmdLine->argv[0] = calloc(strlen(token) + 1, sizeof(char));
    cmdLine->argc++;
    strcpy(cmdLine->command, token);
    strcpy(cmdLine->argv[0], token);

    // Use token to get string
    token = strtok_r(NULL, delim, &saveptr);

    // Arguments
    while (token != NULL && cmdLine->argc < 513)
    {
        // If token is < or > 
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0)
        {
            break;
        }

        // If token is & check if it is the last thing in the string
        else if (strcmp(token, "&") == 0)
        {
            if (strcmp(saveptr,"") == 0)
            {
                // Its a background Process because its at the end
                if (foregroundFlag == 0)
                {
                    cmdLine->backgroundProcess = 1;
                }
            }
            else
            {
                cmdLine->argv[cmdLine->argc] = calloc(strlen(token) + 1, sizeof(char));
                strcpy(cmdLine->argv[cmdLine->argc],token);
                cmdLine->argc++;
            }
        }

        // Else put in the argument arr
        else
        {
            cmdLine->argv[cmdLine->argc] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmdLine->argv[cmdLine->argc],token);
            cmdLine->argc++;
        }

        token = strtok_r(NULL, delim, &saveptr);
    }

    // Check if its input or output
    if (token != NULL && (strcmp(token, "<") == 0 || strcmp(token, ">") == 0))
    {
        // If its < input
        if (strcmp(token, "<") == 0)
        {
            // Go to the actual title
            token = strtok_r(NULL, delim, &saveptr);

            // Put in the variable
            cmdLine->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmdLine->inputFile,token);
            
            // move on to next thing
            token = strtok_r(NULL, delim, &saveptr);
        }

        // If its > output
        else if (strcmp(token, ">") == 0)
        {
            // Go to the actual title
            token = strtok_r(NULL, delim, &saveptr);

            // Put in the variable
            cmdLine->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmdLine->outputFile,token);
            
            // move on to next thing
            token = strtok_r(NULL, delim, &saveptr);
        }
    }

    if (token != NULL && (strcmp(token, "<") == 0 || strcmp(token, ">") == 0))
    {
        // If its < input
        if (strcmp(token, "<") == 0)
        {
            // Go to the actual title
            token = strtok_r(NULL, delim, &saveptr);

            // Put in the variable
            cmdLine->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmdLine->inputFile,token);
            
            // move on to next thing
            token = strtok_r(NULL, delim, &saveptr);
        }

        // If its > output
        else if (strcmp(token, ">") == 0)
        {
            // Go to the actual title
            token = strtok_r(NULL, delim, &saveptr);

            // Put in the variable
            cmdLine->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmdLine->outputFile,token);
            
            // move on to next thing
            token = strtok_r(NULL, delim, &saveptr);
        }
    }

    // Count for backgroud process
    if (token != NULL && strcmp(token, "&") == 0 && foregroundFlag == 0)
    {
        cmdLine->backgroundProcess = 1;
    }

    free(commandInput);

    return cmdLine;
}

int redirectIO(struct commandLine* cmd)
{
    int rFile;
    int wFile;

    if (cmd->inputFile != NULL)
    {
        // Try to open file in read mode
        rFile  = open(cmd->inputFile, O_RDONLY);

        // If it doesnt open, return -1
        if (rFile == -1)
        {
            printf("ERROR: Input file does not exist \n");
            fflush(stdout);
            return -1;
        }

        // Else use dup2()
        dup2(rFile, STDIN_FILENO);

        // Close file
        close(rFile);
    }
    else if (cmd->backgroundProcess == 1)
    {
        // Try to open file in read mode
        rFile  = open(DEV_NULL, O_RDONLY);

        // If it doesnt open, return -1
        if (rFile == -1)
        {
            printf("ERROR: Input file does not exist \n");
            fflush(stdout);
            return -1;
        }

        // Else use dup2()
        dup2(rFile, STDIN_FILENO);

        // Close file
        close(rFile);
    }

    if (cmd->outputFile != NULL)
    {
        // Try to open file in write mode, create or truncate
        wFile = open(cmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        // If it doesnt open, return -1
        if (wFile == -1)
        {
            printf("ERROR: Output file does not exist\n");
            fflush(stdout);
            return -1;
        }

        // Else use dup2()
        dup2(wFile, STDOUT_FILENO);

        // Close file
        close(wFile);
    }
    else if (cmd->backgroundProcess == 1)
    {
        // Try to open file in write mode, create or truncate
        wFile = open(DEV_NULL, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        // If it doesnt open, return -1
        if (wFile == -1)
        {
            printf("ERROR: Output file does not exist\n");
            fflush(stdout);
            return -1;
        }

        // Else use dup2()
        dup2(wFile, STDOUT_FILENO);

        // Close file
        close(wFile);
    }

    return 0;

}

void checkBP()
{
    pid_t currPID;
    int count;

    for (int i = 0; i < MAX_BCKGRD_PROCESSES; i++)
    {
        if (backgroundProcesses[i] != -3)
        {
            currPID = waitpid(backgroundProcesses[i], &statusState, WNOHANG);

            if (currPID > 0) // =-1 error =0 its still going on
            {
                if (WIFEXITED(statusState))
                {
                    printf("PID %d exited with status %d\n", backgroundProcesses[i], WEXITSTATUS(statusState));
                    fflush(stdout);
                }
                else if (WIFSIGNALED(statusState))
                {
                    printf("PID %d terminated with signal %d\n", backgroundProcesses[i], WTERMSIG(statusState));
                    fflush(stdout);

                    backgroundProcesses[i] = -3;
                }
            }
        }
    }
}

void execute(struct commandLine* cmd)
{
    // Variables to track fork()
    pid_t spawnpid = -1;
    int childStatus;

    backgroundFlag = 0;
    // Check if its a background Process, then flag accordingly
    if(cmd->backgroundProcess == 1)
    {
        backgroundFlag = 1;
    }

    // Fork switch
    spawnpid = fork();
    switch (spawnpid)
    {
        case -1:
            printf("failed to fork\n");
            fflush(stdout);
            exit(1);
            break;
        case 0: // If its the child

            // File redirection
            if (redirectIO(cmd) == -1)
            {
                exit(1);
            }

            // Signal handle
            struct sigaction int_action = {0};
            struct sigaction tstp_action = {0};

            int_action.sa_handler = SIG_DFL;
            if (cmd->backgroundProcess == 1)
            {
                int_action.sa_handler = SIG_IGN;
            }

            sigfillset(&int_action.sa_mask);
            int_action.sa_flags = 0;
            sigaction(SIGINT, &int_action, NULL);

            tstp_action.sa_handler = SIG_IGN;
            sigfillset(&tstp_action.sa_mask);
            tstp_action.sa_flags = 0;
            sigaction(SIGTSTP, &tstp_action, NULL);

            // Execute command with execvp
            execvp(cmd->argv[0], cmd->argv); // It goes to execute another program and ignores the rest

            // Print error if exec fails
            printf("ERROR: %s command not found\n", cmd->command);
            fflush(stdout);
            exit(1);
            break;

        default: // Parent code
            if (cmd->backgroundProcess == 0)
            {
                spawnpid = waitpid(spawnpid, &statusState, 0);
            }
            // Else its a background process
            else if (cmd->backgroundProcess == 1)
            {

                int openSpot = numBackgroundProcesses % MAX_BCKGRD_PROCESSES;

                backgroundProcesses[openSpot] = spawnpid;

                numBackgroundProcesses++;

                printf("Background PID %d\n", spawnpid);
                fflush(stdout);
            }

            // Run a function to check any background process completed
            checkBP();
            break;
    }

}

void builtInCommands(struct commandLine* cmd)
{
    if (cmd == NULL)
    {
        return;
    }
    
    if(strcmp(cmd->argv[0], "exit") == 0)
    {
        // Kill background processes 
        for (int i = 0; i < MAX_BCKGRD_PROCESSES; i++)
        {
            if (backgroundProcesses[i] != -3)
            {
                pid_t ongoing = waitpid(backgroundProcesses[i], &statusState, WNOHANG);

                if (ongoing == 0)
                {
                    kill(backgroundProcesses[i], SIGTERM);
                }
            }
        }

        // Kill the program
        exit(0);
    }
    else if(strcmp(cmd->argv[0], "cd") == 0)
    {
        char word[1024];

        if (cmd->argc > 1) // If its cd with arguments
        {
            // Change it to the argument
            chdir(cmd->argv[1]);

            getcwd(word, sizeof(word));

            printf("%s\n", word);
        }
        else
        {
            // Lets get home directory
            char* home = getenv("HOME");

            // Change it to there
            chdir(home);

            getcwd(word, sizeof(word));

            printf("%s\n", word);
        }
    }
    // Code inspired by 3.1 Processes slides
    else if(strcmp(cmd->argv[0], "status") == 0)
    {
        // Use a global variable to track the exit status of all different exec functions; statusState

        // If process exited normally
        if (WIFEXITED(statusState) && backFlag == 0)
        {
            printf("exit value %d\n", WEXITSTATUS(statusState));
            fflush(stdout);
        }
        else if (!WIFEXITED(statusState) && backFlag == 0)
        {
            printf("terminated by signal %d", WTERMSIG(statusState));
            fflush(stdout);
        }
        else // If there is a background process 
        {
            statusState = 1;
            printf("exit value %d\n", WTERMSIG(statusState));
            fflush(stdout);
        }
    }
    else // Execute all other commands
    {
        execute(cmd);
    }
}

void interuptSignal(int signo)
{
    write(STDOUT_FILENO, "\n", 1);
}

void tstpSignal(int signo)
{
    const char* message1 = "\nentering foreground mode\n";
    const char* message2 = "\nexiting foreground mode\n";

    if (foregroundFlag == 0) {
        write(STDOUT_FILENO, message1, strlen(message1));
        fflush(stdout);

        foregroundFlag = 1;
    }
    else {
        write(STDOUT_FILENO, message2, strlen(message2));
        fflush(stdout);

        foregroundFlag = 0;
    }
}

int main()
{
    // Ignore Cntrl C for Shell Program
    struct sigaction sigintAction = {0};
    sigintAction.sa_handler = interuptSignal;
    sigintAction.sa_flags = 0;
    sigfillset(&(sigintAction.sa_mask));
    sigaction(SIGINT, &sigintAction, NULL);

    // Use control Z to allow or block background processes
    struct sigaction sigtstpAction;
    sigtstpAction.sa_handler = tstpSignal;
    sigtstpAction.sa_flags = 0;
    sigfillset(&(sigtstpAction.sa_mask));
    sigaction(SIGTSTP, &sigtstpAction, NULL);
    
    // Initialize backgroundProcesses array
    for (int i = 0; i < MAX_BCKGRD_PROCESSES; i++)
    {
        backgroundProcesses[i] = -3;
    }

    while (1)
    {
        //checkBackgroundProcesses();
        struct commandLine *cmd = commandPrompt();

        // Execute commands
        builtInCommands(cmd);

        freeCommandLine(cmd);

    }

    return 0;
}

