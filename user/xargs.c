#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXLINE 512

static int
run_line(int base_argc, char *base_argv[], char *line)
{
  char *args[MAXARG];
  char *p;
  int argc;
  int pid;

  argc = 0;
  while(argc < base_argc){
    args[argc] = base_argv[argc + 1];
    argc++;
  }

  p = line;
  while(*p){
    while(*p == ' ' || *p == '\t')
      p++;
    if(*p == 0)
      break;
    if(argc >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      return -1;
    }

    args[argc++] = p;
    while(*p && *p != ' ' && *p != '\t')
      p++;
    if(*p)
      *p++ = 0;
  }

  if(argc == base_argc)
    return 0;

  args[argc] = 0;
  pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    return -1;
  }
  if(pid == 0){
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  }

  wait(0);
  return 0;
}

int
main(int argc, char *argv[])
{
  char line[MAXLINE];
  char c;
  int n;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arguments ...]\n");
    exit(1);
  }
  if(argc - 1 >= MAXARG){
    fprintf(2, "xargs: too many initial arguments\n");
    exit(1);
  }

  n = 0;
  while(read(0, &c, 1) == 1){
    if(c == '\n' || c == '\r'){
      line[n] = 0;
      if(run_line(argc - 1, argv, line) < 0)
        exit(1);
      n = 0;
      continue;
    }

    if(n >= sizeof(line) - 1){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }
    line[n++] = c;
  }

  if(n > 0){
    line[n] = 0;
    if(run_line(argc - 1, argv, line) < 0)
      exit(1);
  }

  exit(0);
}
