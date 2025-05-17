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
int curr_index = -1; // Initialize to -1, will be set before first use
int active_children_count = 0;

volatile sig_atomic_t g_sigalrm_fired = 0;
volatile sig_atomic_t g_sigchld_fired = 0;

void actual_sigalrm_handler(int sig) {
    g_sigalrm_fired = 1;
}

void actual_sigchld_handler(int sig) {
    g_sigchld_fired = 1;
}

int find_next_schedulable_process_idx(int last_processed_idx) {
    if (active_children_count == 0) {
        return -1;
    }
    int search_start_idx = (last_processed_idx == -1) ? 0 : (last_processed_idx + 1);
    for (int i = 0; i < num_children_launched; i++) {
        int candidate_idx = (search_start_idx + i) % num_children_launched;
        if (status_arr[candidate_idx] == PROC_INITIALIZING || status_arr[candidate_idx] == PROC_STOPPED) {
            return candidate_idx;
        }
    }
    if (active_children_count == 1 && last_processed_idx != -1 && status_arr[last_processed_idx] == PROC_STOPPED) {
        return last_processed_idx;
    }
    if (active_children_count > 0) {
        search_start_idx = (last_processed_idx == -1) ? 0 : (last_processed_idx + 1);
         for (int i = 0; i < num_children_launched; i++) {
            int candidate_idx = (search_start_idx + i) % num_children_launched;
            if (status_arr[candidate_idx] != PROC_TERMINATED) {
                return candidate_idx;
            }
        }
    }

    return -1;
}

