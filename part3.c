#define _GNU_SOURCE // Must be the first line for getline, strdup (if used in str_filler)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h> // For waitpid and status macros
#include <signal.h>   // For sigset_t, sigemptyset, sigaddset, sigprocmask, sigwait, kill, signals, sigaction
#include <time.h>     // For nanosleep (optional, for precise sleeps) or time()
#include "string_parser.h"
#include <unistd.h> // For fork, execvp, _exit, getpid, sleep, alarm, pause
#include <errno.h>  // For errno

#define MAX_COMMANDS 1000
#define TIME_SLICE_SECONDS 1 // Define the time slice for each process

// Enum for process states (relevant for the scheduler)
typedef enum {
    PROC_INITIALIZING, // Child has been forked, waiting for initial SIGUSR1 from scheduler
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_TERMINATED
} process_state_t;

// Global variables for signal handling and scheduling
volatile sig_atomic_t sigalrm_fired = 0; // Flag set by SIGALRM handler
volatile sig_atomic_t sigchld_fired = 0; // Flag set by SIGCHLD handler

pid_t pid_arr[MAX_COMMANDS];
process_state_t process_states[MAX_COMMANDS];
int num_children_launched = 0;
int current_running_idx = -1; 
int active_children_count = 0; 

void sigalrm_handler(int signum) {
    if (signum == SIGALRM) {
        sigalrm_fired = 1;
    }
}

// SIGCHLD handler function
void sigchld_handler(int signum) {
    if (signum == SIGCHLD) {
        sigchld_fired = 1;
    }
}

void select_and_run_next_process() {
    if (active_children_count == 0) {
        current_running_idx = -1;
        alarm(0); 
        return;
    }

    int search_start_idx = 0;
    if (current_running_idx != -1) { 
        search_start_idx = (current_running_idx + 1) % num_children_launched;
    } else { 
        for(int i=0; i<num_children_launched; ++i) {
            if(process_states[i] != PROC_TERMINATED) {
                search_start_idx = i;
                break;
            }
        }
    }
    
    int next_idx_to_run = -1;
    for (int i = 0; i < num_children_launched; ++i) {
        int temp_idx = (search_start_idx + i) % num_children_launched;
        if (process_states[temp_idx] == PROC_INITIALIZING || process_states[temp_idx] == PROC_STOPPED) {
            next_idx_to_run = temp_idx;
            break;
        }
    }

    if (next_idx_to_run == -1) {
        if (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
            next_idx_to_run = current_running_idx;
        } else {
             for (int i = 0; i < num_children_launched; ++i) {
                if (process_states[i] == PROC_RUNNING) {
                    next_idx_to_run = i;
                    break;
                }
            }
        }
    }

    if (next_idx_to_run != -1) {
        current_running_idx = next_idx_to_run;
        printf("PARENT: Scheduler: Next to run is child (PID: %d, Index: %d, State: %d).\n",
               pid_arr[current_running_idx], current_running_idx, process_states[current_running_idx]);
        fflush(stdout);

        if (process_states[current_running_idx] == PROC_INITIALIZING) {
            if (kill(pid_arr[current_running_idx], SIGUSR1) == -1) {
                perror("PARENT: kill (SIGUSR1) for scheduled child failed");
                process_states[current_running_idx] = PROC_TERMINATED; 
                active_children_count--;
                current_running_idx = -1; 
                sigalrm_fired = 1; 
                return;
            }
            process_states[current_running_idx] = PROC_RUNNING;
        } else if (process_states[current_running_idx] == PROC_STOPPED) {
            if (kill(pid_arr[current_running_idx], SIGCONT) == -1) {
                perror("PARENT: kill (SIGCONT) for scheduled child failed");
                if (errno == ESRCH) { 
                    process_states[current_running_idx] = PROC_TERMINATED;
                    active_children_count--;
                    current_running_idx = -1;
                    sigalrm_fired = 1; 
                    return;
                }
            }
            process_states[current_running_idx] = PROC_RUNNING;
        }        
        if (process_states[current_running_idx] == PROC_RUNNING) {
            printf("PARENT: Scheduler: Setting alarm for child (PID: %d).\n", pid_arr[current_running_idx]);
            fflush(stdout);
            alarm(TIME_SLICE_SECONDS);
        }
    } else if (active_children_count > 0) {
        printf("PARENT: Scheduler: No new process to switch to, but active children remain. Current running: %d\n", current_running_idx);
        fflush(stdout);
        if(current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
            alarm(TIME_SLICE_SECONDS); 
            alarm(1);
        }
    } else {
        alarm(0); 
        current_running_idx = -1;
    }
}

