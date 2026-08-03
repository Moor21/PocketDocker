#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#define STACK_SIZE (1024*1024)
typedef struct{
    int fd[2];
    char *command;
    char *args[];
} Params;
void parse_args(int argc, char **argv, Params *params){
	printf("\nargc: %i\n", argc);
 	for(int i = 0; i < argc; i++){
	 printf("\nargv[%d]: %s\n",i, argv[i]);
	}	
       for (int i = 0; i < argc-1; i++){
	params->args[i] = argv[i+1];
       }
       for(int i = 0; i < argc -1;i++){
	       printf("\nparams->args[%d]: %s\n", i, params->args[i]);
       }
}
int nesquick(void *arg){
    if(prctl(PR_SET_PDEATHSIG, SIGKILL)){
        exit(1);
    }
    Params *params = (Params *)arg;
    printf("\nCommand for execution: %s\n", params->command);
    fflush(stdout);
    printf("\nChild pid: %i\n", getpid());
   umount2("/proc", MNT_DETACH); 
   usleep(100000);
   if(mount("proc", "/proc", "proc", 0, NULL ) < 0){
        printf("\nError: Mounting failed\n");
        exit(1);
   }
    char const *args[] = { "/bin/bash", "-c","ls","-la", NULL };
    execv( (char const *)args[0], (char * const *)args);
    perror("Execution failed");
    exit(EXIT_FAILURE); 
    return 1;
}
int main(int argc, char **argv){
    Params params;
    memset(&params, 0, sizeof(params));
    printf("\nNesquick\n");
    params.command = "Nesquick";
    if(pipe(params.fd)){
        printf("\nFailed to create PIPE!\n");
        exit(1);
    }
    parse_args(argc,argv,&params);
    char *nesquick_stack = malloc(STACK_SIZE);
   if(nesquick_stack == NULL){
       printf("\nMemory allocation error\n");
       exit(1);
   }
   char *nesquick_stack_top = nesquick_stack + STACK_SIZE;
   int flags = CLONE_NEWPID | SIGCHLD | CLONE_NEWNS;
   pid_t nesquick_pid = clone(nesquick, nesquick_stack_top, flags, &params);
   if(nesquick_pid == -1){
       printf("\nClone error\n");
       exit(1);
   }
   if(waitpid(nesquick_pid, NULL, 0) == -1){
       printf("\nChild has terminated\n");
       free(nesquick_stack);
       exit(0);
   }
    free(nesquick_stack);
    return 0;

}
