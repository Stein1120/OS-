#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
sieve(int input)
{
  int prime;
  int value;
  int next[2];
  int pid;

  if(read(input, &prime, sizeof(prime)) != sizeof(prime)){
    close(input);
    exit(0);
  }

  printf("prime %d\n", prime);

  if(pipe(next) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(input);
    close(next[1]);
    sieve(next[0]);
  }

  close(next[0]);
  while(read(input, &value, sizeof(value)) == sizeof(value)){
    if(value % prime != 0 &&
       write(next[1], &value, sizeof(value)) != sizeof(value)){
      fprintf(2, "primes: write failed\n");
      exit(1);
    }
  }

  close(input);
  close(next[1]);
  wait(0);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int first[2];
  int pid;
  int value;

  if(argc != 1){
    fprintf(2, "usage: primes\n");
    exit(1);
  }

  if(pipe(first) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(first[1]);
    sieve(first[0]);
  }

  close(first[0]);
  for(value = 2; value <= 35; value++){
    if(write(first[1], &value, sizeof(value)) != sizeof(value)){
      fprintf(2, "primes: write failed\n");
      exit(1);
    }
  }
  close(first[1]);
  wait(0);
  exit(0);
}
