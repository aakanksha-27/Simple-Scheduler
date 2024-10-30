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
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>


int NCPU;
int TSLICE;
int shmid;

sem_t schedulerSem;
sem_t processQueueLock;

typedef struct process {
    char cmd[1024];
    pid_t pid;
    bool background;
    struct timespec startTime;
    struct timespec endTime;
    long long execTime;
    long long waitTime;
    int state; // 0-running, 1-waiting, -1-finish
    int priority;
} process;

typedef struct processQueue {
    struct process processes[200];
    int rear;
} processQueue;

processQueue *schedulerQ;  //Scheduler Queue
processQueue terminatedQ;  //Terminated Queue

static void my_handler(int signum);
void setupSignalHandler();
void shell_loop();
void read_user_input(char* input);
int launch (char* command , int status);
void trimWhiteSpace(char *str);
int create_process_and_run(char* cmd, int bg);

void enqueue(struct processQueue* q, struct process p){  // Add to the queue
    if (q->rear == 200){
        perror("Error: Queue is full.");
        exit(0);
    }
    q->processes[q->rear++] = p;
}

void printTermination(){  //Printing details of processess after termination
    processQueue* q = &terminatedQ;
    for (int i = 0; i < q->rear; i++){
        printf("%s %d Completion time: %lld ms Waiting time: %lld ms \n", q->processes[i].cmd, q->processes[i].pid, q->processes[i].endTime, q->processes[i].waitTime);
    }
}

static void my_handler(int signum) { // signal handler for SIGINT
    static int counter = 0;
    if (signum == SIGINT) {  //handling SIGINT
        char buff1[23] = "\nCaught SIGINT signal\n";
        write(STDOUT_FILENO, buff1, 23);
        if (counter++ == 1) {
            char buff2[20] = "Cannot handle more\n";
            write(STDOUT_FILENO, buff2, 20);
            printTermination();
            exit(0);
        }
    }
    else if (signum == SIGUSR1) { // Signal the scheduler to wake up
        sem_post(&schedulerSem);
    }
    else if (signum == SIGCHLD) {
        int status;
        int pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            sem_wait(&processQueueLock);
            for (int i = 0; i <= schedulerQ->rear; i++) {
                if (schedulerQ->processes[i].pid == pid) {

                    schedulerQ->processes[i].state = -1;

                    clock_gettime(CLOCK_MONOTONIC, &schedulerQ->processes[i].endTime);
                    struct timespec duration;
                    duration.tv_sec = schedulerQ->processes[i].endTime.tv_sec - schedulerQ->processes[i].startTime.tv_sec;
                    duration.tv_nsec = schedulerQ->processes[i].endTime.tv_nsec - schedulerQ->processes[i].startTime.tv_nsec;

                    if (duration.tv_nsec < 0) {
                        duration.tv_sec--;
                        duration.tv_nsec += 1000000000;
                    }
                    schedulerQ->processes[i].execTime = duration.tv_sec * 1000 + duration.tv_nsec / 1000000;
                    schedulerQ->processes[i].waitTime += schedulerQ->processes[i].execTime;

                    terminatedQ.processes[++terminatedQ.rear] = schedulerQ->processes[i];
                    for (int j = i; j < schedulerQ->rear; j++) {
                        schedulerQ->processes[j] = schedulerQ->processes[j+1];
                    }
                    schedulerQ->rear--;
                    break;
                }
            }
            sem_post(&processQueueLock);
        }
    }
}

void setupSignalHandler() {  //Setting up signal handler
    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = my_handler;

    sigaction(SIGINT, &sig, NULL);
    sigaction(SIGUSR1, &sig, NULL);
    sigaction(SIGCHLD, &sig, NULL);
}

