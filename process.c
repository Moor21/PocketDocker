#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/prctl.h>
#include <pty.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#define MAX_EVENTS 10
#define PTY_BUFFER_SIZE 1024
#define STACK_SIZE (1024 * 1024)
typedef struct {
  int fd[2];
  char *command;
  int argc;
  int *pty_master;
  int *pty_slave;
  char **args;

} Params;

char *str_join(char *delimiter, char *words[], int count) {
  if (count <= 0) {
    printf("\nCount is not allowed!\n");
    char *result = malloc(sizeof(char *));
    if (result == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    result[0] = '\0';
    return result;
  }
  size_t delimiter_length = strlen(delimiter);
  size_t total_length = 0;
  int delimiter_count = count - 1;
  for (int i = 0; i < count; i++) {
    total_length += strlen(words[i]);
  }
  printf("\nTotal length: %i\n", total_length);
  printf("\nDelimiter length: %i\n", delimiter_length);

  total_length = total_length + (delimiter_length * delimiter_count) + 1;
  printf("\nCount: %i\n", count);
  printf("\nTotal length with delimiters: %li\n", total_length);

  char *result = (char *)malloc(total_length);
  if (result == NULL) {
    perror("Memory allocation error");
    exit(1);
  }
  result[0] = '\0';
  for (int i = 0; i < count; i++) {
    strcat(result, words[i]);
    if (i < count - 1) {
      strcat(result, delimiter);
    }
  }
  printf("\nResult: %s\n", result);
  return result;
}

void parse_args(int argc, char **argv, Params *params) {
  if (argc == 1) {
    params->argc = 0;
    params->args = malloc(sizeof(char *));
    return;
  }
  printf("\nargc: %i\n", argc);
  for (int i = 0; i < argc; i++) {
    printf("\nargv[%d]: %s\n", i, argv[i]);
  }
  params->argc = argc - 1;
  params->args = calloc((size_t)params->argc + 2, sizeof(*params->args));
  if (params->args == NULL) {
    perror("Memory allocation failed");
    exit(1);
  }
  for (int i = 0; i < params->argc; i++) {
    params->args[i] = argv[i + 1];
  }
  params->args[params->argc] = NULL;
  for (int i = 0; i < params->argc + 1; i++) {
    printf("\nparams->args[%d]: %s\n", i, params->args[i]);
  }
}

int nesquick(void *arg) {

  if (prctl(PR_SET_PDEATHSIG, SIGKILL)) {
    exit(1);
  }
  setsid();
  Params *params = (Params *)arg;
  printf("\nCommand for execution: %s\n", params->command);
  fflush(stdout);
  printf("\nChild pid: %i\n", getpid());
  // Вот тут решение общего mount, получается что проблема была в том, что он до
  // этого был share, короче, теперь в новом mount namespace он приватный, из за
  // чего это не касается общего хоста
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
    perror("MS_PRIVATE is failed");
    exit(1);
  }
  umount2("/proc", MNT_DETACH);
  if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
    printf("\nError: Mounting failed\n");
    exit(1);
  }
  if (params->argc == 0) {
    printf("\nThere are no arguments passed!\n");
    exit(1);
  }
  // parent pty
  printf("\nParent pty_master: %li\n", *(params->pty_master));
  printf("\nParent pty_slave: %li\n", *(params->pty_slave));

  char *pty_name = ttyname(*(params->pty_slave));
  printf("\npty_master: %i\n", *(params->pty_master));
  printf("\npty_slave: %i\n", *(params->pty_slave));
  printf("\npty_slave_name: %s\n", pty_name);

  // making pty controlling terminal for the process
#ifdef TIOCSCTTY
  ioctl(*(params->pty_slave), TIOCSCTTY, 0);