void change_process() {
    int previously_running_idx = curr_index;
    if (previously_running_idx != -1 && status_arr[previously_running_idx] == PROC_RUNNING) {
        printf("PARENT: Timeslice for PID %d (idx %d) ended. Stopping.\n", pid_arr[previously_running_idx], previously_running_idx);
        if (kill(pid_arr[previously_running_idx], SIGSTOP) == 0) {
            status_arr[previously_running_idx] = PROC_STOPPED;
        } else {
            if (errno == ESRCH) {
                printf("PARENT: Process PID %d (idx %d) already exited before SIGSTOP.\n", pid_arr[previously_running_idx], previously_running_idx);
                if (status_arr[previously_running_idx] != PROC_TERMINATED) {
                    status_arr[previously_running_idx] = PROC_TERMINATED;
                    active_children_count--;
                }
            } else {
                perror("PARENT: kill (SIGSTOP) failed");
            }
        }
    }
    curr_index = find_next_schedulable_process_idx(previously_running_idx);
    if (curr_index != -1) {
        printf("PARENT: Switching to process PID %d (idx %d). Current state: %d\n", pid_arr[curr_index], curr_index, status_arr[curr_index]);
        if (status_arr[curr_index] == PROC_INITIALIZING) {
            printf("PARENT: Initializing and Starting PID %d (idx %d)\n", pid_arr[curr_index], curr_index);
            if (kill(pid_arr[curr_index], SIGUSR1) == -1) {
                perror("PARENT: kill (SIGUSR1) failed");
                status_arr[curr_index] = PROC_TERMINATED; 
                active_children_count--;
            } else {
                status_arr[curr_index] = PROC_RUNNING;
            }
        } else if (status_arr[curr_index] == PROC_STOPPED) {
            printf("PARENT: Continuing PID %d (idx %d)\n", pid_arr[curr_index], curr_index);
            if (kill(pid_arr[curr_index], SIGCONT) == -1) {
                perror("PARENT: kill (SIGCONT) failed");
                status_arr[curr_index] = PROC_TERMINATED;
                active_children_count--;
            } else {
                status_arr[curr_index] = PROC_RUNNING;
            }
        } else if (status_arr[curr_index] == PROC_RUNNING) {
            printf("PARENT: Re-scheduling only active process PID %d (idx %d) which is already running (or was just continued).\n", pid_arr[curr_index], curr_index);
        }
        if (active_children_count > 0 && status_arr[curr_index] == PROC_RUNNING) {
            alarm(TIME_SLICE_SECONDS);
        } else if (active_children_count == 0) {
            alarm(0);
        } else if (curr_index == -1 && active_children_count > 0){
            printf("PARENT: change_process ended with curr_index -1 but active children %d. Setting alarm.\n", active_children_count);
            alarm(TIME_SLICE_SECONDS);
        }

    } else { 
        if (active_children_count == 0) {
            printf("PARENT: No schedulable process found and no active children. Stopping alarm.\n");
            alarm(0);
        } else {
            printf("PARENT: No schedulable process found by change_process. Keeping alarm for now.\n");
            alarm(TIME_SLICE_SECONDS); 
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
        }

        if (reaped_child_idx != -1) {
            if (active_children_count == 0) {
                printf("PARENT: All children have terminated (handled by SIGCHLD). Cancelling alarm.\n");
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
    while ((getline(&line, &len, open_file)) != -1) {
        command_line token_buffer = str_filler(line, " \t\n");
        if (token_buffer.num_token == 0) {
            free_command_line(&token_buffer);
            continue;
        }
        if (num_children_launched >= MAX_LENGTH) {
            fprintf(stderr, "PARENT: Maximum number of commands (%d) reached. Skipping: %s\n", MAX_LENGTH, token_buffer.command_list[0]);
            free_command_line(&token_buffer);
            continue;
        }
        pid_t child_pid = fork();
        if (child_pid < 0) {
            perror("PARENT: Fork failed");
            free_command_line(&token_buffer);
        } else if (child_pid == 0) {
            sigset_t set_for_usr1;
            int recvd_sig;
            sigemptyset(&set_for_usr1);
            sigaddset(&set_for_usr1, SIGUSR1);

            if (sigprocmask(SIG_BLOCK, &set_for_usr1, NULL) == -1) {
                perror("CHILD: sigprocmask block SIGUSR1 failed");
                free_command_line(&token_buffer);
                _exit(errno);
            }

            if (sigwait(&set_for_usr1, &recvd_sig) != 0) {
                fprintf(stderr, "CHILD (PID %d): sigwait failed with error %d.\n", getpid(), errno);
                free_command_line(&token_buffer);
                _exit(errno);
            }

            if (recvd_sig == SIGUSR1) {
                printf("CHILD (PID %d): Received SIGUSR1. Executing command: %s\n", getpid(), token_buffer.command_list[0]);
            } else {
                fprintf(stderr, "CHILD (PID %d): Received unexpected signal %d via sigwait.\n", getpid(), recvd_sig);
                free_command_line(&token_buffer);
                _exit(1);
            }

            if (execvp(token_buffer.command_list[0], token_buffer.command_list) == -1) {
                perror("CHILD: execvp failed");
                fprintf(stderr, "CHILD: Failed command was: %s\n", token_buffer.command_list[0]);
                free_command_line(&token_buffer);
                _exit(errno);
            }
        } else { 
            pid_arr[num_children_launched] = child_pid;
            status_arr[num_children_launched] = PROC_INITIALIZING;
            printf("PARENT: Launched child PID %d for command: %s\n", child_pid, token_buffer.command_list[0]);
            num_children_launched++;
            free_command_line(&token_buffer);
        }
    }
    fclose(open_file);
    if (line) {
        free(line);
        line = NULL;
    }
    active_children_count = num_children_launched;
    if (num_children_launched == 0) {
        printf("PARENT: No commands found in input file. Exiting.\n");
        return 0;
    }
    signal(SIGALRM, actual_sigalrm_handler);
    signal(SIGCHLD, actual_sigchld_handler);
    curr_index = find_next_schedulable_process_idx(-1); 
    if (curr_index != -1) {
        printf("PARENT: Starting initial process PID %d (idx %d).\n", pid_arr[curr_index], curr_index);
        if (kill(pid_arr[curr_index], SIGUSR1) == -1) {
            perror("PARENT: kill (SIGUSR1) for initial process failed");
            status_arr[curr_index] = PROC_TERMINATED;
            active_children_count--;
        } else {
            status_arr[curr_index] = PROC_RUNNING;
        }
        if(active_children_count > 0 && status_arr[curr_index] == PROC_RUNNING) {
            alarm(TIME_SLICE_SECONDS);
        } else if (active_children_count == 0) {
            alarm(0);
        }
    } else if (active_children_count > 0) {
        printf("PARENT ERROR: Children launched but none found schedulable initially!\n");
        return 1;
    } else {
        printf("PARENT: No active children to schedule initially.\n");
    }


    printf("PARENT: Entering main scheduling loop. Active children: %d\n", active_children_count);
    while (active_children_count > 0) {
        if (g_sigalrm_fired) {
            g_sigalrm_fired = 0;
            if (active_children_count > 0) {
                change_process();
            } else {
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
    return 0;
}