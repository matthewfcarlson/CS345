// os345.c - OS Kernel	09/12/2013
// ***********************************************************************
// **   DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER ** DISCLAMER   **
// **                                                                   **
// ** The code given here is the basis for the BYU CS345 projects.      **
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
#include <time.h>
#include <assert.h>

#include "os345.h"
#include "os345signals.h"
#include "os345config.h"
#include "os345lc3.h"
#include "os345fat.h"

// **********************************************************************
//	local prototypes
//
void pollInterrupts(void);
static int scheduler(void);
static int dispatcher(void);

//static void keyboard_isr(void);
//static void timer_isr(void);

int sysKillTask(int taskId);
static int initOS(void);

// **********************************************************************
// **********************************************************************
// global semaphores

Semaphore* semaphoreList;			// linked list of active semaphores

Semaphore* keyboard;				// keyboard semaphore
Semaphore* charReady;				// character has been entered
Semaphore* inBufferReady;			// input buffer ready semaphore

Semaphore* tics1sec;				// 1 second semaphore
Semaphore* tics10thsec;				// 1/10 second semaphore
Semaphore* tics10sec;               // 10 second semaphore

// **********************************************************************
// **********************************************************************
// global system variables

TCB tcb[MAX_TASKS];					// task control block
Semaphore* taskSems[MAX_TASKS];		// task semaphore
jmp_buf k_context;					// context of kernel stack
jmp_buf reset_context;				// context of kernel stack
volatile void* temp;				// temp pointer used in dispatcher

int scheduler_mode;					// scheduler mode
int superMode;						// system mode
int curTask;						// current task #
long swapCount;						// number of re-schedule cycles
char inChar;						// last entered character
int charFlag;						// 0 => buffered input
int inBufIndx;						// input pointer into input buffer
char inBuffer[INBUF_SIZE+1];		// character input buffer
//Message messages[NUM_MESSAGES];		// process message buffers

int pollClock;						// current clock()
int lastPollClock;					// last pollClock
bool diskMounted;					// disk has been mounted

time_t oldTime1;					// old 1sec time
time_t oldTime10;					// old 1sec time
clock_t myClkTime;
clock_t myOldClkTime;
char printBuffer[128];



//Ready Queues
TaskQueue* ReadyQueue;


// **********************************************************************
// **********************************************************************
// OS startup
//
// 1. Init OS
// 2. Define reset longjmp vector
// 3. Define global system semaphores
// 4. Create CLI task
// 5. Enter scheduling/idle loop
//
int main(int argc, char* argv[])
{
	// save context for restart (a system reset would return here...)
	int resetCode = setjmp(reset_context);
	superMode = TRUE;						// supervisor mode
    
    

	switch (resetCode)
	{
		case POWER_DOWN_QUIT:				// quit
			powerDown(0);
			printf("\nGoodbye!!");
			return 0;

		case POWER_DOWN_RESTART:			// restart
			powerDown(resetCode);
			printf("\nRestarting system...\n");

		case POWER_UP:						// startup
			break;

		default:
			printf("\nShutting down due to error %d", resetCode);
			powerDown(resetCode);
			return resetCode;
	}

	// output header message
	printf("%s", STARTUP_MSG);

	// initalize OS
	if (resetCode = initOS()) return resetCode;

	// create global/system semaphores here
	//?? vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

	charReady = createSemaphore("charReady", BINARY, 0);
	inBufferReady = createSemaphore("inBufferReady", BINARY, 0);
	keyboard = createSemaphore("keyboard", BINARY, 1);
	tics1sec = createSemaphore("tics1sec", BINARY, 0);
	tics10thsec = createSemaphore("tics10thsec", BINARY, 0);
    tics10sec = createSemaphore("tics10sec", COUNTING, 0);

	//?? ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

	// schedule CLI task
	createTask("myShell",			// task name
					P1_shellTask,	// task
					MED_PRIORITY,	// task priority
					argc,			// task arg count
					argv);			// task argument pointers

	// HERE WE GO................

	// Scheduling loop
	// 1. Check for asynchronous events (character inputs, timers, etc.)
	// 2. Choose a ready task to schedule
	// 3. Dispatch task
	// 4. Loop (forever!)

	while(1)									// scheduling loop
	{
		// check for character / timer interrupts
		pollInterrupts();

		// schedule highest priority ready task
		if ((curTask = scheduler()) < 0) continue;

		// dispatch curTask, quit OS if negative return
		if (dispatcher() < 0) break;
	}											// end of scheduling loop

	// exit os
	longjmp(reset_context, POWER_DOWN_QUIT);
	return 0;
} // end main



