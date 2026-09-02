#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

static char*
base_name(char *path)
{
  char *p;

  p = path + strlen(path);
  while(p > path && p[-1] != '/')
    p--;
  return p;
}

static void
find(char *path, char *target)
{
  char buf[512];
  char *p;
  int fd;
  struct dirent de;
  struct stat st;

  if(stat(path, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    return;
  }

  if(st.type != T_DIR){
    if(strcmp(base_name(path), target) == 0)
      printf("%s\n", path);
    return;
  }

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
    fprintf(2, "find: path too long: %s\n", path);
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  if(p == buf || p[-1] != '/')
    *p++ = '/';

  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;

    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
      continue;

    find(buf, target);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path name\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
