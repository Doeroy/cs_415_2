#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h> // Required for waitpid and status macros
#include <time.h>     // Not strictly used in this version, but often kept
#include "string_parser.h" // For command_line and str_filler
#include <unistd.h>   // For fork, execvp, _exit, getpid
#include <errno.h>    // For errno

#define MAX_COMMANDS 1000 // Define a maximum number of commands

int main(int argc, char *argv[]) {
    FILE *open_file;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread; // getline returns ssize_t
    int num_children_launched = 0; // Correctly count children launched by parent

    // Array to store PIDs of child processes
    pid_t pid_arr[MAX_COMMANDS];

    // --- 1. Argument Checking ---
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1; // Return non-zero for error
    }

    // --- 2. File Opening ---
    open_file = fopen(argv[1], "r");
    if (open_file == NULL) {
        perror("Error opening input file"); // perror adds its own newline and error string
        return 1; // Return non-zero for error
    }

    // --- 3. Read file, fork, and exec commands ---
    while ((nread = getline(&line, &len, open_file)) != -1) {
        // Strip the newline character read by getline
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
            nread--; // Adjust length
        }

        // Skip empty lines
        if (nread == 0 || strlen(line) == 0) {
            continue;
        }

        command_line token_buffer = str_filler(line, " ");

        // Skip if str_filler produced no tokens (e.g., line with only delimiters)
        if (token_buffer.num_token == 0) {
            free_command_line(&token_buffer); // Still need to free if internal allocations happened
            continue;
        }

        // Check if we have space for another PID
        if (num_children_launched >= MAX_COMMANDS) {
            fprintf(stderr, "MCP: Maximum number of commands (%d) reached. Skipping: %s\n", MAX_COMMANDS, line);
            free_command_line(&token_buffer);
            continue; // Or break if you want to stop processing the file
        }

        pid_t child_pid = fork();

        if (child_pid < 0) { // Fork failed
            perror("PARENT: Fork failed");
            free_command_line(&token_buffer); // Clean up tokens for this command
            // Potentially continue to try next command or exit MCP if critical
            continue;
        } else if (child_pid == 0) {
            // --- CHILD Process ---
            // printf("CHILD (PID: %d): Attempting to execute: %s\n", getpid(), token_buffer.command_list[0]);
            // fflush(stdout); // Good for debugging, ensures print before exec

            if (execvp(token_buffer.command_list[0], token_buffer.command_list) == -1) {
                perror("CHILD: execvp failed");
                fprintf(stderr, "CHILD (PID: %d): Failed command was: '%s'\n", getpid(), token_buffer.command_list[0]);
                free_command_line(&token_buffer); // Free memory before child exits
                _exit(1); // CRITICAL: Child MUST exit if execvp fails. Use _exit.
            }
            // If execvp is successful, the child's code is replaced, and it will never reach here.

        } else {
            // --- PARENT Process ---
            // printf("PARENT (PID: %d): Launched child (PID: %d) for command: %s\n", getpid(), child_pid, token_buffer.command_list[0]);
            // fflush(stdout);
            pid_arr[num_children_launched] = child_pid; // Store child PID
            num_children_launched++;                   // Increment count of children launched
            free_command_line(&token_buffer);          // Parent frees its copy of tokens
        }
    }

    free(line); // Free the buffer allocated by getline
    // fclose(open_file); // Close file after reading, or after waiting (see below)

    // --- 4. Parent waits for all launched children to terminate ---
    printf("PARENT: All commands from file processed. Waiting for %d child process(es) to complete...\n", num_children_launched);
    fflush(stdout);

    for (int i = 0; i < num_children_launched; i++) {
        int wstatus;
        pid_t terminated_pid = waitpid(pid_arr[i], &wstatus, 0); // Wait for a specific child

        if (terminated_pid == -1) {
            perror("PARENT: waitpid failed");
        } else {
            // printf("PARENT: Child process (PID: %d) terminated ", terminated_pid);
            if (WIFEXITED(wstatus)) {
                // printf("with exit status %d.\n", WEXITSTATUS(wstatus));
            } else if (WIFSIGNALED(wstatus)) {
                // printf("due to signal %d.\n", WTERMSIG(wstatus));
            } else {
                // printf("(unknown termination reason).\n");
            }
            // fflush(stdout); // Ensure this specific line prints
        }
    }

    fclose(open_file); // Close the input file now that we're done with it.

    // --- 5. MCP exits successfully ---
    printf("PARENT: All child processes have completed. MCP exiting.\n");
    fflush(stdout); // Ensure final message is printed
    return 0; // Equivalent to exit(0), indicates successful MCP termination
}