// **********************************************************************
// **********************************************************************
// scheduler
//
static int scheduler()
{
	// ?? Design and implement a scheduler that will select the next highest
	// ?? priority ready task to pass to the system dispatcher.

	// ?? WARNING: You must NEVER call swapTask() from within this function
	// ?? or any function that it calls.  This is because swapping is
	// ?? handled entirely in the swapTask function, which, in turn, may
	// ?? call this function.  (ie. You would create an infinite loop.)

	// ?? Implement a round-robin, preemptive, prioritized scheduler.

	// ?? This code is simply a round-robin scheduler and is just to get
	// ?? you thinking about scheduling.  You must implement code to handle
	// ?? priorities, clean up dead tasks, and handle semaphores appropriately.

	// schedule next task
    
    TaskID topTask = checkReadyQueue();
    if (topTask.tid == curTask && tcb[curTask].state == S_READY){
        
        //listQueues();
        TaskID readyTask = takeFromReadyQueue();
        //printf("Check to make sure we don't need to switch for TID:%d\n",readyTask.tid);
        addToReadyQueue(readyTask.tid, readyTask.priority);
        topTask = checkReadyQueue();
        //listQueues();
    }
    
    
    if (topTask.priority == 0) return -2;
    
    if (tcb[topTask.tid].signal & mySIGSTOP) return -1;
    //if (topTask.tid != curTask) printf("Starting task tid: %d\n",topTask.tid);
    //else printf("Sticking with the same thing");
    
    curTask = topTask.tid;

	return curTask;
} // end scheduler



// **********************************************************************
// **********************************************************************
// dispatch curTask
//
static int dispatcher()
{
	int result;

	// schedule task
	switch(tcb[curTask].state)
	{
		case S_NEW:
		{
			// new task
			printf("\nNew Task[%d] %s", curTask, tcb[curTask].name);
			tcb[curTask].state = S_RUNNING;	// set task to run state

			// save kernel context for task SWAP's
			if (setjmp(k_context))
			{
				superMode = TRUE;					// supervisor mode
				break;								// context switch to next task
			}

			// move to new task stack (leave room for return value/address)
			temp = (int*)tcb[curTask].stack + (STACK_SIZE-8);
			SET_STACK(temp);
			superMode = FALSE;						// user mode

			// begin execution of new task, pass argc, argv
			result = (*tcb[curTask].task)(tcb[curTask].argc, tcb[curTask].argv);

			// task has completed
			if (result) printf("\nTask[%d] returned %d", curTask, result);
			else printf("\nTask[%d] returned %d", curTask, result);
			tcb[curTask].state = S_EXIT;			// set task to exit state

			// return to kernal mode
			longjmp(k_context, 1);					// return to kernel
		}

		case S_READY:
		{
			tcb[curTask].state = S_RUNNING;			// set task to run
		}

		case S_RUNNING:
		{
			if (setjmp(k_context))
			{
				// SWAP executed in task
				superMode = TRUE;					// supervisor mode
				break;								// return from task
			}
			if (signals()) break;                   // if we get a 1 here, we aren't supposed to be scheduled
			longjmp(tcb[curTask].context, 3); 		// restore task context
		}

		case S_BLOCKED:
		{
			break;
		}

		case S_EXIT:
		{
			if (curTask == 0) return -1;			// if CLI, then quit scheduler
            // release resources and kill task
			sysKillTask(curTask);					// kill current task
            break;
		}

		default:
		{
			printf("Unknown Task[%d] State", curTask);
			longjmp(reset_context, POWER_DOWN_ERROR);
		}
	}
	return 0;
} // end dispatcher


// **********************************************************************
// **********************************************************************
// Unblocks a task on a semaphore

// 1. Finds the highest prority task in the blocked list blocked by the semaphore
// 2. Moves it to ready list
// 3. Return 0 if unsuccessful

int unblock_task(Semaphore* s){
    
    //get the first task ready to unblock
    TaskID capturedTask = takeFromBlockedQueue(s);
    
    //check to make sure we are in the running or ready state
    if (capturedTask.priority == 0 || tcb[capturedTask.tid].state != S_BLOCKED)
        return 0;
    
    
    return addToReadyQueue(capturedTask.tid, capturedTask.priority);
}


// **********************************************************************
// **********************************************************************
// Blocks a task on a semaphore

// 1. Checks to make sure the task is in the ready queue
// 2. Take the task out of the ready list
// 3. Puts it in the blocked list
// 4. Return 0 if it is already blocked

