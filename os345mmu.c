// os345mmu.c - LC-3 Memory Management Unit	03/12/2015
//
//		03/12/2015	added PAGE_GET_SIZE to accessPage()
//
// **************************************************************************
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
#include "os345.h"
#include "os345lc3.h"

// ***********************************************************************
// mmu variables

// LC-3 memory
unsigned short int memory[LC3_MAX_MEMORY];

// statistics
int memAccess;						// memory accesses
int memHits;						// memory hits
int memPageFaults;					// memory faults

int getFrame(int);
int getAvailableFrame(void);
extern TCB tcb[];					// task control block
extern int curTask;					// current task #
int rptClockIndex;
int uptClockIndex;



void outputClock(){
    int rpt,upt;
    printf("Clock status:\n");
    for (rpt = 0; rpt < 64; rpt+=2)
    {
        if (MEMWORD(rpt+TASK_RPT) || MEMWORD(rpt+TASK_RPT+1))
        {
            printf((rptClockIndex == rpt)?">":" ");
            outPTE("  RPT  =", rpt+TASK_RPT);
            
            for(upt = 0; upt < 64; upt+=2)
            {
                if (DEFINED(MEMWORD(rpt+TASK_RPT)) &&
                    (DEFINED(MEMWORD((FRAME(MEMWORD(rpt+TASK_RPT))<<6)+upt))
                     || PAGED(MEMWORD((FRAME(MEMWORD(rpt+TASK_RPT))<<6)+upt+1))))
                {
                    printf((uptClockIndex == upt)?">":" ");
                    
                    outPTE("    UPT=", (FRAME(MEMWORD(rpt+TASK_RPT))<<6)+upt);
                }
            }
        }
    }

}
int advanceRootClock(){
    int i, rpta, rpte;
    int complete = 0;
    while (complete == 0 && i < 32){
        //advance the clock
        //printf("Advancing clock\n");
        rptClockIndex= (rptClockIndex+2)%64;
        rpta = tcb[curTask].RPT + rptClockIndex;
        rpte = MEMWORD(rpta);
        if (PINNED(rpte)){
            //printf("Pinned %x\n",rptClockIndex);
        }
        if (DEFINED(rpte)){
            //printf("Taking entry %x\n",rptClockIndex);
            complete = 1;
        }
        else{
            //printf("Skipping entry %x: %x\n",rptClockIndex,rpte);
        }
        i++;
    }
    return i;
}


//write the page out data
//returns the page number
int pageDataFrameOut(int upta){

    int upte1,upte2,pageNumber,frame;
    
    upte1 = MEMWORD(upta);
    upte2 = MEMWORD(upta+1);
    frame = FRAME(upte1);
    pageNumber = PAGE(upte2);
    
    
    if (DEFINED(upte1)){
        upte1 = CLEAR_FRAME(CLEAR_DEFINED(upte1));
        //if we already have a entry in our swap space and it's dirty
        if (DEFINED(upte2) && DIRTY(upte1)){
            pageNumber = accessPage(pageNumber, frame, PAGE_OLD_WRITE);
            printf("Paging old frame %d to page %d\n",frame,pageNumber);
            
        }
        //otherwise we have a new page write
        else if (!DEFINED(upte2)){
            pageNumber = accessPage(0, frame, PAGE_NEW_WRITE);
            upte2 = CLEAR_PAGE(upte2);
            upte2 = SET_PAGE(upte2,pageNumber);
            printf("Paging new frame %d to page %d\n",frame,pageNumber);
        }
        else{
            //we have a non dirty page already in swap space
            printf("Nothing to do to swap out\n");
        }
        
    }
    else{
        printf("Write out to swap failed for %x\n",upta);
        return -1;
    }
    
    //update the user page table entry
    MEMWORD(upta) = CLEAR_DIRTY(upte1);
    MEMWORD(upta+1) =  SET_PAGED(upte2);
    
    return pageNumber;
}

//returns a true if viable
//returns a false if not viable
int checkRootEntryClock(int index, int notme, int mark){
    int rpta = tcb[curTask].RPT + index;
    int rpte1 = MEMWORD(rpta);
    //make sure we count this access
    memAccess++;
    //check whether it's in memory
    
    if (PINNED(rpte1)){
       // printf("%x is pinned\n",index);
        return 0;
    }
    else if (!DEFINED(rpte1))	{ // rpte defined
        //printf("%x is undefined \n",index);
        return 0;
    }
    else if (REFERENCED(rpte1)){ //if it's in use
        if (mark) MEMWORD(rpta) = rpte1 = CLEAR_REF(rpte1);
        //printf("%x is referenced\n",index);
        return 0;
    }
    //printf("%x:%x is not pinned or referenced: %x\n",index,rpta,rpte1);
    //otherwise it's not referenced and good to be used
    return 1;
    
}

