// os345p3.c - Jurassic Park
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
#include <time.h>
#include <assert.h>
#include "os345.h"
#include "os345park.h"

// ***********************************************************************
// project 3 variables

// Jurassic Park
extern JPARK myPark;
extern Semaphore* parkMutex;						// protect park access
extern Semaphore* fillSeat[NUM_CARS];			// (signal) seat ready to fill
extern Semaphore* seatFilled[NUM_CARS];		// (wait) passenger seated
extern Semaphore* rideOver[NUM_CARS];			// (signal) ride over

typedef struct delta_timer{
    Semaphore* trigger;
    int ticks_left;
    struct delta_timer* next_timer;
} DeltaTimer;

DeltaTimer* delta_timer = 0;

static TID timeTaskID;

// ***********************************************************************
// project 3 functions and tasks
void CL3_project3(int, char**);
void CL3_dc(int, char**);

int dcMonitorTask(int argc, char* argv[]);
int timeTask(int argc, char* argv[]);
void insertDeltaClock(int ticks, Semaphore* event);
void tickDeltaClock();

// ***********************************************************************
// ***********************************************************************
// project3 command
int P3_project3(int argc, char* argv[])
{
	char buf[32];
	char* newArgv[2];

	// start park
	sprintf(buf, "jurassicPark");
	newArgv[0] = buf;
	createTask( buf,				// task name
		jurassicTask,				// task
		MED_PRIORITY,				// task priority
		1,								// task count
		newArgv);					// task argument

	// wait for park to get initialized...
	while (!parkMutex) SWAP;
	printf("\nStart Jurassic Park...");

	//?? create car, driver, and visitor tasks here

	return 0;
} // end project3

void tickDeltaClock(){
    if (delta_timer == 0)
        return;
    //printf("Ticking delta clock %s\n",delta_timer->trigger->name);
    delta_timer->ticks_left -= 1;
    DeltaTimer* destroy_timer;
    while (delta_timer != 0 && delta_timer->ticks_left == 0 ){
        SEM_SIGNAL(delta_timer->trigger);
        destroy_timer = delta_timer;
        delta_timer = delta_timer->next_timer;
        free(destroy_timer);
    }
}


void insertDeltaClock(int ticks, Semaphore* event){
    DeltaTimer* new_timer = malloc(sizeof(DeltaTimer));
    //printf("\nCreating delta timer: %s at %d\n",event->name,ticks);

    new_timer->next_timer = 0;
    new_timer->ticks_left = ticks;
    new_timer->trigger = event;
    
    if (delta_timer == 0){
        delta_timer = new_timer;
        return;
    }
    int delta = 0;
    DeltaTimer* pointer = delta_timer;
    if (delta_timer->ticks_left >= ticks){
        new_timer->next_timer = delta_timer;
        delta_timer = new_timer;
        delta = ticks;
    }
    else if (delta_timer->next_timer == 0){
        new_timer->ticks_left -= delta_timer->ticks_left;
        delta_timer->next_timer = new_timer;
        
    }
    else{
        DeltaTimer* prev_timer = pointer;
        int current_tick_count = 0;
        while (pointer != 0 && current_tick_count + pointer->ticks_left <= ticks){
            prev_timer = pointer;
            current_tick_count += pointer->ticks_left;
            //printf("Checking %s, count: %d, prev: %s\n",pointer->trigger->name, current_tick_count, prev_timer->trigger->name);
            pointer = pointer->next_timer;
            
        }
        //printf("Inserting after %s\n",prev_timer->trigger->name);
        prev_timer->next_timer = new_timer;
        new_timer->ticks_left -= current_tick_count;
        delta = new_timer->ticks_left;
        new_timer->next_timer = pointer;
    }
    
    pointer = new_timer->next_timer;
    while (delta >= 0 && pointer != 0){
        if (pointer->ticks_left >= delta) {
            pointer->ticks_left -= delta;
            delta = 0;
        }
        else{
            delta -= pointer->ticks_left;
            pointer->ticks_left = 0;
            
        }
        pointer = pointer->next_timer;
    }
    
    
    
}

// ***********************************************************************
// ***********************************************************************
// delta clock command
int P3_dc(int argc, char* argv[])
{
	printf("\nDelta Clock\n");
    DeltaTimer* current_timer = delta_timer;
    while(current_timer != 0){
        printf("%s: %i\n",current_timer->trigger->name,current_timer->ticks_left);
        current_timer = current_timer->next_timer;
        SWAP
    }
	return 0;
} // end CL3_dc





// ***********************************************************************
// test delta clock
int P3_tdc(int argc, char* argv[])
{
	createTask( "DC Test",			// task name
		dcMonitorTask,		// task
		10,					// task priority
		argc,					// task arguments
		argv);

	timeTaskID = createTask( "Time",		// task name
		timeTask,	// task
		10,			// task priority
		argc,			// task arguments
		argv);
	return 0;
} // end P3_tdc


extern Semaphore* tics1sec;
// ***********************************************************************
// monitor the delta clock task
int dcMonitorTask(int argc, char* argv[])
{
	int i, flg;
	char buf[32];
    Semaphore* event[10];
    // create some test times for event[0-9]
	int ttime[10] = {
		90, 300, 50, 170, 340, 300, 50, 300, 40, 110	};
    //8,  2,  6,  0,  9,   3,   1,   7,   5,    4
    //40, 50, 50, 90, 110, 170, 300, 300, 300,  340
    //Sould be 40, 10, 0, 40, 20, 60, 130, 0, 0, 40

	for (i=0; i<10; i++)
	{
		sprintf(buf, "event[%d]", i);
        event[i] = createSemaphore(buf, BINARY, 0);
		insertDeltaClock(ttime[i], event[i]);
	}
	P3_dc(0,0);

	while (delta_timer != 0)
	{
		SEM_WAIT(tics1sec)
		flg = 0;
		for (i=0; i<10; i++)
		{
			if (event[i]->state ==1)			{
					printf("\n  event[%d] signaled", i);
					event[i]->state = 0;
					flg = 1;
				}
		}
		if (flg) P3_dc(0,0);
	}
	printf("\nNo more events in Delta Clock");

	// kill dcMonitorTask
	//tcb[timeTaskID].state = S_EXIT;
	return 0;
} // end dcMonitorTask


// ********************************************************************************************
// display time every tics1sec
int timeTask(int argc, char* argv[])
{
	char svtime[64];						// ascii current time
	while (1)
	{
		SEM_WAIT(tics1sec)
		printf("\nTime = %s", myTime(svtime));
	}
	return 0;
} // end timeTask


