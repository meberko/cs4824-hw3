#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define NUM_WORKERS	  4
#define BYTES_PER_BLOCK   64
#define BLOCKS_PER_ARRAY  1000

#define INTS_PER_BLOCK    (BYTES_PER_BLOCK/sizeof(int))
#define INTS_PER_ARRAY    (INTS_PER_BLOCK * BLOCKS_PER_ARRAY)
#define BLOCKS_PER_CHUNK  (BLOCKS_PER_ARRAY / (NUM_WORKERS+1))
#define INTS_PER_CHUNK    (BLOCKS_PER_CHUNK * INTS_PER_BLOCK)

int* arr;

void *DoAccesses(void* t) {
  long tid = (long) t;
  int base_word = INTS_PER_CHUNK * tid + tid;
  int bound_word = base_word + 2 * INTS_PER_CHUNK;
  if (bound_word >= INTS_PER_ARRAY) {
    bound_word = INTS_PER_ARRAY;
  }
  int sum = 0;
  int i;
  for (i = base_word; i < bound_word; i++) {
    sum += arr[i];
  }
  printf("sum=%d\n", sum);
  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
   pthread_t threads[NUM_WORKERS];
   int rc;
   long t;
   
   arr = (int*) malloc(1000 * sizeof(int));
   assert(arr);

   for (t = 0; t < NUM_WORKERS; t++) {
     rc = pthread_create(&threads[t], NULL, DoAccesses, (void*) t);
     if (rc) {
       printf("ERROR; return code from pthread_create() is %d\n", rc);
       exit(-1);
     }
   }
   for (t = 0; t < NUM_WORKERS; t++) {
     pthread_join(threads[t],NULL);
   }

   printf("On this program, your pintool should report a sequence of roughly %d falsely shared blocks.\n", BLOCKS_PER_CHUNK * (NUM_WORKERS-1), NUM_WORKERS);


}
