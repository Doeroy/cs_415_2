#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include "string_parser.h"
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#define MAX_LENGTH 1000


int main(int argc, char * argv[]){
    FILE * open_file;
    int num_children_launched;
    char *line = NULL;
    size_t len = 0;

    if(argc != 2){
        fprintf(stderr, "Must have two arguments, recieved: %d", argc);
        return 1;	
    }
 
    open_file = fopen(argv[1], "r");
    if(open_file == NULL){
        perror("Error opening file!");
        return 1;
    }
    num_children_launched = 0;
    pid_t pid_arr[MAX_LENGTH];
   
    while(getline(&line, &len, open_file) != -1){
        command_line token_buffer = str_filler(line, " ");
        if (num_children_launched >= MAX_LENGTH) {
            fprintf(stderr, "PARENT: Maximum number of commands (%d) reached. Skipping: %s\n", MAX_LENGTH, line);
            free_command_line(&token_buffer); 
            continue; 
        }

        pid_arr[num_children_launched] = fork();
        if(pid_arr[num_children_launched] == 0){
            printf("CHILD (PID: %d): process created\n", getpid());
            sigset_t set;
            int received_signal;

            if(sigemptyset(&set) != 0){
                perror("CHILD (PID: %d): sigemptyset() failed");
                free_command_line(&token_buffer); 
                _exit(1);
            }

            if(sigaddset(&set, SIGUSR1) != 0){
                if(errno == EINVAL){
                    perror("CHILD: sigaddset(SIGUSR1) failed");
                    free_command_line(&token_buffer); // Free before exit
                    _exit(1);
                }
            }

            if(sigprocmask(SIG_BLOCK, &set, NULL) == -1){
                perror("CHILD: sigprocmask failed to block SIGUSR1");
                free_command_line(&token_buffer); 
                _exit(1);
            }

            int sigwait_ret= sigwait(&set, &received_signal);
            if(sigwait_ret != 0){
                fprintf(stderr, "CHILD (PID: %d): sigwait() failed with error %d.\n", getpid(), sigwait_ret);
                free_command_line(&token_buffer);
                _exit(1);
            }

            if (received_signal == SIGUSR1) {
                printf("CHILD (PID: %d): Received SIGUSR1. Resuming and attempting to execvp().\n", getpid());
                fflush(stdout);
            } else {
                fprintf(stderr, "CHILD (PID: %d): Received unexpected signal %d via sigwait instead of SIGUSR1.\n", getpid(), received_signal);
                free_command_line(&token_buffer); // Free before exit
                _exit(1);
            }

            if(execvp(token_buffer.command_list[0], token_buffer.command_list) == -1){
		        perror("Error with execvp()");
                free_command_line(&token_buffer);
                _exit(1);
	        }

        }
        else if(pid_arr[num_children_launched] > 0){
            printf("I'm the parent. Child ID is: %d\n", pid_arr[num_children_launched]);
            free_command_line(&token_buffer);
	        num_children_launched ++;
        }
        else{
            perror("Fork failed");
            free_command_line(&token_buffer);
        }
    }

    if(num_children_launched > 0){
        printf("PARENT (PID: %d): Sending SIGUSR1 signal", getpid());
        for(int i = 0; i < num_children_launched; i++){
            if(kill(pid_arr[i], SIGUSR1) == -1){
                perror("PARENT: kill (SIGUSR1) failed");
            }
        }
        sleep(1);
    }

    if(num_children_launched > 0){
        printf("PARENT (PID: %d): Sending SIGSTOP signal\n", getpid());
        for(int i = 0; i < num_children_launched; i++){
            if(kill(pid_arr[i], SIGSTOP) == -1){
                perror("PARENT: kill(SIGSTOP) failed");
            }
        }
        sleep(1);
    }

    if(num_children_launched > 0){
        printf("PARENT (PID: %d): Sending SIGCONT signal\n", getpid());
        for(int i = 0; i < num_children_launched; i++){
            if(kill(pid_arr[i], SIGCONT) == -1){
                perror("PARENT: kill(SIGCONT) failed");
            }
        }
        sleep(1);
    }
            
    for(int i = 0; i < num_children_launched; i++){
    	int wstatus;
    	pid_t terminated_pid;
	    terminated_pid = waitpid(pid_arr[i], &wstatus, 0);
	    if(terminated_pid == -1){
	        perror("waitpid failed");
	    }
        else if(terminated_pid == pid_arr[i]){
            printf("PARENT: Child process %d (PID: %d) terminated \n", i, terminated_pid);
        }	
    }
    fclose(open_file);
    free(line);
    return 0;
}
