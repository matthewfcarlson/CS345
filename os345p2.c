// os345p2.c - 5 state scheduling	08/08/2013
// ***********************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the CS345 projects.          **
// ** It comes "as is" and "unwarranted."  As such, when you use part   **
// ** or all of the code, it becomes "yours" and you are responsible to **
// ** understand any algorithm or method presented.  Likewise, any      **
// ** errors or problems become your responsibility to fix.             **
// **                                                                   **
// ** NOTES:                                                            **
// ** -Comments beginning with "// ??" may require some implementation. **
// ** -Tab stops are set at every 3 spaces.                             **
// ** -The function API's in "OS345.h" should not be altered.           **
// **                                                                   **
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// ***********************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include <time.h>
#include "os345.h"
#include <stdarg.h>
#include "os345signals.h"

#define my_printf	printf

// ***********************************************************************
// project 2 variables
static Semaphore* s1Sem;					// task 1 semaphore
static Semaphore* s2Sem;					// task 2 semaphore
extern Semaphore* tics10sec;
extern TaskQueue* ReadyQueue;

extern TCB tcb[];								// task control block
extern char printBuffer[];
extern int curTask;							// current task #
extern Semaphore* semaphoreList;			// linked list of active semaphores
extern jmp_buf reset_context;				// context of kernel stack

// ***********************************************************************
// project 2 functions and tasks

int signalTask(int, char**);
int ImAliveTask(int, char**);
int TenSecTask(int, char**);

// ***********************************************************************
// ***********************************************************************
// project2 command
int P2_project2(int argc, char* argv[])
{
	static char* s1Argv[] = {"signal1", "s1Sem"};
	static char* s2Argv[] = {"signal2", "s2Sem"};
	static char* aliveArgv[] = {"I'm Alive", "3"};

	printf("\nStarting Project 2");
	SWAP;
    for (int i=0;i<9;i++){
        createTask("TenSec",				// task name
                   TenSecTask,			// task
                   HIGH_PRIORITY,			// task priority
                   1,							// task argc
                   aliveArgv);				// task argument pointers
    }

	// start tasks looking for sTask semaphores
	createTask("signal1",				// task name
					signalTask,				// task
					VERY_HIGH_PRIORITY,	// task priority
					2,							// task argc
					s1Argv);					// task argument pointers

	createTask("signal2",				// task name
					signalTask,				// task
					VERY_HIGH_PRIORITY,	// task priority
					2,							// task argc
					s2Argv);					// task argument pointers

	createTask("I'm Alive",				// task name
					ImAliveTask,			// task
					LOW_PRIORITY,			// task priority
					2,							// task argc
					aliveArgv);				// task argument pointers

	createTask("I'm Alive",				// task name
					ImAliveTask,			// task
					LOW_PRIORITY,			// task priority
					2,							// task argc
					aliveArgv);				// task argument pointers

    
    return 0;
} // end P2_project2

void print_task(int i){
    if (tcb[i].name)
    {
        printf("\n%4d/%-4d%20s%4d  ", i, tcb[i].parent,
               tcb[i].name, tcb[i].priority);
        if (tcb[i].signal & mySIGSTOP) my_printf("Paused");
        else if (tcb[i].state == S_NEW) my_printf("New");
        else if (tcb[i].state == S_READY) my_printf("Ready");
        else if (tcb[i].state == S_RUNNING) my_printf("Running");
        else if (tcb[i].state == S_BLOCKED) my_printf("Blocked    %s",
                                                      tcb[i].event->name);
        else if (tcb[i].state == S_EXIT) my_printf("Exiting");
    }
}

// ***********************************************************************
// ***********************************************************************
// list tasks command
int P2_listTasks(int argc, char* argv[])
{
	int i;
    int position = 0;
    TaskQueue* tq;

    printf("\nReady Queue:");
    if (ReadyQueue == 0) printf(" empty");
    else{
        tq = ReadyQueue;
        
        while (tq){
            print_task(tq->id.tid);
            //printf("%d\t:%s\tTID: %d Pri:%d\n",position, tcb[tq->id.tid].name,tq->id.tid, tq->id.priority);
            tq = tq->nextTask;
            position++;
            if (position >= MAX_TASKS) assert("Too many waiting tasks" && 0);
            swapTask();
        }
        
    }
    
    Semaphore* sem = semaphoreList;
    while(sem)
    {
        if (sem->tasksWaiting != 0){
            printf("\n%s:", sem->name);
            tq = sem->tasksWaiting;
            //position = 0;
            while (tq){
                print_task(tq->id.tid);
                tq = tq->nextTask;
                position++;
                if (position >= MAX_TASKS) assert("Too many waiting tasks" && 0);
                swapTask();
            }
        }
        sem = (Semaphore*)sem->semLink;
    }
// ?? 2) Show the task stake (new, running, blocked, ready)
// ?? 3) If blocked, indicate which semaphore

	
	return 0;
} // end P2_listTasks



