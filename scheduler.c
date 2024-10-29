#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>

int NCPU;
int TSLICE;
int shmid;

sem_t schedulerSem;
sem_t processQueueLock;

typedef struct {
    char cmd[1024];
    pid_t pid;
    bool background;
    timespec startTime;
    timespec endTime;
    timespec execTime;
    timespec waitTime;
    int state; //0-running, 1-waiting, -1-finish
    int priority;
} process;

typedef struct {
    struct process processes[200];
    int rear = 0;
} processQueue

struct processQueue schedulerQ;
struct processQueue terminatedQ;

static void my_handler(int signum);
void setupSignalHandler();
void shell_loop();
void read_user_input(char* input);
int launch (char* command , int status);
void trimWhiteSpace(char *str);
int create_process_and_run(char* cmd, int bg);

void enqueue(struct processQueue* q, struct process p){
    if (q.rear == 199){
        perror("Error: Queue is full.")
        exit(0);
    }
    q.processes[q.rear] = p;
}

void printTermination(){
    struct processQueue* q = terminatedQ;
    for (int i = 0; i < q.rear; i++){
        printf("%s %d Completion time: %lld ms Waiting time: %lld ms \n", q.processes[i].cmd, q.processes[i].pid, q.processes[i].endTime, q.processes[i].waitTime);
    }
}

static void my_handler(int signum) { // signal handler for SIGINT
    static int counter = 0;
    if (signum == SIGINT) {
        char buff1[23] = "\nCaught SIGINT signal\n";
        write(STDOUT_FILENO, buff1, 23);
        if (counter++ == 1) {
            char buff2[20] = "Cannot handle more\n";
            write(STDOUT_FILENO, buff2, 20);
            terminateHistory();
            exit(0);
        }
    }
    else if (signum == SIGUSR1) { // Signal the scheduler to wake up
        sem_post(&scheduler_sem);
    }
    else if (signum == SIGCHLD) {
        int status;
        int pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            sem_wait(&processQueueLock);
            for (int i = 0; i <= schedulerQ.rear; i++) {
                if (schedulerQ.processes[i].pid == pid) {

                    schedulerQ.processes[i].state = -1;

                    clock_gettime(CLOCK_MONOTONIC, &schedulerQ.processes[i].endTime);
                    duration.tv_sec = schedulerQ.processes[i].endTime.tv_sec - schedulerQ.processes[i].startTime.tv_sec;
                    duration.tv_nsec = schedulerQ.processes[i].endTime.tv_nsec - schedulerQ.processes[i].startTime.tv_nsec;

                    if (duration.tv_nsec < 0) {
                        duration.tv_sec --;
                        duration.tv_nsec += 1000000000;
                    }
                    long long dur = duration.tv_sec * 1000 + duration.tv_nsec / 1000000;

                    schedulerQ.processes[i].execTime += dur;
                    schedulerQ.processes[i].waitTime += dur;

                    terminatedQ.processes[++terminatedQ.rear] = schedulerQ.processes[i];
                    for (int j = i; j < schedulerQ.rear; j++) {
                        schedulerQ.processes[j] = schedulerQ.processes[j+1];
                    }
                    schedulerQ.rear--;
                    break;
                }
            }
            sem_post(&processQueueLock);
        }
    }
}

void setupSignalHandler() {
    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = my_handler;

    sigaction(SIGINT, &sig, NULL);
}

void shell_loop() { // for infinite loop running
    int status = 1;
    char input[1024];
    do {
        printf("group_48@aakanksha_palak:~$ ");
        read_user_input(input);
        status = launch(input, status);
    } while (status);
}

void read_user_input(char* input) {  // take input
    if (fgets(input, 1024, stdin) != NULL) {
        int length = strlen(input);
        for (int i = 0; input[i] != '\0'; i++) {
            if (input[i] == '\n') {
                input[i] = '\0';
                break;
            }
        }
    } 
    else {
        perror("Error: Unable to take input ");
        exit(0);
    }
}

int launch(char* command, int status) { // launches non piped commands
    if (strcmp(command, "exit") == 0) {
        printf("Shell ended");
        return 0;
    }
    else if (strcmp(command[0], "submit") == 0){

    }
    else {
        status = create_process_and_run(command, 0);
        if (status == -1) {
            perror("Error: Unable to create process and run ");
            exit(1);
        }
    }
    return 1;
}

void trimWhiteSpace(char *str) {  // trimming white spaces
    int len = strlen(str);
    int s = 0;
    int e = len - 1;

    while (isspace(str[s]) && s < len) s++;
    while (isspace(str[e]) && e >= 0) e--;

    char st[1024];
    int j = 0;
    for (int i = s; i <= e; i++) {
        st[j++] = str[i];
    }
    st[j] = '\0';
    strcpy(str, st);
}

int create_process_and_run(char* cmd, int bg) {  // create and run child process using fork
    int status = fork();
    if (status < 0) {
        perror("Error: Could Not Fork");
        exit(1);
    }
    if (status == 0) {
        if (bg) {
            if (setsid() == -1) {
                perror("Error: Setsid Error");
                exit(1);
            }
        }
        char* parameters[1024];
        char* tok = strtok(cmd, " ");

        int idx = 0;
        while (tok != NULL) {
            parameters[idx++] = tok;
            tok = strtok(NULL, " ");
        }
        parameters[idx] = NULL;

        if (execvp(parameters[0], parameters) == -1) {
            perror("Error: Execution Error");
            exit(1);
        }
    }
    else {
        int pid;
        if (!bg) {
            int status;
            pid = wait(&status);
            if (pid == -1) {
                perror("Error: Wait Failed");
                exit(1);
            }
        }
    }
    return 1;
}

int main() {
    printf("Enter the number of CPUs: ");
    scanf("%d", &NCPU);
    printf("Enter the time quantum (TSLICE) in milliseconds: ");
    scanf("%d",&TSLICE);

    setupSignalHandler();
    shell_loop();
    return 0;
}
