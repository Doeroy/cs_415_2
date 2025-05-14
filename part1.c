#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include "string_parser.h"
//#include <unistd.h>

int main(int argc, char * argv[]){
    FILE * fptr;
    char *line = NULL;
    size_t len = 0;
    int read;

    if((argc > 1)){
        fptr = fopen(argv[0], "r");
        if(fptr == NULL){
            perror("ERROR: Couldn't open file \n");
            return 0;
        }
    }
    if((read = getline(&line, &len, fptr)) == -1){
        return 0;
    }

    command_line token_buffer = str_filler(line, " ");
    printf("%s", token_buffer.command_list[0]);
}