#endif

  dup2(*(params->pty_slave), STDIN_FILENO);
  dup2(*(params->pty_slave), STDOUT_FILENO);
  dup2(*(params->pty_slave), STDERR_FILENO);

  if (*(params->pty_slave) > 2) {
    close(*(params->pty_slave));
  }
  execvp((char *const)params->args[0], (char *const *)params->args);
  perror("Execution failed");
  exit(EXIT_FAILURE);
  return 1;
}
int main(int argc, char **argv) {
  Params params;
  memset(&params, 0, sizeof(params));
  printf("\nNesquick\n");
  params.command = "Nesquick";
  if (pipe(params.fd)) {
    printf("\nFailed to create PIPE!\n");
    exit(1);
  }
  parse_args(argc, argv, &params);
  char *nesquick_stack = malloc(STACK_SIZE);
  if (nesquick_stack == NULL) {
    printf("\nMemory allocation error\n");
    exit(1);
  }
  // pty
  params.pty_master = malloc(sizeof(int));
  params.pty_slave = malloc(sizeof(int));
  if (openpty(params.pty_master, params.pty_slave, NULL, NULL, NULL) < 0) {
    perror("openpty");
    exit(-1);
  }
  // pty_master non-blocking
  int pty_flags = fcntl(*(params.pty_master), F_GETFL);
  if (pty_flags < 0) {
    perror("flags");
    exit(-1);
  }
  pty_flags = pty_flags | O_NONBLOCK;
  if (fcntl(*(params.pty_master), F_SETFL, pty_flags) < 0) {
    perror("NONBLOCK");
    exit(-1);
  }
  int stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
  if (stdin_flags < 0){
      perror("stdin_flags");
      exit(-1);
  }
  stdin_flags = stdin_flags | O_NONBLOCK;
  if(fcntl(STDIN_FILENO, F_SETFL, stdin_flags) < 0){
    perror("STDIN NONBLOCK");
    exit(-1);
  }

  char *nesquick_stack_top = nesquick_stack + STACK_SIZE;
  int flags = CLONE_NEWPID | SIGCHLD | CLONE_NEWNS;
  pid_t nesquick_pid = clone(nesquick, nesquick_stack_top, flags, &params);
  if (nesquick_pid == -1) {
    printf("\nClone error\n");
    exit(1);
  }
  int status;
  // pty read and write
  // epoll
  close(*(params.pty_slave));
  int epfd, nfds;
  struct epoll_event ev, ev_stdin, events[MAX_EVENTS];
  epfd = epoll_create(1);
  if (epfd < 0) {
    perror("epoll");
    exit(-1);
  }

  char pty_buffer[PTY_BUFFER_SIZE] = {0};
  ssize_t bytes_read;

 

  ev.events = EPOLLIN;
  ev.data.fd = *(params.pty_master);

  ev_stdin.events = EPOLLIN;
  ev_stdin.data.fd = STDIN_FILENO;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, *(params.pty_master), &ev) < 0) {
    perror("epoll_ctl");
    exit(-1);
  }
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev_stdin) < 0) {
    perror("epoll_ctl STDIN");
    exit(-1);
  }
  while (1) {
    pid_t pid;
    pid = waitpid(nesquick_pid, &status, WNOHANG);
    if (pid == -1) {
      perror("child process");
      free(nesquick_stack);
      free(params.args);
      exit(0);
    } else if (pid == nesquick_pid) {
      printf("\nChild has terminated!\n");
      printf("\nStatus: %i\n", status);
      if (WIFEXITED(status)) {
        printf("\nChild has terminated normally\n");
      } else {
        perror("child error");
      }
      break;
    } else if (pid == 0) {
      nfds = epoll_wait(epfd, events, MAX_EVENTS, 100);
      if (nfds < 0) {
        perror("nfds");
        exit(-1);
      }
      for (int n = 0; n < nfds; n++) {
        if (events[n].data.fd == *(params.pty_master)) {
          printf("\nReady!\n");
          bytes_read =
              read(*(params.pty_master), pty_buffer, PTY_BUFFER_SIZE);
          if (bytes_read > 0) {
            write(STDOUT_FILENO, pty_buffer, bytes_read);
          }
          if(bytes_read < 0){
              if(errno  != EAGAIN && errno != EWOULDBLOCK && errno != EINTR){
                    if(errno != EIO){
                        perror("pty_master read");
                        break;
                    }
                
                 }
                 perror("read: ");
              }

        } else if (events[n].data.fd == STDIN_FILENO) {
          printf("\nSTDIN is ready!\n");
          char std_buffer[PTY_BUFFER_SIZE];
          ssize_t n = read(STDIN_FILENO, std_buffer, PTY_BUFFER_SIZE);
          if (n>0){
                ssize_t written = 0;
                while(written < n){
                    ssize_t w = write(*(params.pty_master), std_buffer+written, n - written);
                    if(w < 0){
                        if(errno == EINTR){
                            continue;
                        }
                        perror("pty_master write");
                        break;
                     }
                    written = written + w;  
              }
          }
        }
      }
    }
  }

  free(nesquick_stack);
  free(params.args);
  return 0;
}
