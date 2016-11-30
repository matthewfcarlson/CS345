// os345fat.c - file management system
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
//
//		11/19/2011	moved getNextDirEntry to P6
//
// ***********************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include "os345.h"
#include "os345fat.h"

// ***********************************************************************
// ***********************************************************************
//	functions to implement in Project 6
//
int fmsCloseFile(int);
int fmsDefineFile(char*, int);
int fmsDeleteFile(char*);
int fmsOpenFile(char*, int);
int fmsReadFile(int, char*, int);
int fmsSeekFile(int, int);
int fmsWriteFile(int, char*, int);

// ***********************************************************************
// ***********************************************************************
//	Support functions available in os345p6.c
//
extern int fmsGetDirEntry(char* fileName, DirEntry* dirEntry);
extern int fmsGetNextDirEntry(int *dirNum, char* mask, DirEntry* dirEntry, int dir);

extern int fmsMount(char* fileName, void* ramDisk);

extern void setFatEntry(int FATindex, unsigned short FAT12ClusEntryVal, unsigned char* FAT);
extern unsigned short getFatEntry(int FATindex, unsigned char* FATtable);

extern int fmsMask(char* mask, char* name, char* ext);
extern void setDirTimeDate(DirEntry* dir);
extern int isValidFileName(char* fileName);
extern void printDirectoryEntry(DirEntry*);
extern void fmsError(int);

extern int fmsReadSector(void* buffer, int sectorNumber);
extern int fmsWriteSector(void* buffer, int sectorNumber);

extern unsigned short getFatEntry(int FATindex, unsigned char* FATtable);

// ***********************************************************************
// ***********************************************************************
// fms variables
//
// RAM disk
unsigned char RAMDisk[SECTORS_PER_DISK * BYTES_PER_SECTOR];

// File Allocation Tables (FAT1 & FAT2)
unsigned char FAT1[NUM_FAT_SECTORS * BYTES_PER_SECTOR];
unsigned char FAT2[NUM_FAT_SECTORS * BYTES_PER_SECTOR];

char dirPath[128];							// current directory path
FDEntry OFTable[NFILES];					// open file table
unsigned int currOpenFiles = 0;

extern bool diskMounted;					// disk has been mounted
extern TCB tcb[];							// task control block
extern int curTask;							// current task #


// ***********************************************************************
// ***********************************************************************
// This function closes the open file specified by fileDescriptor.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
//	Return 0 for success, otherwise, return the error number.
//
int fmsCloseFile(int fileDescriptor)
{
	// ?? add code here
	printf("\nfmsCloseFile Not Implemented");
    
    if (OFTable[fileDescriptor].name[0] == 0) return ERR63;
    
    OFTable[fileDescriptor].name[0] = 0;
    
    OFTable[fileDescriptor].startCluster = 0;

	return 0;
} // end fmsCloseFile

// ***********************************************************************
// ***********************************************************************
// This function autcompletes the filename given with the first directory entry in the current directory
void fmsAutcompleteFile(char* filename){
    DirEntry dirEntry;
    int error, index = 0;
    uint16 cdir = tcb[0].cdir;
    printf("%d",cdir);
    error = fmsGetNextDirEntry(&index, filename, &dirEntry, cdir);
    printf("%d %d",error,index);
    printf("%s",dirEntry.name);
    
}



// ***********************************************************************
// ***********************************************************************
// If attribute=DIRECTORY, this function creates a new directory
// file directoryName in the current directory.
// The directory entries "." and ".." are also defined.
// It is an error to try and create a directory that already exists.
//
// else, this function creates a new file fileName in the current directory.
// It is an error to try and create a file that already exists.
// The start cluster field should be initialized to cluster 0.  In FAT-12,
// files of size 0 should point to cluster 0 (otherwise chkdsk should report an error).
// Remember to change the start cluster field from 0 to a free cluster when writing to the
// file.
//
// Return 0 for success, otherwise, return the error number.
//
int fmsDefineFile(char* fileName, int attribute)
{
	// ?? add code here
	printf("\nfmsDefineFile Not Implemented");
    
	return ERR72;
} // end fmsDefineFile