int block_task(int tid, Semaphore* s){
    //check to make sure we are in the running or ready state
    if (tcb[tid].state != S_RUNNING && tcb[tid].state != S_READY)
        return 0;
    
    TaskQueue* queuePointer = ReadyQueue;
    TaskQueue* queuePointerPrev = ReadyQueue;
    
    if (queuePointer == 0){
        return addToBlockedQueue(s, tid, tcb[tid].priority);
    }
    
    while(queuePointer != 0 && queuePointer->id.tid != tid){
        queuePointerPrev = queuePointer;
        queuePointer = queuePointer -> nextTask;
    }
    
    TaskID capturedTask;
    
    if (queuePointerPrev != 0 && queuePointerPrev == ReadyQueue && queuePointerPrev->id.tid == tid){
        ReadyQueue = queuePointerPrev->nextTask;
        capturedTask = queuePointerPrev->id;
        free(queuePointerPrev);
        
    }
    else if (queuePointer != 0 && queuePointer->id.tid == tid){
        queuePointerPrev->nextTask = queuePointer->nextTask;
        capturedTask = queuePointer->id;
        free(queuePointer);
    }
    else{
        return 0;
    }
    if (s == 0) return -1;
    return addToBlockedQueue(s, capturedTask.tid, capturedTask.priority);
    
}


// **********************************************************************
// **********************************************************************
// Adds a task to the readyQueue

// 1. Moves the task from the ready list to the blocked list
// 2. Return 0 if it is already blocked

int addToReadyQueue(TID tid, int prority){
    
    if (prority == 0) return 0;
    
    TaskQueue* queuePointer = ReadyQueue;
    TaskQueue* queuePointerPrev = ReadyQueue;
    TaskQueue* newtq = malloc(sizeof(TaskQueue));
    newtq->id.priority = prority;
    newtq->id.tid = tid;
    newtq->nextTask = 0;
    
    
    
    if (queuePointer == 0){
        ReadyQueue = newtq;
        if (tcb[tid].state != S_NEW)
            tcb[tid].state = S_READY;
        return 1;
    }
    
    while(queuePointer != 0 && queuePointer->id.priority >= prority){
        //if the task is already in the ready queue
        if (queuePointer->id.tid == tid){
            free(newtq);
            printf("Already ready\n");
            return 0;
        }
        queuePointerPrev = queuePointer;
        queuePointer = queuePointer -> nextTask;
    }
    //listQueues();
    //printf("Now adding\n");
    //Add the task
   
    if (queuePointer == ReadyQueue){
        
        ReadyQueue = newtq;
    }
    else{
        queuePointerPrev->nextTask = newtq;
    }
    
    newtq->nextTask = queuePointer;
    //listQueues();
    //printf("All better\n");
    if (tcb[tid].state != S_NEW)
        tcb[tid].state = S_READY;
    return 1;
    
}

// **********************************************************************
// **********************************************************************
// Gets the task at the top of the readyQueue

// 1. Moves the task from the ready list to the blocked list
// 2. Return 0 if it is already blocked

TaskID takeFromReadyQueue(){
    TaskID capturedTask;
    capturedTask.tid = -1;
    capturedTask.priority = 0;
    
    TaskQueue* newTask = ReadyQueue;
    
    if (newTask != 0 && newTask->id.priority != 0){
        capturedTask = newTask->id;
        ReadyQueue = newTask->nextTask;
        
        free(newTask);
    }
    
    return capturedTask;
}

TaskID checkReadyQueue(){
    TaskID capturedTask;
    capturedTask.tid = -1;
    capturedTask.priority = 0;
    
    TaskQueue* newTask = ReadyQueue;
    
    if (newTask != 0 && newTask->id.priority != 0){
        capturedTask = newTask->id;
    }
    
    return capturedTask;
}


// **********************************************************************
// **********************************************************************
// Adds a task it to the blocked Queue

// 1. Moves the task from the ready list to the blocked list
// 2. Return 0 if it is already blocked

int addToBlockedQueue(Semaphore* s, TID tid, int prority){
    
    if (prority <= 0 || tid < 0) return 0;
    
    TaskQueue* queuePointer = s->tasksWaiting;
    TaskQueue* queuePointerPrev = queuePointer;
    TaskQueue* newtq = malloc(sizeof(TaskQueue));
    newtq->id.priority = prority;
    newtq->id.tid = tid;
    newtq->nextTask = 0;
    
    if (queuePointer == 0){
        s->tasksWaiting = newtq;
        tcb[tid].event = s;
        tcb[tid].state = S_BLOCKED;
        return 1;
    }
    
    while(queuePointer != 0 && queuePointer->id.priority >= prority){
        //if the task is already in the semaphore's queue
        if (queuePointer->id.tid == tid){
            tcb[tid].state = S_BLOCKED;
            printf("Already in blocked queue!");
            free(newtq);
            return 0;
        }
        queuePointerPrev = queuePointer;
        queuePointer = queuePointer -> nextTask;
    }
    
    //Add the task
    queuePointerPrev->nextTask = newtq;
    newtq->nextTask = queuePointer;
    tcb[tid].event = s;
    tcb[tid].state = S_BLOCKED;
    return 1;
}

