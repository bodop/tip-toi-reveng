#include <stdlib.h>
#include <stdio.h>
#include <parser.h>

extern int tipparse(const char*);

int main(int argc,char** argv) {
  return tipparse(argv[1]);
}

