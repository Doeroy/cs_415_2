#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include "string_parser.h"
#include <unistd.h>
#include <errno.h>
#define MAX_LENGTH 1000

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
    pid_t * pid_arr = (pid_t *)malloc(sizeof(pid_t) * amt_of_commands);
    while(getline(&line, &len, open_file) != -1){
	command_line token_buffer = str_filler(line, " ");
	pid_arr[amt_of_commands] = fork();
	if(pid_arr[amt_of_commands] == 0){
	   printf("Child process created\n");
	   amt_of_commands ++;
	}
	else if(pid_arr[amt_of_commands] > 0){
	   printf("I'm the parent. Child ID is: %d\n", pid_arr[count]);
	   amt_of_commands ++;
	}
	else{
	   perror("Fork failed\n");
	}
    }
    printf("here");
    free(pid_arr);
    fclose(open_file);
    return 0;
}