// ***********************************************************************
// ***********************************************************************
// list semaphores command
//
int match(char* mask, char* name)
{
   int i,j;

   // look thru name
	i = j = 0;
	if (!mask[0]) return 1;
	while (mask[i] && name[j])
   {
		if (mask[i] == '*') return 1;
		if (mask[i] == '?') ;
		else if ((mask[i] != toupper(name[j])) && (mask[i] != tolower(name[j]))) return 0;
		i++;
		j++;
   }
	if (mask[i] == name[j]) return 1;
   return 0;
} // end match

int P2_listSems(int argc, char* argv[])				// listSemaphores
{
	Semaphore* sem = semaphoreList;
	while(sem)
	{
		if ((argc == 1) || match(argv[1], sem->name))
		{
			printf("\n%20s  %c  %d  %s", sem->name, (sem->type?'C':'B'), sem->state,
	  					tcb[sem->taskNum].name);
		}
		sem = (Semaphore*)sem->semLink;
	}
	return 0;
} // end P2_listSems



// ***********************************************************************
// ***********************************************************************
// reset system
int P2_reset(int argc, char* argv[])						// reset
{
	longjmp(reset_context, POWER_DOWN_RESTART);
	// not necessary as longjmp doesn't return
	return 0;

} // end P2_reset



// ***********************************************************************
// ***********************************************************************
// kill task

int P2_killTask(int argc, char* argv[])			// kill task
{
	int taskId = INTEGER(argv[1]);				// convert argument 1

	if (taskId > 0) printf("\nKill Task %d", taskId);
	else printf("\nKill All Tasks");

	// kill task
	if (killTask(taskId)) printf("\nkillTask Error!");

	return 0;
} // end P2_killTask



// ***********************************************************************
// ***********************************************************************
// signal command
void sem_signal(Semaphore* sem)		// signal
{
	if (sem)
	{
		my_printf("\nSignal %s", sem->name);
		SEM_SIGNAL(sem);
	}
	else my_printf("\nSemaphore not defined!");
	return;
} // end sem_signal



// ***********************************************************************
int P2_signal1(int argc, char* argv[])		// signal1
{
	SEM_SIGNAL(s1Sem);
	return 0;
} // end signal

int P2_signal2(int argc, char* argv[])		// signal2
{
	SEM_SIGNAL(s2Sem);
	return 0;
} // end signal



// ***********************************************************************
// ***********************************************************************
// signal task
//
#define COUNT_MAX	5
//
int signalTask(int argc, char* argv[])
{
	int count = 0;					// task variable

	// create a semaphore
	Semaphore** mySem = (!strcmp(argv[1], "s1Sem")) ? &s1Sem : &s2Sem;
	*mySem = createSemaphore(argv[1], 0, 0);

	// loop waiting for semaphore to be signaled
	while(count < COUNT_MAX)
	{
		SEM_WAIT(*mySem);			// wait for signal
		printf("\n%s  Task[%d], count=%d", tcb[curTask].name, curTask, ++count);
	}
	return 0;						// terminate task
} // end signalTask



// ***********************************************************************
// ***********************************************************************
// I'm alive task
int ImAliveTask(int argc, char* argv[])
{
	int i;							// local task variable
	while (1)
	{
		printf("\n(%d) I'm Alive!", curTask);
		for (i=0; i<100000; i++) swapTask();
	}
	return 0;						// terminate task
} // end ImAliveTask

// ***********************************************************************
// ***********************************************************************
// I'm alive task
int TenSecTask(int argc, char* argv[])
{
    time_t cTime;
    
    while (1)
    {
        SEM_WAIT(tics10sec);
        time(&cTime);						// read current time
        printf("\n(%d) Ten Second Timer at %s", curTask, ctime(&cTime));
    }
    return 0;						// terminate task
} // end ImAliveTask

// **********************************************************************
// **********************************************************************
// read current time
//
char* myTime(char* svtime)
{
	time_t cTime;						// current time

	time(&cTime);						// read current time
	strcpy(svtime, asctime(localtime(&cTime)));
	svtime[strlen(svtime)-1] = 0;		// eliminate nl at end
	return svtime;
} // end myTime


void my_printf_todo(char* fmt, ...)
{
    va_list arg_ptr;
    
    char* s = printBuffer;
    
    va_start(arg_ptr, fmt);
    vsprintf(printBuffer, fmt, arg_ptr);
    
    while (*s) putchar(*s++);
    
    va_end(arg_ptr);
} // end my_printf