// **********************************************************************
// **********************************************************************
// Blocks a task on a semaphore

// 1. Moves the task from the ready list to the blocked list
// 2. Return 0 in prority if it is already blocked

TaskID takeFromBlockedQueue(Semaphore* s){
    TaskID capturedTask;
    capturedTask.tid = 0;
    capturedTask.priority = 0;
    
    TaskQueue* newTask = s->tasksWaiting;
    
    if (newTask != 0 && newTask->id.priority != 0){
        capturedTask = newTask->id;
        s->tasksWaiting = newTask->nextTask;
        free(newTask);
        tcb[capturedTask.tid].event = 0;
    }

    
    return capturedTask;
}

TaskID removeFromBlockedQueue(Semaphore* s,TID tid, int prority){
    TaskID capturedTask;
    capturedTask.tid = 0;
    capturedTask.priority = 0;
    
    TaskQueue* newTask = s->tasksWaiting;
    TaskQueue* prevTask = newTask;
    
    while (newTask != 0 && newTask->id.tid != tid){
        prevTask = newTask;
        newTask = newTask->nextTask;
    }
    if (newTask != 0){
        capturedTask = newTask->id;
        if (prevTask != newTask)
            prevTask->nextTask = newTask->nextTask;
    }
    
    return capturedTask;
}


void listQueues(){
    printf("\nReadyQueue: ");
    if (ReadyQueue == 0) printf("empty\n");
    else{
        TaskQueue* tq = ReadyQueue;
        int position = 0;
        while (tq){
            printf("%d\t:%s\tTID: %d Pri:%d\n",position, tcb[tq->id.tid].name,tq->id.tid, tq->id.priority);
            tq = tq->nextTask;
            position++;
            if (position >= MAX_TASKS) assert("Too many waiting tasks" && 0);
        }
        printf("Done.\n");
    }
}

// **********************************************************************
// **********************************************************************
// Do a context switch to next task.

// 1. If scheduling task, return (setjmp returns non-zero value)
// 2. Else, save current task context (setjmp returns zero value)
// 3. Set current task state to READY
// 4. Enter kernel mode (longjmp to k_context)

void swapTask()
{
	assert("SWAP Error" && !superMode);		// assert user mode
    if (superMode){
        printf(":(");
        return;
    }

	// increment swap cycle counter
	swapCount++;

	// either save current task context or schedule task (return)
	if (setjmp(tcb[curTask].context))
	{
		superMode = FALSE;					// user mode
		return;
	}

	// context switch - move task state to ready
	if (tcb[curTask].state == S_RUNNING) tcb[curTask].state = S_READY;

	// move to kernel mode (reschedule)
	longjmp(k_context, 2);
} // end swapTask



// **********************************************************************
// **********************************************************************
// system utility functions
// **********************************************************************
// **********************************************************************

// **********************************************************************
// **********************************************************************
// initialize operating system
static int initOS()
{
	int i;

	// make any system adjustments (for unblocking keyboard inputs)
	INIT_OS

	// reset system variables
	curTask = 0;						// current task #
	swapCount = 0;						// number of scheduler cycles
	scheduler_mode = 0;					// default scheduler
	inChar = 0;							// last entered character
	charFlag = 0;						// 0 => buffered input
	inBufIndx = 0;						// input pointer into input buffer
	semaphoreList = 0;					// linked list of active semaphores
	diskMounted = 0;					// disk has been mounted

	// capture current time
	lastPollClock = clock();			// last pollClock
	time(&oldTime1);
    time(&oldTime10);

    // initialize lc-3 memory
	initLC3Memory(LC3_MEM_FRAME, 0xF800>>6);

	// ?? initialize all execution queues
    ReadyQueue = 0;

	return 0;
} // end initOS



// **********************************************************************
// **********************************************************************
// Causes the system to shut down. Use this for critical errors
void powerDown(int code)
{
	int i;
	printf("\nPowerDown Code %d", code);

	// release all system resources.
	printf("\nRecovering Task Resources...");

	// kill all tasks
	for (i = MAX_TASKS-1; i >= 0; i--)
		if(tcb[i].name) sysKillTask(i);

	// delete all semaphores
	while (semaphoreList)
		deleteSemaphore(&semaphoreList);

	// free ready queue
	// TODO: release the queues
    TaskID releasingTask;
    releasingTask.priority = 1;
    while((releasingTask = takeFromReadyQueue()).priority);
    

	// ?? release any other system resources
	// ?? deltaclock (project 3)

	RESTORE_OS
	return;
} // end powerDown