// ***********************************************************************
// ***********************************************************************
// This function deletes the file fileName from the current director.
// The file name should be marked with an "E5" as the first character and the chained
// clusters in FAT 1 reallocated (cleared to 0).
// Return 0 for success; otherwise, return the error number.
//
int fmsDeleteFile(char* fileName)
{
	// ?? add code here
	printf("\nfmsDeleteFile Not Implemented");

	return ERR61;
} // end fmsDeleteFile



// ***********************************************************************
// ***********************************************************************
// This function opens the file fileName for access as specified by rwMode.
// It is an error to try to open a file that does not exist.
// The open mode rwMode is defined as follows:
//    0 - Read access only.
//       The file pointer is initialized to the beginning of the file.
//       Writing to this file is not allowed.
//    1 - Write access only.
//       The file pointer is initialized to the beginning of the file.
//       Reading from this file is not allowed.
//    2 - Append access.
//       The file pointer is moved to the end of the file.
//       Reading from this file is not allowed.
//    3 - Read/Write access.
//       The file pointer is initialized to the beginning of the file.
//       Both read and writing to the file is allowed.
// A maximum of 32 files may be open at any one time.
// If successful, return a file descriptor that is used in calling subsequent file
// handling functions; otherwise, return the error number.
//
int fmsOpenFile(char* fileName, int rwMode)
{
    DirEntry dirEntry;
    char mask[20];
    int index = 0;
    int error = 0;
    
    if (!diskMounted)
    {
        fmsError(ERR72);
        return 0;
    }
    //check if filename is valid
    int valid_name = isValidFileName(fileName);
    if (valid_name == 0) return ERR50;
    
    //check if we already have too many files open
    if (currOpenFiles == NFILES) return ERR70;
    
    //Find directory entry
    strcpy(mask, fileName);
    error = fmsGetDirEntry(fileName, &dirEntry);
    if (error){
        return ERR61;
    }
    
    printf("Index: %x",index);
    
    //Check permission
    //if we're trying to write and it's read only
    if (dirEntry.attributes & READ_ONLY && rwMode != 0){
        return ERR83;
    }
    /*Invalid File Name”, “File Not Defined”, “File Already open”, “Too Many Files Open”, “File Space Full”*/
    
    //Create a channel (file slot, handle)
    
    //find the first open file slot
    int open_slot = -1;
    for (int fd = 0; fd <= NFILES; fd++)
    {
        if (OFTable[fd].name[0] == 0 && open_slot == -1){
            open_slot = fd;
        }
        //check if the file is already open
        //qif (OFTable[fd].name != 0 && )
    }
    if (open_slot == -1) return ERR70; //we didn't find a slot
    
    FDEntry* entry = &OFTable[open_slot];
    //set the attributes
    entry->startCluster = dirEntry.startCluster;
    entry->currentCluster = 0;
    entry->fileSize = (rwMode == 1)?0:dirEntry.fileSize;
    
    entry->pid = curTask;
    //string copy
    for (int i=0;i<8;i++) entry->name[i] = dirEntry.name[i];
    for (int i=0;i<3;i++) entry->extension[i] = dirEntry.extension[i];
    
    //other attributes
    entry->attributes = dirEntry.attributes;
    entry->mode = rwMode;
    entry->directoryCluster = CDIR;
    entry->fileIndex = (rwMode!=2)?0:dirEntry.fileSize;
    
    
    return open_slot;
} // end fmsOpenFile



