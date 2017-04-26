#include <algorithm>
#include <iostream>
#include <map>
#include "pin.H"
#include <set>
#include <stdio.h>
#include <stdint.h>

#define MAX_THREAD_ID   32
#define BLOCK_MASK      ~((uintptr_t)63)
#define WORD_MASK       (uintptr_t)60
#define WORDS_PER_BLOCK (uintptr_t)16


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "sharing.out", "file name for falsely-shared cache block list");

PIN_MUTEX* addr_m = new PIN_MUTEX;
std::map< uintptr_t,std::set<int> > tracker;
std::vector<uintptr_t> addrs = std::vector<uintptr_t>();

// This analysis routine is called on every memory reference.
VOID MemRef(THREADID tid, VOID* addr) {
    VOID* block = (void*)(((uintptr_t)addr & BLOCK_MASK)>>6);
    VOID* word  = (void*)(((uintptr_t)addr &  WORD_MASK)>>2);
    uintptr_t tracker_addr = ((uintptr_t)block<<4)|((uintptr_t)word);
    PIN_MutexLock(addr_m);
    /*if(std::find(addrs.begin(),addrs.end(),tracker_addr) == addrs.end())*/
    // If this address hasn't been touched yet
    if(tracker.find(tracker_addr)==tracker.end()){
        tracker[tracker_addr] = std::set<int>();
        addrs.push_back(tracker_addr);
    }
    tracker[tracker_addr].insert(tid);
    PIN_MutexUnlock(addr_m);
}

// Note: Instrumentation function adapted from ManualExamples/pinatrace.cpp
// It is called for each Trace and instruments reads and writes
VOID Trace(TRACE trace, VOID *v) {
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            // For this instruction, get the number of operands!
            UINT32 memOperands = INS_MemoryOperandCount(ins);
            //cout << "memOperands=" << memOperands << endl;
            for (UINT32 memOp = 0; memOp < memOperands; memOp++){
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    //cout << "INS: " << INS_Disassemble(ins) << endl;
                    INS_InsertPredicatedCall(ins,
                            IPOINT_BEFORE,
                            (AFUNPTR)MemRef,
                    	    IARG_THREAD_ID,
                    	    IARG_MEMORYOP_EA,
                            memOp,
                    	    IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertPredicatedCall(ins,
                            IPOINT_BEFORE,
                            (AFUNPTR)MemRef,
                    	    IARG_THREAD_ID,
                    	    IARG_MEMORYOP_EA,
                            memOp,
                    	    IARG_END);
                }
            }
        }
    }
}

VOID Fini(INT32 code, VOID *v) {
    printf("Fini!\n");
    printf("addrs size: %d\n", (int)addrs.size());
    std::vector<uintptr_t>::iterator addr;
    std::sort(addrs.begin(), addrs.end());

    std::vector<uintptr_t> blocks = std::vector<uintptr_t>();
    int /*truesh_detected = 0,*/ falsesh = 0;
    //unsigned long currblock = 0;
    std::set<uintptr_t> touched_this_block;

    std::map< uintptr_t, std::set<int> > block_touched_by;
    std::map< uintptr_t, int > block_true_shared;

    // Iterate through addresses
    for(addr=addrs.begin(); addr!=addrs.end(); addr++) {
    /*
        // Get previous block
        unsigned long prevblock = currblock;
        // Get current block
        currblock = *it/WORDS_PER_BLOCK;
        int sameblock = (prevblock==currblock);
        //printf("BLOCK %lu, %d\n", *it, (int)tracker[*it].size());

        // True sharing detection, always runs
        if((int)tracker[*it].size()>1) truesh_detected = 1;

        // Check if still in same block
        if(sameblock) {
            std::set<int>::iterator sit;
            // If a thread has accessed this word
            if(!tracker[*it].empty() && (int)tracker[*it].size()==1) {
                for(sit=tracker[*it].begin(); sit!=tracker[*it].end(); sit++) {
                    touched_this_block.insert(*sit);
                }
            }
        }

        // New block
        else {
            //printf("BLOCK %lu, %d, %d\n", *it, (int)touched_this_block.size(), truesh_detected);
            //printf("%d\n",(int)touched_this_block.size());
            if(touched_this_block.size() > 1 && !truesh_detected) falsesh++;
            touched_this_block.clear();
            truesh_detected = 0;
        }
    */
        uintptr_t block = *addr/16;
        if(std::find(blocks.begin(), blocks.end(), block)==blocks.end()) blocks.push_back(block);
        // Initialize stuff
        if(block_true_shared.find(block)==block_true_shared.end()) block_true_shared[block] = 0;
        if(block_touched_by.find(block)==block_touched_by.end()) block_touched_by[block] = std::set<int>();

        // If block is truely shared
        if((int)tracker[*addr].size()>1) block_true_shared[block] = 1;
        std::set<int>::iterator sit;

        // Iterate through threads that accessed this word
        // Add them to the threads which touched this block
        for(sit=tracker[*addr].begin(); sit!=tracker[*addr].end(); sit++) block_touched_by[block].insert(*sit);
    }
    std::vector<uintptr_t>::iterator bit;
    for(bit=blocks.begin();bit!=blocks.end();bit++) {
        if(!block_true_shared[*bit] && block_touched_by[*bit].size()>1) falsesh++;
    }

    printf("Falsely Shared: %d\n",falsesh);

}

INT32 Usage(){
    PIN_ERROR("This Pintool prints list of falsely-shared cache blocks\n"
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

int main(int argc, char *argv[])
{
    PIN_MutexInit(addr_m);

    if (PIN_Init(argc, argv)) return Usage();

    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
