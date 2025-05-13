/*
 * string_parser.c
 *
 *  Created on: Nov 25, 2020
 *      Author: gguan, Monil
 *
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "string_parser.h"
 #include <ctype.h>
 #define _GUN_SOURCE
 
 int count_token (char* buf, const char* delim)
 {
	 //TODO：
	 /*
	 *	#1.	Check for NULL string
	 *	#2.	iterate through string counting tokens
	 *		Cases to watchout for
	 *			a.	string start with delimeter
	 *			b. 	string end with delimeter
	 *			c.	account NULL for the last token
	 *	#3. return the number of token (note not number of delimeter)
	 */
 
	 /* We are basically separating a string by a delimiter. and we are counting each piece of text. 
	 We don't count strings with delimiters and if we have a null string we return 0 for the count
	 */
 
	 if (buf == NULL){
		 return 0;
	 }
 
	 char *token; char * ptr;
 
	 int count = 0;
 
	 char *copy_buf = (char*)malloc(sizeof(char) * strlen(buf) + 1);
	 strcpy(copy_buf, buf);
 
	 copy_buf[strlen(buf)] = '\0';
	 token = strtok_r(copy_buf, delim, &ptr);
 
	 while (token != NULL){
		 count ++;
		 token = strtok_r(NULL, delim, &ptr);
	 }
    free(copy_buf); 
	return count;
 }
 
command_line str_filler(char* buf, const char* delim)
{
    command_line cl;
    cl.num_token = count_token(buf, delim);
    char **arr = (char**)malloc((cl.num_token + 1) * sizeof(char*));

    char *token; 
    char *ptr;

    char *copy_buf = (char*)malloc(sizeof(char) * strlen(buf) + 1);
    strcpy(copy_buf, buf);

    copy_buf[strlen(buf)] = '\0';
    token = strtok_r(copy_buf, delim, &ptr);

    for(int i = 0; i < cl.num_token; i++) {
    if(token == NULL) {
        break;
    }
    arr[i] = malloc(sizeof(char) * (strlen(token) + 1));
    strcpy(arr[i], token);
    int j = 0;
        while (arr[i][j] != '\0') {
            if (arr[i][j] == '\n') {
                arr[i][j] = '\0';
                break;
            }
            j++;
        }
    token = strtok_r(NULL, delim, &ptr);
    }
    arr[cl.num_token] = NULL;
    cl.command_list = arr;

    free(copy_buf);
    return cl;
}

void free_command_line(command_line* command)
{
    //TODO：
    /*
    *	#1.	free the array base num_token
    */
    for(int i = 0; i < command -> num_token; i ++){
        free(command -> command_list[i]);
    }
    free(command->command_list);
}