//this function gets the first available frame
//sends stuff out to swap space if we need to
int getFrame(int notme)
{
    static TID lastTask = -1;
    int rpta,rpte,upt,upta,upte,rpt;
    
	int frame;
	frame = getAvailableFrame();
	if (frame >=0) return frame;

	// run clock
    
    //check if we've changed tasks
    if (lastTask != curTask){
        lastTask = curTask;
        rptClockIndex = 0;
        uptClockIndex = 0;
    }

    outputClock();

    //advance clock
	
    outputClock();
    
    
    //exit(1);

	return frame;
}

// **************************************************************************
// **************************************************************************
// LC3 Memory Management Unit
// Virtual Memory Process
// **************************************************************************
//           ___________________________________Frame defined
//          / __________________________________Dirty frame
//         / / _________________________________Referenced frame
//        / / / ________________________________Pinned in memory
//       / / / /     ___________________________
//      / / / /     /                 __________frame # (0-1023) (2^10)
//     / / / /     /                 / _________page defined
//    / / / /     /                 / /       __page # (0-4096) (2^12)
//   / / / /     /                 / /       /
//  / / / /     / 	             / /       /
// F D R P - - f f|f f f f f f f f|S - - - p p p p|p p p p p p p p

#define MMU_ENABLE	TRUE

unsigned short int *getMemAdr(int va, int rwFlg)
{
	int rpta, rpte1, rpte2;
	int upta, upte1, upte2;
	int dataFrame, uptFrame;
    
    // turn off virtual addressing for system RAM
	if (va < 0x3000) return &memory[va];

    memAccess++;
	rpta = tcb[curTask].RPT + RPTI(va);		// root page table address
	rpte1 = memory[rpta];					// FDRP__ffffffffff
	rpte2 = memory[rpta+1];					// S___pppppppppppp
    if (DEFINED(rpte1))	{ // rpte defined
        //rpte defined
        memHits++;
        rpte1 = SET_DIRTY(rpte1);
    }
    else{ // rpte undefined
        // 1. get a UPT frame from memory (may have to free up frame)
        // 2. if paged out (DEFINED) load swapped page into UPT frame
        // else initialize UPT
        uptFrame = getFrame(-1);
        rpte1 = SET_DEFINED(uptFrame);
        if (PAGED(rpte2))	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
        {
            memPageFaults++;
            accessPage(SWAPPAGE(rpte2), uptFrame, PAGE_READ);
        }
        else	// define new upt frame and reference from rpt
        {	rpte1 = SET_DIRTY(rpte1);
            rpte2 = 0;
            // undefine all upte's
        }
    
    }
    //get the frame number
    uptFrame = FRAME(rpte1);
    
    //update the root page table entries
    //MEMWORD(rpta) = rpte1 = SET_REF(SET_PINNED(rpte1));	// set rpt frame access bit
    MEMWORD(rpta) = rpte1 = SET_REF(rpte1);	// set rpt frame access bit
    MEMWORD(rpta+1) = rpte2;
    
    //User page table entry
    memAccess++;
    upta = (FRAME(rpte1)<<6) + UPTI(va);
    upte1 = MEMWORD(upta);
    upte2 = MEMWORD(upta+1);
    if (DEFINED(upte1)){	// upte defined
        memHits++;
        upte1 = SET_DIRTY(upte1);
    }
    else {	// upte undefined

        // 1. get a physical frame (may have to free up frame) (x3000 - limit) (192 - 1023)
        dataFrame = getFrame(uptFrame); //but not the root page table
        upte1 = SET_DEFINED(dataFrame);
        // 2. if paged out (DEFINED) load swapped page into physical frame
        if (PAGED(upte2))	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
        {
            memPageFaults++;
            printf("Paging page %d in to page:%d,%x\n",SWAPPAGE(upte2),dataFrame,dataFrame<<6);
            accessPage(SWAPPAGE(upte2), dataFrame, PAGE_READ);
        }
        else	// define new upt frame and reference from rpt
        {
            upte2 = 0;
            printf("Creating data frame %d,%x for VA: %x\n",dataFrame,dataFrame<<6, va);
            // undefine all upte's
        }
        
        if (dataFrame == 0){
            exit(-3);
        }

    }
    //update the user page table entry
    MEMWORD(upta) = upte1 = SET_DIRTY(SET_REF(upte1));	// set upt frame access bit
    MEMWORD(upta+1) = upte2;
    
    //printf("Getting memory address for VA: %x, RP: %x, RPTI: %d, UPTI: %02x, rwFlag:%d, Frame:%x, PA:%x\n",va,tcb[curTask].RPT,RPTI(va),UPTI(va),rwFlg,FRAME(upte1),(FRAME(upte1)<<6) + FRAMEOFFSET(va));
    
    //we know we have a hit here so get the memAccess and hits
    memAccess++;
    memHits++;
    
    return &memory[(FRAME(upte1)<<6) + FRAMEOFFSET(va)];	// return physical address}

} // end getMemAdr


