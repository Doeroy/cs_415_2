#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
            printf("Child process created\n");
            if(execvp(token_buffer.command_list[0], token_buffer.command_list) == -1){
                free_command_line(&token_buffer);
		        perror("Error with execvp()");
                _exit(1);
	    }
        }
        else if(pid_arr[num_children_launched] > 0){
            printf("I'm the parent. Child ID is: %d\n", pid_arr[num_children_launched]);
            free_command_line(&token_buffer);
	        num_children_launched ++;
        }
        else{
            perror("Fork failed\n");
            free_command_line(&token_buffer);
        }
    }
    
    for(int i = 0; i < num_children_launched; i++){
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
    free(line);

    return 0;
}