void shell_loop() { // for infinite loop running
    int status = 1;
    char input[1024];
    do {
        printf("\nSimpleShell$ ");
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
    }else if (strncmp(command, "submit",6) == 0) {
        char *cmd = command + 7;
        trimWhiteSpace(cmd); //removing white spaces
        int pid = create_process_and_run(cmd, 0);
        if (pid < 0) {
            perror("Error: Failed to launch process");
            return 1;
        }
        process new_process;
        strncpy(new_process.cmd, cmd, sizeof(new_process.cmd) - 1);
        new_process.cmd[sizeof(new_process.cmd) - 1] = '\0';
        new_process.pid = pid;
        new_process.state = 1;
        new_process.priority = 1;
        clock_gettime(CLOCK_MONOTONIC, &new_process.startTime);
        sem_wait(&processQueueLock);
        enqueue(&schedulerQ, new_process);
        schedulerQ->rear++;
        sem_post(&processQueueLock);
        kill(getppid(), SIGUSR1);
   }
    else {
        status = create_process_and_run(command, 0);
        if (status < 0) {
            perror("Error: Unable to create process and run ");
            exit(0);
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
    int pid;
    if (status < 0) {
        perror("Error: Could Not Fork");
        exit(0);
    }
    if (status == 0) {
        if (bg) {
            if (setsid() == -1) {
                perror("Error: Setsid Error");
                exit(0);
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
            exit(0);
        }
    }
    else {
        if (!bg) {
            int status;
            pid = wait(&status);
            if (pid < 0) {
                perror("Error: Wait Failed");
                exit(0);
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

    if (sem_init(&schedulerSem, 0, 0) == -1 || sem_init(&processQueueLock, 0, 1) == -1){
      perror("Error: Unable to initialize semaphore");
      exit(0);
    }

    setupSignalHandler(); //setting up the signal handler

    shmid = shmget(IPC_PRIVATE, sizeof(struct processQueue), 0666 | IPC_CREAT); // Create shared memory
    if (shmid < 0 ) {
        perror("Error: shmget failed");
        exit(0);
    }

    schedulerQ = (processQueue*)shmat(shmid, NULL, 0); // Attach shared memory
    if (schedulerQ == (void*) -1) {
        perror("Error: shmat failed");
        exit(0);
    }
    //schedulerQ = shmdt;
    schedulerQ->rear = 0;
    terminatedQ.rear = 0;

    pid_t scheduler_pid = fork();
    if (scheduler_pid < 0) {
        perror("Error: Unable to fork scheduler process");
        exit(0);
    }
    if (scheduler_pid == 0) {
        // Scheduler Process
        while (1) {
            sem_wait(&schedulerSem); // Wait for a signal to proceed
            sem_wait(&processQueueLock);

            int activeProcesses = NCPU;  //number of cpus
            int priorityLevel = 0; //priority

            for (int i = 0; i <= schedulerQ->rear && activeProcesses > 0; i++) {
                if (schedulerQ->processes[i].state == 1 &&
                    (priorityLevel == 0 || priorityLevel == schedulerQ->processes[i].priority)) {

                    kill(schedulerQ->processes[i].pid, SIGCONT); // Resume the selected process
                    schedulerQ->processes[i].state = 0;  // Running

                    activeProcesses--; // Deduct from the count of available CPUs and set priority level
                    priorityLevel = schedulerQ->processes[i].priority; //set priority

                    int adjustedTimeSlice = (priorityLevel != 0) ? TSLICE / priorityLevel : TSLICE; // Adjust TSLICE based on priority
                    usleep(adjustedTimeSlice * 1000);

                    kill(schedulerQ->processes[i].pid, SIGSTOP); // Stop the process after its time slice expires
                    clock_gettime(CLOCK_MONOTONIC, &schedulerQ->processes[i].endTime);

                    // Calculate elapsed time
                    long long elapsedMs = (schedulerQ->processes[i].endTime.tv_sec - schedulerQ->processes[i].startTime.tv_sec) * 1000 +(schedulerQ->processes[i].endTime.tv_nsec - schedulerQ->processes[i].startTime.tv_nsec) / 1000000;

                    schedulerQ->processes[i].execTime += elapsedMs;
                    schedulerQ->processes[i].waitTime += elapsedMs * (schedulerQ->rear - 1);

                    schedulerQ->processes[i].state = 1; // Mark as waiting
                }
            }
            sem_post(&processQueueLock);
        }
    }
    else{
        shell_loop(); //running the shell
    }
    printTermination(); //printing the termination queue

    if (shmdt(schedulerQ) == -1 || shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("Error: Couldn't Release Shared Memory");
    }

    if (sem_destroy(&schedulerSem) == -1 || sem_destroy(&processQueueLock) == -1) {
        perror("Error: Unable to Release Semaphore");
    }

    exit(0);
    return 0;
}