// **************************************************************************
// **************************************************************************
// set frames available from sf to ef
//    flg = 0 -> clear all others
//        = 1 -> just add bits
//
void setFrameTableBits(int flg, int sf, int ef)
{	int i, data;
	int adr = LC3_FBT-1;             // index to frame bit table
	int fmask = 0x0001;              // bit mask

	// 1024 frames in LC-3 memory
	for (i=0; i<LC3_FRAMES; i++)
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;
			adr++;
			data = (flg)?MEMWORD(adr):0;
		}
		else fmask = fmask >> 1;
		// allocate frame if in range
		if ( (i >= sf) && (i < ef)) data = data | fmask;
		MEMWORD(adr) = data;
	}
	return;
} // end setFrameTableBits


// **************************************************************************
// get frame from frame bit table (else return -1)
int getAvailableFrame()
{
	int i, data;
	int adr = LC3_FBT - 1;				// index to frame bit table
	int fmask = 0x0001;					// bit mask

	for (i=0; i<LC3_FRAMES; i++)		// look thru all frames
	{	if (fmask & 0x0001)
		{  fmask = 0x8000;				// move to next work
			adr++;
			data = MEMWORD(adr);
		}
		else fmask = fmask >> 1;		// next frame
		// deallocate frame and return frame #
		if (data & fmask)
		{  MEMWORD(adr) = data & ~fmask;
			return i;
		}
	}
	return -1;
} // end getAvailableFrame



// **************************************************************************
// read/write to swap space
int accessPage(int pnum, int frame, int rwnFlg)
{
	static int nextPage;						// swap page size
	static int pageReads;						// page reads
	static int pageWrites;						// page writes
    int i;
	static unsigned short int swapMemory[LC3_MAX_SWAP_MEMORY];

	if ((nextPage >= LC3_MAX_PAGE) || (pnum >= LC3_MAX_PAGE))
	{
		printf("\nVirtual Memory Space Exceeded!  (%d)", LC3_MAX_PAGE);
		exit(-4);
	}
	switch(rwnFlg)
	{
		case PAGE_INIT:                    		// init paging
			memAccess = 0;						// memory accesses
			memHits = 0;						// memory hits
			memPageFaults = 0;					// memory faults
			nextPage = 0;						// disk swap space size
			pageReads = 0;						// disk page reads
            pageWrites = 0;						// disk page writes
			return 0;

		case PAGE_GET_SIZE:                    	// return swap size
			return nextPage;

		case PAGE_GET_READS:                   	// return swap reads
			return pageReads;

		case PAGE_GET_WRITES:                    // return swap writes
			return pageWrites;

		case PAGE_GET_ADR:                    	// return page address
            return (int)(&swapMemory[pnum<<6]);

		case PAGE_NEW_WRITE:                   // new write (Drops thru to write old)
			pnum = nextPage++;

		case PAGE_OLD_WRITE:                   // write
			//printf("\n    (%d) Write frame %d (memory[%04x]) to page %d", p.PID, frame, frame<<6, pnum);
			memcpy(&swapMemory[pnum<<6], &memory[frame<<6], 1<<7);
            
			pageWrites++;
			return pnum;

		case PAGE_READ:                    	// read
			//printf("\n    (%d) Read page %d into frame %d (memory[%04x])", p.PID, pnum, frame, frame<<6);
			memcpy(&memory[frame<<6], &swapMemory[pnum<<6], 1<<7);
			pageReads++;
			return pnum;

		case PAGE_FREE:                   // free page
			printf("\nPAGE_FREE not implemented");
			break;
   }
   return pnum;
} // end accessPage
