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
    #define TIME_SLICE_SECONDS 1

    typedef enum {
        PROC_INITIALIZING, 
        PROC_RUNNING,
        PROC_STOPPED,
        PROC_TERMINATED
    } process_state_t;

    pid_t pid_arr[MAX_LENGTH];
    process_state_t status_arr[MAX_LENGTH];
    int num_children_launched = 0;
    int curr_index = 0;
    int active_children_count;

    volatile sig_atomic_t g_sigalrm_fired = 0; 
    volatile sig_atomic_t g_sigchld_fired = 0;


    void actual_sigalrm_handler(int sig){ 
        g_sigalrm_fired = 1; 
    }

    void actual_sigchld_handler(int sig){ 
        g_sigchld_fired = 1; 
    }

    
int find_next_schedulable_process_idx(int last_processed_idx) {
    if (active_children_count == 0) {
        return -1;
    }
    int current_search_idx = (last_processed_idx == -1) ? 0 : (last_processed_idx + 1) % num_children_launched;

    for (int count = 0; count < num_children_launched; count++) { 
        if (status_arr[current_search_idx] == PROC_INITIALIZING ||
            status_arr[current_search_idx] == PROC_STOPPED) {
            return current_search_idx;
        }
        current_search_idx = (current_search_idx + 1) % num_children_launched;
    }
    if (active_children_count > 0) {
         current_search_idx = (last_processed_idx == -1) ? 0 : (last_processed_idx + 1) % num_children_launched;
         for (int count = 0; count < num_children_launched; count++) {
            if (status_arr[current_search_idx] != PROC_TERMINATED) {
                return current_search_idx;
            }
            current_search_idx = (current_search_idx + 1) % num_children_launched;
         }
    }
    return -1; 
}

void change_process(){
    printf("PARENT: Timeslice for PID %d ended", pid_arr[curr_index]);
    kill(pid_arr[curr_index], SIGSTOP);
    int prev_idx = curr_index;
    curr_index = find_next_schedulable_process_idx(prev_idx);
    if (curr_index != -1) { 
        printf("PARENT: Switching to process PID %d. State: %d\n", pid_arr[curr_index], status_arr[curr_index]);
        if (status_arr[curr_index] == PROC_INITIALIZING) {
            kill(pid_arr[curr_index], SIGUSR1);
            status_arr[curr_index] = PROC_RUNNING;
            printf("PARENT: Initialized and Started PID %d\n", pid_arr[curr_index]);
        } else if (status_arr[curr_index] == PROC_STOPPED) {
            kill(pid_arr[curr_index], SIGCONT);
            status_arr[curr_index] = PROC_RUNNING;
            printf("PARENT: Continued PID %d \n", pid_arr[curr_index]);
        }
        if (active_children_count > 0) { 
            alarm(TIME_SLICE_SECONDS);
        }
    } 
    else {
        printf("PARENT: No schedulable process found by change_process. Active children: %d.\n", active_children_count);
        if (active_children_count == 0) {
            alarm(0); 
        }
    }
}

void handle_terminated_children() {
    int status;
    pid_t terminated_pid;
    int child_was_reaped_this_invocation = 0;

    while ((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child_was_reaped_this_invocation = 1; 
        int reaped_child_idx = -1;

        for (int i = 0; i < num_children_launched; i++) {
            if (pid_arr[i] == terminated_pid) {
                reaped_child_idx = i;
                if (status_arr[i] != PROC_TERMINATED) { 
                    status_arr[i] = PROC_TERMINATED;
                    active_children_count--;
                    printf("PARENT: Child PID %d (idx %d) reaped. Active children: %d. ",
                        terminated_pid, i, active_children_count);
                    if (WIFEXITED(status)) {
                        printf("Exited normally (status %d).\n", WEXITSTATUS(status));
                    } else if (WIFSIGNALED(status)) {
                        printf("Terminated by signal %d.\n", WTERMSIG(status));
                    } else {
                        printf("Terminated abnormally.\n"); 
                    }
                }
                break; 
        }
        if (reaped_child_idx != -1) { 
            if (active_children_count == 0) {
                printf("PARENT: The last active child terminated. No more processes to schedule.\n");
                alarm(0);
            } else if (reaped_child_idx == curr_index) {
                printf("PARENT: Currently running process (PID %d, idx %d) terminated early.\n",
                    pid_arr[curr_index], curr_index);
                alarm(0);           
                g_sigalrm_fired = 1; 
            }
        }
    } 

    if (terminated_pid == -1 && errno != ECHILD && errno != 0) {
        perror("waitpid in handle_terminated_children");
    }
    }
}

int main(int argc, char * argv[]){
    FILE * open_file;
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

    while(getline(&line, &len, open_file) != -1){
        command_line token_buffer = str_filler(line, " ");
        if (num_children_launched >= MAX_LENGTH) {
            fprintf(stderr, "PARENT: Maximum number of commands (%d) reached. Skipping: %s\n", MAX_LENGTH, line);
            free_command_line(&token_buffer); 
            continue; 
        }
        pid_t child_pid = fork();
        if(child_pid < 0){
            perror("fork failed");
            free_command_line(&token_buffer);
            return 1;
        }

        if(child_pid > 0){
            pid_arr[num_children_launched] = child_pid;
            status_arr[num_children_launched] = PROC_INITIALIZING;
            printf("I'm the parent. Child ID is: %d\n", pid_arr[num_children_launched]);
            free_command_line(&token_buffer);
            num_children_launched ++;
        }

        if(child_pid == 0){
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
                    free_command_line(&token_buffer);
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
            } else {
                fprintf(stderr, "CHILD (PID: %d): Received unexpected signal %d via sigwait instead of SIGUSR1.\n", getpid(), received_signal);
                free_command_line(&token_buffer);
                _exit(1);
            }

            if(execvp(token_buffer.command_list[0], token_buffer.command_list) == -1){
                perror("Error with execvp()");
                free_command_line(&token_buffer);
                _exit(1);
            }
        }
    }
    active_children_count = num_children_launched;
    if(num_children_launched == 0){
        printf("No children were created, now exiting");
        return 0;
    }
    kill(pid_arr[curr_index], SIGUSR1);
    status_arr[curr_index] = PROC_RUNNING;
    signal(SIGALRM, actual_sigalrm_handler);
    signal(SIGCHLD, actual_sigchld_handler);
    alarm(TIME_SLICE_SECONDS);

    printf("PARENT: Entering scheduling loop. Active children: %d\n", active_children_count);
    while(active_children_count > 0){
        if (g_sigalrm_fired) {
            g_sigalrm_fired = 0;
            if (active_children_count > 0) {
                change_process(SIGALRM); 
            } 
            else {
                alarm(0);
            }
        } 
        if (g_sigchld_fired) { 
            g_sigchld_fired = 0; 
            handle_terminated_children(); 
        }
        if (active_children_count > 0 && !g_sigalrm_fired && !g_sigchld_fired) {
            pause(); 
        }
    }
    printf("PARENT: All children have terminated. MCP exiting.\n");
    fclose(open_file);
    free(line);
    return 0;
}
