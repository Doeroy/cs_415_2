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
    PROC_INITIALIZING, // Child has been forked, waiting for initial SIGUSR1
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_TERMINATED
} process_state_t;

// Global variables for signal handling and scheduling
volatile sig_atomic_t sigalrm_fired = 0; // Flag set by SIGALRM handler
pid_t pid_arr[MAX_COMMANDS];
process_state_t process_states[MAX_COMMANDS]; // Track state of each child
int num_children_launched = 0;
int current_running_idx = -1; // Index in pid_arr of the currently "running" child
int active_children_count = 0; // Number of children not yet terminated

// SIGALRM handler function
void sigalrm_handler(int signum) {
    if (signum == SIGALRM) {
        sigalrm_fired = 1;
    }
}

int main(int argc, char *argv[]) {
    FILE *open_file;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    // --- 1. Argument Checking ---
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    // --- 2. File Opening ---
    open_file = fopen(argv[1], "r");
    if (open_file == NULL) {
        perror("Error opening input file");
        return 1;
    }

    // --- 3. Setup SIGALRM handler ---
    struct sigaction sa_alarm;
    memset(&sa_alarm, 0, sizeof(sa_alarm));
    sa_alarm.sa_handler = sigalrm_handler;
    // sa_alarm.sa_flags = SA_RESTART; // Optional: restart syscalls if interrupted by this signal
    if (sigaction(SIGALRM, &sa_alarm, NULL) == -1) {
        perror("PARENT: sigaction for SIGALRM failed");
        return 1;
    }

    // --- 4. Fork child processes. Children will set up to wait for SIGUSR1 ---
    printf("PARENT (PID: %d): Forking child processes...\n", getpid());
    fflush(stdout);
    while ((nread = getline(&line, &len, open_file)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }
        if (strlen(line) == 0) {
            continue;
        }

        command_line token_buffer = str_filler(line, " ");
        if (token_buffer.num_token == 0) {
            if (token_buffer.command_list != NULL) free_command_line(&token_buffer);
            continue;
        }

        if (num_children_launched >= MAX_COMMANDS) {
            fprintf(stderr, "PARENT: Maximum number of commands (%d) reached. Skipping: %s\n", MAX_COMMANDS, line);
            free_command_line(&token_buffer);
            continue;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            perror("PARENT: Fork failed");
            free_command_line(&token_buffer);
        } else if (child_pid == 0) {
            // --- CHILD Process ---
            // printf("CHILD (PID: %d): process created, waiting for SIGUSR1.\n", getpid());
            // fflush(stdout);
            sigset_t set_for_usr1;
            int received_signal;
            sigemptyset(&set_for_usr1);
            sigaddset(&set_for_usr1, SIGUSR1);
            if (sigprocmask(SIG_BLOCK, &set_for_usr1, NULL) == -1) {
                perror("CHILD: sigprocmask failed");
                free_command_line(&token_buffer);
                _exit(1);
            }
            if (sigwait(&set_for_usr1, &received_signal) != 0) {
                fprintf(stderr, "CHILD (PID: %d): sigwait() failed.\n", getpid());
                free_command_line(&token_buffer);
                _exit(1);
            }
            if (received_signal == SIGUSR1) {
                // printf("CHILD (PID: %d): Received SIGUSR1. Attempting to execvp().\n", getpid());
                // fflush(stdout);
                 // Unblock SIGUSR1 before exec, though exec replaces signal mask anyway
                sigprocmask(SIG_UNBLOCK, &set_for_usr1, NULL);
            } else {
                fprintf(stderr, "CHILD (PID: %d): Received unexpected signal %d via sigwait.\n", getpid(), received_signal);
                free_command_line(&token_buffer);
                _exit(1);
            }
            if (execvp(token_buffer.command_list[0], token_buffer.command_list) == -1) {
                perror("CHILD: execvp failed");
                fprintf(stderr, "CHILD (PID: %d): Failed command was: '%s'\n", getpid(), token_buffer.command_list[0]);
                free_command_line(&token_buffer);
                _exit(1);
            }
        } else {
            // --- PARENT Process (after successful fork) ---
            // printf("PARENT (PID: %d): Forked child (PID: %d)\n", getpid(), child_pid);
            // fflush(stdout);
            pid_arr[num_children_launched] = child_pid;
            process_states[num_children_launched] = PROC_INITIALIZING; // Child is waiting for SIGUSR1
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

    // --- 5. Initial Start of the first process by the scheduler ---
    current_running_idx = 0; // Start with the first child
    if (process_states[current_running_idx] == PROC_INITIALIZING) {
        printf("PARENT: Starting first child (PID: %d) with SIGUSR1.\n", pid_arr[current_running_idx]);
        fflush(stdout);
        if (kill(pid_arr[current_running_idx], SIGUSR1) == -1) {
            perror("PARENT: kill (SIGUSR1) for first child failed");
            // Handle error: maybe mark as terminated
            process_states[current_running_idx] = PROC_TERMINATED;
            active_children_count--;
        } else {
            process_states[current_running_idx] = PROC_RUNNING;
        }
    }
    // For Part 3, we only SIGUSR1 the first one. Others will get SIGUSR1 when it's their first turn.
    // Or, simpler: send SIGUSR1 to all, then SIGSTOP all except first. Let's use the former for now.

    if (active_children_count > 0 && process_states[current_running_idx] == PROC_RUNNING) {
        printf("PARENT: Setting alarm for child (PID: %d).\n", pid_arr[current_running_idx]);
        fflush(stdout);
        alarm(TIME_SLICE_SECONDS);
    } else if (active_children_count > 0) {
        // First child failed to start, try to find another one.
        // This logic can be more complex; for now, if first fails, we might get stuck
        // or need to advance current_running_idx.
        // For simplicity, assume first child starts or is marked terminated.
        // The main loop will handle finding the next runnable if current_running_idx is -1 or terminated.
        current_running_idx = -1; // No one is running yet
    }


    // --- 6. MCP Main Scheduling Loop ---
    printf("PARENT: Entering scheduling loop. Active children: %d\n", active_children_count);
    fflush(stdout);
    while (active_children_count > 0) {
        pause(); // Wait for a signal (SIGALRM or SIGCHLD if we were handling it)

        if (sigalrm_fired) {
            sigalrm_fired = 0; // Reset the flag

            // --- A. Stop the current running process (if any) ---
            if (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
                printf("PARENT: Time slice ended for child (PID: %d). Sending SIGSTOP.\n", pid_arr[current_running_idx]);
                fflush(stdout);
                if (kill(pid_arr[current_running_idx], SIGSTOP) == 0) {
                    process_states[current_running_idx] = PROC_STOPPED;
                } else {
                    if (errno == ESRCH) { // No such process (it might have terminated)
                        printf("PARENT: Child (PID: %d) already terminated before SIGSTOP.\n", pid_arr[current_running_idx]);
                        fflush(stdout);
                        process_states[current_running_idx] = PROC_TERMINATED;
                        // active_children_count--; // This will be handled by waitpid check
                    } else {
                        perror("PARENT: kill (SIGSTOP) failed");
                    }
                }
            }

            // --- B. Check for any terminated children (IMPORTANT: do this often) ---
            int previously_active = active_children_count;
            for (int i = 0; i < num_children_launched; i++) {
                if (process_states[i] != PROC_TERMINATED) {
                    int status;
                    pid_t child_pid = waitpid(pid_arr[i], &status, WNOHANG);

                    if (child_pid > 0) { // Child with PID child_pid changed state
                        if (WIFEXITED(status) || WIFSIGNALED(status)) {
                            printf("PARENT: Child (PID: %d) has terminated. ", child_pid);
                            if (WIFEXITED(status)) printf("Exited status %d.\n", WEXITSTATUS(status));
                            else printf("Killed by signal %d.\n", WTERMSIG(status));
                            fflush(stdout);
                            
                            process_states[i] = PROC_TERMINATED;
                            active_children_count--;
                            if (current_running_idx == i) {
                                current_running_idx = -1; // The one that was running just terminated
                            }
                        }
                    } else if (child_pid == -1 && errno != ECHILD) {
                        perror("PARENT: waitpid WNOHANG error");
                    }
                }
            }
             if (previously_active != active_children_count) {
                printf("PARENT: Active children count updated to %d.\n", active_children_count);
                fflush(stdout);
            }


            if (active_children_count == 0) {
                break; // All children have terminated
            }

            // --- C. Select the next process to run (Round Robin) ---
            int search_start_idx;
            if (current_running_idx != -1) {
                search_start_idx = (current_running_idx + 1) % num_children_launched;
            } else { // No process was running (e.g., previous one terminated)
                search_start_idx = 0; // Start search from the beginning
                // Or, find the next non-terminated from a known point if desired.
                // For simplicity, we can just iterate from 0.
            }
            
            int next_idx_to_run = -1;
            for (int i = 0; i < num_children_launched; ++i) {
                int temp_idx = (search_start_idx + i) % num_children_launched;
                if (process_states[temp_idx] == PROC_INITIALIZING || process_states[temp_idx] == PROC_STOPPED) {
                    next_idx_to_run = temp_idx;
                    break;
                }
            }
            
            // If all remaining processes are already PROC_RUNNING (should not happen if SIGSTOP worked)
            // or if all are TERMINATED (handled by active_children_count check),
            // this loop might not find one. If next_idx_to_run is still -1 but active_children_count > 0,
            // it means all non-terminated processes are in a state not handled above (e.g. all RUNNING)
            // which implies an issue in state transition or that only one process is left and it's already running.
            if (next_idx_to_run == -1 && active_children_count > 0) {
                 // Attempt to find any non-terminated process that is not currently running
                for (int i = 0; i < num_children_launched; ++i) {
                    if (process_states[i] != PROC_TERMINATED && i != current_running_idx) {
                         if (process_states[i] == PROC_INITIALIZING || process_states[i] == PROC_STOPPED) {
                            next_idx_to_run = i;
                            break;
                         }
                    }
                }
                // If still -1, and current_running_idx is valid and RUNNING, just re-alarm for it.
                // Or if current_running_idx is valid and STOPPED, make it the next to run.
                if (next_idx_to_run == -1 && current_running_idx != -1 && process_states[current_running_idx] != PROC_TERMINATED) {
                    next_idx_to_run = current_running_idx; // Reschedule the current one if it's the only option
                }
            }


            // --- D. Start/Continue the selected process ---
            if (next_idx_to_run != -1) {
                current_running_idx = next_idx_to_run;
                printf("PARENT: Scheduling next child (PID: %d, Index: %d, State: %d).\n", pid_arr[current_running_idx], current_running_idx, process_states[current_running_idx]);
                fflush(stdout);

                if (process_states[current_running_idx] == PROC_INITIALIZING) {
                    if (kill(pid_arr[current_running_idx], SIGUSR1) == -1) {
                        perror("PARENT: kill (SIGUSR1) for scheduled child failed");
                        process_states[current_running_idx] = PROC_TERMINATED; // Assume failed to start
                        active_children_count--;
                        current_running_idx = -1; // Force re-evaluation
                    } else {
                         process_states[current_running_idx] = PROC_RUNNING;
                    }
                } else if (process_states[current_running_idx] == PROC_STOPPED) {
                    if (kill(pid_arr[current_running_idx], SIGCONT) == -1) {
                        perror("PARENT: kill (SIGCONT) for scheduled child failed");
                         if (errno == ESRCH) {
                            process_states[current_running_idx] = PROC_TERMINATED;
                            active_children_count--;
                            current_running_idx = -1;
                         }
                    } else {
                        process_states[current_running_idx] = PROC_RUNNING;
                    }
                }
                // If it's already PROC_RUNNING, it means it's the only active child left.

                if (process_states[current_running_idx] == PROC_RUNNING) {
                    alarm(TIME_SLICE_SECONDS);
                } else if (active_children_count > 0) {
                    // If the selected process couldn't be started/continued and marked terminated,
                    // try to re-arm alarm to find another one.
                    alarm(1); // Short alarm to re-trigger scheduling logic quickly
                }

            } else if (active_children_count > 0) {
                // No runnable process found, but children are still active.
                // This might happen if all remaining active children are in a state
                // that wasn't picked (e.g., all are somehow still PROC_RUNNING without being current_running_idx).
                // Or if the current_running_idx was the only one and it just got stopped.
                // Re-arm the alarm to try scheduling again.
                printf("PARENT: No specific next child to run, but active children remain. Re-arming alarm.\n");
                fflush(stdout);
                alarm(TIME_SLICE_SECONDS);
            }
            // If active_children_count is 0, the loop will exit.
        }
        // else: signal was not SIGALRM, loop around with pause()
    }

    printf("PARENT (PID: %d): All child processes have completed. MCP exiting.\n", getpid());
    fflush(stdout);
    return 0;
}
