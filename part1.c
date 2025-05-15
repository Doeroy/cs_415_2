#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include "string_parser.h"
#include <unistd.h>
#include <errno.h>
#define MAX_LENGTH 1000
#include <sys/wait.h>
int main(int argc, char * argv[]){
    FILE * open_file;
    int amt_of_commands;
    char **args;
    char *line = NULL;
    size_t len = 0;
    int count = 0;
    if(argc != 2){
	perror("Error you must have two arguments");
	return 0;	
    }
 
    open_file = fopen(argv[1], "r");
    if(open_file == NULL){
        perror("Error opening file!\n");
        return 0;
    }

    amt_of_commands = 0;
    pid_t pid_arr[MAX_LENGTH];
    while(getline(&line, &len, open_file) != -1){
        command_line token_buffer = str_filler(line, " ");
        pid_arr[amt_of_commands] = fork();
        if(pid_arr[amt_of_commands] == 0){
            printf("Child process created\n");
            if(execvp(token_buffer.command_list[0], token_buffer.command_list) == -1){
		perror("Error with execvp()");
	    }
        }
        else if(pid_arr[amt_of_commands] > 0){
            printf("I'm the parent. Child ID is: %d\n", pid_arr[amt_of_commands]);
	    amt_of_commands ++;
        }
        else{
            perror("Fork failed\n");
        }
    }
    
    for(int i = 0; i < amt_of_commands; i++){
	
    	int wstatus;
    	pid_t terminated_pid;
	terminated_pid = waitpid(pid_arr[i], &wstatus, 0);
	if(terminated_pid == -1){
	        perror("waitpid failed");
	}
	else if(terminated_pid == pid_arr[i]){
	    printf("PARENT: Child process %d (PID: %d) terminated ", i, terminated_pid);
	}	
    }
    fclose(open_file);
    _exit(1);
    return 0;
}
