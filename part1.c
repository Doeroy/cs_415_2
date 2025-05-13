#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include "string_parser.h"
#include <unistd.h>

int main(int argc, char * argv[]){
    if((argc > 1) && strcmp(argv[1], "-f") == 0){
        if(argc < 3){
            printf("Error: No filename provided for file mode\n");
            return 0;
        }
        else if(argc > 3){
            printf("Error: Can only take up to 3 arguments");
            return 0;
        }

        fptr = fopen(argv[2], "r");
        if(fptr == NULL){
            perror("ERROR: Couldn't open file \n");
            return 0;
        }
    }
}

