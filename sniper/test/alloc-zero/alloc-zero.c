#include <stdint.h>
#include <stdio.h>

#include "sim_api.h"

int main() {
  SimSetThreadName("main");

  __attribute__((align(512))) uint32_t a[4][16];
  uint32_t i, j;

  printf("A pointer %p %p %p %p\n", &a[0][0], &a[1][0], &a[2][0], &a[3][0]);

  SimRoiStart(); 

  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 16; ++j) {
      a[i][j] = 0xdeadbeef;
    }
  }


  __builtin___clear_cache(a, &a[3][15]); 

  uint32_t sum = 0;
  uint32_t val = 0;
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 16; ++j) {
      sum += a[i][j];
      val += 1;
    }
  }
  SimRoiEnd();

  printf("HERE val is %u, sum is %u", val, sum);
  
  return 0;
}
