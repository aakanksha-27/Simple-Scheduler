# Simple Scheduler

Implementation
---------------
1. The OS has limited number of CPUs for multiple processes to run, but it gives the illusion that all processes are running at the same time.
2. We take the number of CPUs and the TSLICE (time slice) as input, depending on which, the process scheduling is done.
3. Each process in the running queue gets to execute for TSLICE amount of time. Once the time expires, it moves to the ready queue, and another process from the ready queue is moved to the runnning queue.
4. Upon termination, the program prints the name, pid, completion time, and wait time of all the processes completed.
5. Additionally, users are also allowed to specify a priority value in the range 1-4 for the commands. The deafult priority is 1.

To Run
-------
gcc -o scheduler scheduler.c -lrt -pthread 
./scheduler

Contributions
--------------
Aakanksha (2023004): Signal handling, Semaphore handling, Error handling

Palak Yadav (2023363): Process scheduling, Submit command handling, Error handling
