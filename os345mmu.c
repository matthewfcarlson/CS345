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
#define MMU_DEBUG_CLOCK 1
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

extern jmp_buf reset_context;

int rptClock = 0;
int uptClock = 0;

//outputs the RPT entries and their UPT entries
void outputPageTables(int tid){
    int rpt,upt;
    if (tid == -1){
        for (int i=1;i<32;i++) {
            if (tcb[i].name == 0) continue;
            outputPageTables(i);
        }
        return;
    }

    printf("\nClock status for tid: %d\n",tid);
    for (rpt = 0; rpt < 64; rpt+=2)
    {
        if (MEMWORD(rpt+tcb[tid].RPT) || MEMWORD(rpt+tcb[tid].RPT+1))
        {
            int RPT_addr = (rpt/2)<< 11;
            
            
            outPTE(" RPT=  ", rpt+tcb[tid].RPT);
            printf((rptClock== rpt)?"<":" ");
            printf("(%0x to %0x)",RPT_addr, RPT_addr + (1<<11)-1);
            
            for(upt = 0; upt < 64; upt+=2)
            {
                
                if (DEFINED(MEMWORD(rpt+tcb[tid].RPT)) &&
                    (DEFINED(MEMWORD((FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt))
                     || PAGED(MEMWORD((FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt+1))))
                {
                    
                    
                    outPTE("  UPT=", (FRAME(MEMWORD(rpt+tcb[tid].RPT))<<6)+upt);
                    printf((rptClock == rpt && uptClock== upt)?"<":" ");
                    printf("(%0x to %0x)",RPT_addr + ((upt/2)<<6), RPT_addr + ((upt/2+1)<<6)-1);
                    
                }
            }
        }
    }
    printf("\n");
    
}

//returns a true if viable
//returns a false if not viable
int checkPageTableEntry(int index,int mark){
    int rpta = index;
    int rpte1 = MEMWORD(rpta);
    int upta;
    
    //make sure we count this access
    memAccess++;
    //check whether it's in memory
    if (!DEFINED(rpte1))	{ // rpte defined
        //printf("%x is undefined \n",index);
        return 0;
    }

    
    //check if it's pinned
    if (PINNED(rpte1)){
        return 0;
    }
    else if (REFERENCED(rpte1)){ //if it's in use
        //make it and
        if (mark) {
            MEMWORD(rpta) = CLEAR_REF(rpte1);
            if (rpta < 0x3000){ //check it if's a root page entry
                upta = FRAME(rpta)<<6;
                for (int i=0;i<64;i+=2){
                    MEMWORD(upta+i) = CLEAR_REF(MEMWORD(upta+i));
                }
            }
        }
        
        return 0;
    }
    //otherwise it's not referenced and good to be used
    return 1;
    
}

//advance the root clock
void advanceRootClock(){
    rptClock += 2;
    if (rptClock >= 64) rptClock = 0;
}
//runs the clock and returns the page table entry to the frame to swap
#define MAX_CLOCK_COUNT 90
int runClock(int notme, int tid){
    if (tid < 0 || tid > 32) return -1;
    if (tcb[tid].name == 0) return -1;
#if MMU_DEBUG_CLOCK == 1
    printf("\nRunning clock for Tid %d",tid);
#endif
    int rpta, upta, clockCount,pte,rpte;
    int properIndex = -1;
    int framesInMemory = 0;
    //check the root page table entry
    rpta = tcb[tid].RPT + rptClock;
    clockCount = 0;
    
    //figure out the user page table to take a frame from
    if (checkPageTableEntry(rpta, 1) == 0) { //check it and mark it
        //advance the clock
        advanceRootClock();
        rpta = tcb[tid].RPT + rptClock;
        while (checkPageTableEntry(rpta, 1) == 0 && clockCount < MAX_CLOCK_COUNT){
            advanceRootClock();
            rpta = tcb[tid].RPT + rptClock;
            clockCount++;
            //printf("\n%x is no good",rpta);
        }
    }
    //check if we overran
    if (clockCount == MAX_CLOCK_COUNT || checkPageTableEntry(rpta, 1) == 0){
        
        return -2;
    }
    //get the frame table address
    rpta = tcb[tid].RPT + rptClock;
    rpte = MEMWORD(rpta);
    upta = FRAME(rpte)<<6;
    
    advanceRootClock();
    
    //figure out a frame from the user page table or if it's empty then unload that
   
    
    //check each index in the user page table
    for (int i=0;i<64 && properIndex == -1 ;i+=2){
        //get the entry
        pte = MEMWORD(upta + i);
        if (DEFINED(pte)){ //if it's defined
            framesInMemory++;
            if (REFERENCED(pte)){ //if it's referenced, mark it as good
                pte = CLEAR_REF(pte);
                MEMWORD(upta+i) = pte;
            }
            else{
                properIndex = i;
            }
        }
    }
    
    //check if we can page out the user page table
    if (framesInMemory == 0 && FRAME(rpte) != notme){
        //we can page out the user page table
        return rpta;
    }
    
    if (properIndex == -1){
        //check each index
        for (int i=0;i<64 && properIndex == -1;i+=2){
            //get the entry
            pte = MEMWORD(upta + i);
            if (DEFINED(pte) && !REFERENCED(pte)){ //if it's defined
                properIndex = i;
            }
            
        }
    }
    
    //if we still have a bogus address we failed so try again
#if MMU_DEBUG_CLOCK == 1
    if (properIndex == -1){
        printf("No user page table entry is good");
        return -1;
    }
    else{
        printf("\nFound %x",upta+properIndex);
    }
#endif
    return upta+properIndex;
    
}


#define MAX_CLOCK_RUNS 3
int getFrame(int notme)
{
    int frame,clockCount,pageNumber,pte,pte2,entryAddress;
    frame = getAvailableFrame();
    if (frame >=0) return frame;
    
    // run clock
#if MMU_DEBUG_CLOCK == 1
    printf("\nWe're toast!!!!!!!!!!!!");
    outputPageTables(-1);
#endif
    entryAddress = 0;
    clockCount = 0;
    
    while (entryAddress <= 0 && clockCount < MAX_CLOCK_RUNS){
        entryAddress = runClock(notme,curTask);
        clockCount++;
    }
    if (clockCount == MAX_CLOCK_RUNS || entryAddress <= 0){
        
        //check other tasks to release tasks
        int tid = 0;
        while (entryAddress <= 0 && tid <= 32){
            tid++;
            if (tid == curTask)
                entryAddress = runClock(notme, tid);
            else
                entryAddress = runClock(-1, tid);
        }
        
    }
    
    if (entryAddress <= 0){
        int tid = 0;
        while (entryAddress <= 0 && tid <= 32){
            tid++;
            if (tid == curTask)
                entryAddress = runClock(notme, tid);
            else
                entryAddress = runClock(-1, tid);
        }
    }
    if (entryAddress <= 0){
        printf("Error: unable to find a frame to replace this one clock:%d address:%x\n",clockCount,entryAddress);
        outputPageTables(-1);
        longjmp(reset_context, POWER_DOWN_ERROR);
    }
    
    pte = MEMWORD(entryAddress);
    pte2 = MEMWORD(entryAddress+1);
    pageNumber = SWAPPAGE(pte2);
    
    if (!DEFINED(pte)){
        printf("\nError: bad address %x returned by clock with data %x\n",entryAddress,pte);
        outputPageTables(-1);
        
    }
    
    frame = FRAME(pte);
    
    //if we have a previous paged out entry
    if (PAGED(pte2)){ //&& DIRTY(upte1) ){
        pageNumber = accessPage(pageNumber, frame, PAGE_OLD_WRITE);
        //if (MMUdebugMode||DEBUG_SWAP) printf("Paging old frame %d to page %d\n",frame,pageNumber);
        
    }
    //otherwise we have a new page write
    else if (!DEFINED(pte2)){
        pageNumber = accessPage(0, frame, PAGE_NEW_WRITE);
        //if (MMUdebugMode||DEBUG_SWAP) printf("Paging new frame %d to page %d\n",frame,pageNumber);
    }
    
    //set the entry to paged out
    MEMWORD(entryAddress) = 0;
    MEMWORD(entryAddress+1) =  SET_PAGED(pageNumber);
    
    if (frame < 192){
        printf("\nError: bad frame %d from %x returned by clock\n",frame,entryAddress);
        outputPageTables(-1);
        longjmp(reset_context, POWER_DOWN_ERROR);
    }
#if MMU_DEBUG_CLOCK == 1
    outputPageTables(-1);
    printf("\n--------------");
#endif
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


unsigned short int *getMemAdr(int va, int rwFlg)
{
    unsigned short int pa;
    int rpta, rpte1, rpte2;
    int upta, upte1, upte2;
    int dataFrame, uptFrame;
    
    // turn off virtual addressing for system RAM
    if (va < 0x3000) return &memory[va];

    rpta = tcb[curTask].RPT + RPTI(va);		// root page table address
    rpte1 = memory[rpta];					// FDRP__ffffffffff
    rpte2 = memory[rpta+1];					// S___pppppppppppp
    if (DEFINED(rpte1))	{ // rpte defined
        memHits++;
        if (rwFlg != 0) rpte1 = SET_DIRTY(rpte1);
    }
    else { // rpte undefined
        uptFrame = getFrame(-1);
        rpte1 = SET_DEFINED(uptFrame);
        if (PAGED(rpte2)){	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
            memPageFaults++;
            accessPage(SWAPPAGE(rpte2), uptFrame, PAGE_READ);
        }
        else	// define new upt frame and reference from rpt
        {
            rpte1 = SET_DIRTY(rpte1);  rpte2 = 0;
            for (int i=0;i<64;i++){
                MEMWORD((uptFrame<<6) + i) = 0;
            }
            // undefine all upte's
        }

    }
    memory[rpta] = SET_REF(rpte1);			// set rpt frame access bit
    uptFrame = FRAME(rpte1);
    
    if (uptFrame < 192 || uptFrame > 193){
        printf("\nERROR: bad user page table frame %d",uptFrame);
        outputPageTables(-1);
        longjmp(reset_context, POWER_DOWN_ERROR);
    }
    
    upta = (FRAME(rpte1)<<6) + UPTI(va);	// user page table address
    upte1 = memory[upta]; 					// FDRP__ffffffffff
    upte2 = memory[upta+1]; 				// S___pppppppppppp
    if (DEFINED(upte1))	{                   // upte defined
        memHits++;
    }
    else{				// upte undefined
        dataFrame = getFrame(uptFrame);
        upte1 = SET_DEFINED(dataFrame);
        if (PAGED(upte2)){	// UPT frame paged out - read from SWAPPAGE(rpte2) into frame
            memPageFaults++;
            accessPage(SWAPPAGE(upte2), dataFrame, PAGE_READ);
        }
        else	// define new upt frame and reference from rpt
        {
            upte1 = SET_DIRTY(upte1);  upte2 = 0;
            for (int i=0;i<64;i++){
                MEMWORD((dataFrame<<6) + i);
            }
            // undefine all upte's
        }
    }
    memory[upta] = SET_REF(upte1); 			// set upt frame access bit
    return &memory[(FRAME(upte1)<<6) + FRAMEOFFSET(va)];

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