// ***********************************************************************
// ***********************************************************************
// This function reads nBytes bytes from the open file specified by fileDescriptor into
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// After each read, the file pointer is advanced.
// Return the number of bytes successfully read (if > 0) or return an error number.
// (If you are already at the end of the file, return EOF error.  ie. you should never
// return a 0.)
//
int fmsReadFile(int fileDescriptor, char* buffer, int nBytes)
{
    int bytesOutput = 0;
    int bufferIndex;
    int error;
    int nextCluster;
    int bytesLeft;
    /*Errors
     “File Not Open”
     “Invalid File Descriptor”
     “End-of-File”
     “Illegal Access”
     Always reads from transaction buffer
     Watch out for sector boundaries
     Byte oriented (translates to cluster blocking)
     */
    FDEntry* fd = &OFTable[fileDescriptor];
    if (fd->name[0] == 0) return ERR52;
    
    //check that we aren't write or append
    if (fd->mode == 1 || fd->mode == 2) return ERR85;
    
    //load the nBytes
    while (nBytes > 0){
        //setup the next cluster
        nextCluster = fd->currentCluster;
        
        if (fd->fileSize == fd->fileIndex) //check if we reached the end of the file
            return bytesOutput ? bytesOutput : ERR66; //if so return an EOF or the number of bytes we've output
        bufferIndex = fd->fileIndex % BYTES_PER_SECTOR; //figure out where we are in the sector
        if (bufferIndex == 0 && (fd->fileIndex ||!fd->currentCluster)){ //if we're at a sector boundary
            if (fd->currentCluster == 0){ //load the first cluster
                if (fd->startCluster == 0) return ERR66;
                nextCluster = fd->startCluster;
                fd->fileIndex = 0;
            }
            if (fd->fileIndex != 0){ //otherwise we're loading the next cluster
                nextCluster = getFatEntry(fd->currentCluster, FAT1);
                if (nextCluster == FAT_EOC) return bytesOutput;
            }
            if (fd->flags & BUFFER_ALTERED){ //if we've altered the buffer
                //write it back out
                if ((error = fmsWriteSector(fd->buffer, C_2_S(fd->currentCluster)))){
                    return error;
                }
                fd->flags &= ~BUFFER_ALTERED; //clear the altered flag
            }
            if ((error = fmsReadSector(fd->buffer, C_2_S(nextCluster)))){
                return error;
            }
            //set our current cluster to the one we just read from
            fd->currentCluster = nextCluster;
            
        }
        
        bytesLeft = BYTES_PER_SECTOR - bufferIndex;
        if (bytesLeft > nBytes) bytesLeft = nBytes;
        if (bytesLeft > (fd->fileSize - fd->fileIndex))
            bytesLeft = fd->fileIndex - fd->fileIndex;
        
        //do the memory copy
        memcpy(buffer, &fd->buffer[bufferIndex], bytesLeft);
        
        fd->fileIndex += bytesLeft;
        buffer += bytesLeft;
        nBytes -= bytesLeft;
        bytesOutput += bytesLeft;
        
    }
    
    
	return bytesOutput;
} // end fmsReadFile



// ***********************************************************************
// ***********************************************************************
// This function changes the current file pointer of the open file specified by
// fileDescriptor to the new file position specified by index.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// The file position may not be positioned beyond the end of the file.
// Return the new position in the file if successful; otherwise, return the error number.
//
int fmsSeekFile(int fileDescriptor, int index)
{
	// ?? add code here
	printf("\nfmsSeekFile Not Implemented");

	return ERR63;
} // end fmsSeekFile



// ***********************************************************************
// ***********************************************************************
// This function writes nBytes bytes to the open file specified by fileDescriptor from
// memory pointed to by buffer.
// The fileDescriptor was returned by fmsOpenFile and is an index into the open file table.
// Writing is always "overwriting" not "inserting" in the file and always writes forward
// from the current file pointer position.
// Return the number of bytes successfully written; otherwise, return the error number.
//
int fmsWriteFile(int fileDescriptor, char* buffer, int nBytes)
{
	// ?? add code here
	printf("\nfmsWriteFile Not Implemented");
    
    

	return ERR63;
} // end fmsWriteFile