void handle_terminated_children() {
    int status;
    pid_t child_pid;

    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int found_idx = -1;
        for (int i = 0; i < num_children_launched; ++i) {
            if (pid_arr[i] == child_pid) {
                found_idx = i;
                break;
            }
        }

        if (found_idx != -1 && process_states[found_idx] != PROC_TERMINATED) {
            printf("PARENT: Child (PID: %d, Index: %d) has terminated. ", child_pid, found_idx);
            if (WIFEXITED(status)) {
                printf("Exited status %d.\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Killed by signal %d.\n", WTERMSIG(status));
            } else {
                printf("Terminated with unknown status.\n");
            }
            fflush(stdout);

            process_states[found_idx] = PROC_TERMINATED;
            active_children_count--;
            printf("PARENT: Active children count updated to %d.\n", active_children_count);
            fflush(stdout);

            if (current_running_idx == found_idx) {
                printf("PARENT: Currently running child (PID: %d) terminated. Cancelling its alarm.\n", child_pid);
                fflush(stdout);
                alarm(0);
                current_running_idx = -1; 
                sigalrm_fired = 1; 
            }
        }
    }
    if (child_pid == -1 && errno != ECHILD && errno != 0) { 
        perror("PARENT: waitpid WNOHANG error in handle_terminated_children");
    }
}
int main(int argc, char *argv[]) {
    FILE *open_file;
    char *line = NULL;
    size_t len = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    open_file = fopen(argv[1], "r");
    if (open_file == NULL) {
        perror("Error opening input file");
        return 1;
    }
    struct sigaction sa_alarm, sa_chld;
    memset(&sa_alarm, 0, sizeof(sa_alarm));
    sa_alarm.sa_handler = sigalrm_handler;
    sa_alarm.sa_flags = SA_RESTART; 
    if (sigaction(SIGALRM, &sa_alarm, NULL) == -1) {
        perror("PARENT: sigaction for SIGALRM failed");
        return 1;
    }
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("PARENT: sigaction for SIGCHLD failed");
        return 1;
    }
    printf("PARENT (PID: %d): Forking child processes...\n", getpid());
    fflush(stdout);
    while ((getline(&line, &len, open_file)) != -1) {
        command_line token_buffer = str_filler(line, " ");
        if (token_buffer.num_token == 0) {
            if (token_buffer.command_list != NULL) free_command_line(&token_buffer);
            continue;
        }
        if (num_children_launched >= MAX_COMMANDS) {
            fprintf(stderr, "PARENT: Max commands reached. Skipping: %s\n", line);
            free_command_line(&token_buffer);
            continue;
        }
        pid_t child_pid = fork();
        if (child_pid < 0) {
            perror("PARENT: Fork failed");
            free_command_line(&token_buffer);
            } else if (child_pid == 0) { 
                sigset_t set_for_usr1; int recvd_sig;
                sigemptyset(&set_for_usr1); sigaddset(&set_for_usr1, SIGUSR1);
                if (sigprocmask(SIG_BLOCK, &set_for_usr1, NULL) == -1) { 
                    perror("CHILD: sigprocmask"); free_command_line(&token_buffer); _exit(1); 
                }
                if (sigwait(&set_for_usr1, &recvd_sig) != 0) { 
                    fprintf(stderr, "CHILD: sigwait failed\n"); free_command_line(&token_buffer); _exit(1); 
                }
                if (recvd_sig == SIGUSR1) {
                    sigprocmask(SIG_UNBLOCK, &set_for_usr1, NULL); 
                } else { 
                    fprintf(stderr, "CHILD: Bad signal %d\n", recvd_sig); 
                    free_command_line(&token_buffer); _exit(1); 
                }
                if (execvp(token_buffer.command_list[0], token_buffer.command_list) == -1) {
                    perror("CHILD: execvp failed");
                    fprintf(stderr, "CHILD: Failed cmd: '%s'\n", token_buffer.command_list[0]);
                    free_command_line(&token_buffer); _exit(1);
                }
            } 
        else { 
            pid_arr[num_children_launched] = child_pid;
            process_states[num_children_launched] = PROC_INITIALIZING;
            num_children_launched++;
            free_command_line(&token_buffer);
        }
    }
    fclose(open_file);
    free(line);

    active_children_count = num_children_launched;
    if (num_children_launched == 0) {
        printf("PARENT: No commands to run. Exiting.\n");
        return 0;
    }
    printf("PARENT: All children forked. Initializing scheduler.\n");
    sigalrm_fired = 1; 

    printf("PARENT: Entering scheduling loop. Active children: %d\n", active_children_count);
    while (active_children_count > 0) {
        if (sigchld_fired) {
            sigchld_fired = 0; 
            handle_terminated_children();
        }
        if (sigalrm_fired) {
            sigalrm_fired = 0; 
            if (active_children_count == 0) { 
                printf("PARENT: All children terminated after SIGCHLD processing. Exiting loop.\n");
                break;
            }
            if (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
                printf("PARENT: Scheduler: Time slice ended for child (PID: %d). Sending SIGSTOP.\n", pid_arr[current_running_idx]);
                fflush(stdout);
                if (kill(pid_arr[current_running_idx], SIGSTOP) == 0) {
                    process_states[current_running_idx] = PROC_STOPPED;
                } else {
                    if (errno == ESRCH) { 
                        printf("PARENT: Child (PID: %d) already terminated before SIGSTOP could be sent.\n", pid_arr[current_running_idx]);
                        if(process_states[current_running_idx] != PROC_TERMINATED) {
                        }
                    } else {
                        perror("PARENT: kill (SIGSTOP) failed");
                    }
                }
            }
            handle_terminated_children();
            if (active_children_count == 0) {
                 printf("PARENT: All children terminated during SIGALRM processing. Exiting loop.\n");
                 break;
            }
            select_and_run_next_process();
        }
        if (active_children_count > 0) {
            pause();
        }
    }
    printf("PARENT (PID: %d): All child processes have completed. MCP exiting.\n", getpid());
    return 0;
}