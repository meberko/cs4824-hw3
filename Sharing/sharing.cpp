#include <algorithm>
#include <assert.h>
#include <iostream>
#include <map>
#include "pin.H"
#include <set>
#include <stdio.h>
#include <stdint.h>

#define MAX_THREAD_ID   32
#define BLOCK_MASK      ~((long long)63)
#define WORD_MASK       (long long)60
#define WORDS_PER_BLOCK (long long)16


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "sharing.out", "file name for falsely-shared cache block list");

PIN_MUTEX*                              addr_m = new PIN_MUTEX;
std::map< long long,std::set<int> >     tracker;
std::map< long long, int >              bit_tracker[MAX_THREAD_ID];
std::vector<long long>                  addrs = std::vector<long long>();
std::vector<long long>                  blocks = std::vector<long long>();

// This analysis routine is called on every memory reference.
VOID MemRef(THREADID tid, VOID* addr) {
    long long block = ((long long)addr)>>6;
    long long word  = ((long long)addr &  WORD_MASK)>>2;
    long long tracker_addr = (block<<4)|(word);
    assert(word==((((long long)addr)>>2)&15));
    assert(tracker_addr==((long long)addr)>>2);

    PIN_MutexLock(addr_m);
    // Check if block number has ever appeared before
    if(std::find(blocks.begin(), blocks.end(), block)==blocks.end()) blocks.push_back(block);
    // Check if this thread has touched this block before
    if(bit_tracker[tid].find(block)==bit_tracker[tid].end()) bit_tracker[tid][block] = 0;
    // Mark block bit string
    bit_tracker[tid][block] = bit_tracker[tid][block] | (1 << word);
/*
    // If this address hasn't been touched yet
    if(tracker.find(tracker_addr)==tracker.end()){
        tracker[tracker_addr] = std::set<int>();
        addrs.push_back(tracker_addr);
    }
    tracker[tracker_addr].insert(tid);
*/
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
    int falsely_shared = 0, truly_shared = 0, n_threads = 0;
    std::vector<long long>::iterator     block_it;
    // Iterate through blocks
    for(block_it=blocks.begin(); block_it!=blocks.end(); block_it++) {
        int t;
        std::vector<int> bit_strs = std::vector<int>();
        // Reset n_threads and truly shared
        n_threads = 0;
        truly_shared = 0;
        // For all possible thread ids
        for(t=0; t<MAX_THREAD_ID; t++) {
            // If this thread accessed this block
            if (bit_tracker[t].find(*block_it)!=bit_tracker[t].end()) {
                // Add to n_threads to keep track of n_threads touching block
                n_threads++;
                // Keep a vector of all bit strings we got
                bit_strs.push_back(bit_tracker[t][*block_it]);
            }
        }
        // Now we have a vector of bit strings. We have to go through and make
        // sure that none of them have the same bits set.
        int i,j;
        for(i=0; i<(int)bit_strs.size(); i++) {
            for(j=i+1; j<(int)bit_strs.size(); j++) {
                if((bit_strs[i] & bit_strs[j]) != 0) truly_shared = 1;
            }
        }
        if(n_threads > 1 && !truly_shared) falsely_shared++;
    }
/*
    int                                     falsesh = 0;
    std::set<long long>                     touched_this_block;
    std::map<long long, std::set<int> >     block_touched_by;
    std::map<long long, int >               block_true_shared;
    std::vector<long long>::iterator        addr;
    std::set<int>::iterator                 thread_it;
    std::vector<long long>::iterator        block_it;

    std::sort(addrs.begin(), addrs.end());

    // Iterate through addresses
    for(addr=addrs.begin(); addr!=addrs.end(); addr++) {
        long long block = (*addr)>>4;

        // If we do not have this block number stored
        // Initialize stuff
        if(std::find(blocks.begin(), blocks.end(), block)==blocks.end()){
            blocks.push_back(block);
            block_true_shared[block] = 0;
            block_touched_by[block] = std::set<int>();
        }

        // If block is truely shared, mark it truly shared
        if((int)tracker[*addr].size()>1) block_true_shared[block] = 1;

        // Iterate through threads that accessed this word
        // Add them to the threads which touched this block
        for(thread_it=tracker[*addr].begin(); thread_it!=tracker[*addr].end(); thread_it++) block_touched_by[block].insert(*thread_it);
    }

    // Iterate through blocks
    for(block_it=blocks.begin();block_it!=blocks.end();block_it++) {
        if(!block_true_shared[*block_it]){
            printf("Block: %p\n", (void*)(*block_it));
            for(thread_it=block_touched_by[*block_it].begin(); thread_it!=block_touched_by[*block_it].end(); thread_it++) printf("\tThread: %d\n",*thread_it);
        }
        if(block_true_shared[*block_it]==0 && block_touched_by[*block_it].size()>1) falsesh++;
    }
*/
    printf("blocks size:\t%d\n", (int)blocks.size());
    printf("falsely shared:\t%d\n",falsely_shared);

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
