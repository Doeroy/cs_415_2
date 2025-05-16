#define _GNU_SOURCE // Must be the first line for getline, strdup (if used in str_filler)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h> // For waitpid and status macros
#include <signal.h>   // For sigset_t, sigemptyset, sigaddset, sigprocmask, sigwait, kill, signals, sigaction
#include <time.h>     // For nanosleep (optional, for precise sleeps) or time()
#include "string_parser.h"
#include <unistd.h> // For fork, execvp, _exit, getpid, sleep, alarm, pause, sysconf
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
char *command_display_names[MAX_COMMANDS]; // For Part 4: Store command names for display
int num_children_launched = 0;
int current_running_idx = -1;
int active_children_count = 0;

// For Part 4: System constants for /proc parsing
long clk_tck;
long page_size;

void sigalrm_handler(int signum) {
    if (signum == SIGALRM) {
        sigalrm_fired = 1;
    }
}

void sigchld_handler(int signum) {
    if (signum == SIGCHLD) {
        sigchld_fired = 1;
    }
}

// PART 4: Function to display process statistics
void display_process_stats() {
    printf("\033[H\033[J"); // Clear screen and move cursor to top-left

    printf("--- MCP Process Dashboard (Parent PID: %d) ---\n", getpid());
    printf("%-5s | %-20s | %-5s | %-8s | %-8s | %-12s | %-12s\n",
           "PID", "Command", "State", "UTime(s)", "STime(s)", "VmSize(B)", "RSS(B)");
    printf("-----|----------------------|-------|----------|----------|--------------|--------------\n");

    for (int i = 0; i < num_children_launched; i++) {
        if (process_states[i] == PROC_TERMINATED) {
            printf("%-5d | %-20.20s | %-5s | %-8s | %-8s | %-12s | %-12s\n",
                   pid_arr[i],
                   command_display_names[i] ? command_display_names[i] : "N/A",
                   "TERM", "-", "-", "-", "-");
            continue;
        }

        char proc_stat_path[256];
        sprintf(proc_stat_path, "/proc/%d/stat", pid_arr[i]);
        FILE *f_stat = fopen(proc_stat_path, "r");

        if (f_stat) {
            char stat_line[1024];
            if (fgets(stat_line, sizeof(stat_line), f_stat) != NULL) {
                char state_val;
                unsigned long utime_ticks, stime_ticks, vsize_bytes_val;
                long rss_pages_val;
                char *s_ptr = strrchr(stat_line, ')');

                if (s_ptr == NULL) {
                    printf("%-5d | %-20.20s | %-5s | %-8s | %-8s | %-12s | %-12s (ParseErr1)\n",
                           pid_arr[i], command_display_names[i] ? command_display_names[i] : "N/A",
                           "ERR", "ERR", "ERR", "ERR", "ERR");
                    fclose(f_stat);
                    continue;
                }
                s_ptr += 2; // Skip ") "

                int items_scanned = sscanf(s_ptr,
                    "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %lu %ld",
                    &state_val,
                    &utime_ticks,
                    &stime_ticks,
                    &vsize_bytes_val,
                    &rss_pages_val);

                if (items_scanned >= 5) {
                    double utime_sec = (double)utime_ticks / clk_tck;
                    double stime_sec = (double)stime_ticks / clk_tck;
                    long rss_bytes_val = rss_pages_val * page_size;

                    printf("%-5d | %-20.20s | %-5c | %-8.2f | %-8.2f | %-12lu | %-12ld\n",
                           pid_arr[i],
                           command_display_names[i] ? command_display_names[i] : "N/A",
                           state_val,
                           utime_sec,
                           stime_sec,
                           vsize_bytes_val,
                           rss_bytes_val);
                } else {
                    printf("%-5d | %-20.20s | %-5s | %-8s | %-8s | %-12s | %-12s (ParseErr2:%d)\n",
                           pid_arr[i], command_display_names[i] ? command_display_names[i] : "N/A",
                           "ERR", "ERR", "ERR", "ERR", "ERR", items_scanned);
                }
            } else { // fgets failed
                 printf("%-5d | %-20.20s | %-5s | %-8s | %-8s | %-12s | %-12s (ReadErr)\n",
                           pid_arr[i], command_display_names[i] ? command_display_names[i] : "N/A",
                           "ERR", "ERR", "ERR", "ERR", "ERR");
            }
            fclose(f_stat);
        } else { // fopen failed
            printf("%-5d | %-20.20s | %-5s | %-8s | %-8s | %-12s | %-12s (NoProc)\n",
                   pid_arr[i],
                   command_display_names[i] ? command_display_names[i] : "N/A",
                   (process_states[i] == PROC_INITIALIZING || process_states[i] == PROC_STOPPED || process_states[i] == PROC_RUNNING) ? "WAIT" : "N/A",
                   "N/A", "N/A", "N/A", "N/A");
        }
    }
    printf("------------------------------------------------------------------------------------------\n");
    printf("Total Procs: %d | Active: %d | Running PID: %d (Idx: %d, State: %s)\n",
           num_children_launched, active_children_count,
           (current_running_idx != -1 && pid_arr[current_running_idx] > 0) ? pid_arr[current_running_idx] : 0,
           current_running_idx,
           (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) ? "RUN" : ((current_running_idx != -1 && process_states[current_running_idx] == PROC_STOPPED) ? "STOP" : "N/A")
           );
    fflush(stdout);
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
    } else { // First time or previous was terminated
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
    
    // If no stopped/initializing found, look for an already running one (should not happen if one was just stopped)
    // Or if only one process remains and it's running
    if (next_idx_to_run == -1) {
        for (int i = 0; i < num_children_launched; ++i) {
            if (process_states[i] == PROC_RUNNING) { // Should be the one just stopped, or the only one left.
                next_idx_to_run = i;
                break;
            }
        }
    }


    if (next_idx_to_run != -1) {
        current_running_idx = next_idx_to_run;
        // Note: Detailed printf messages from Part 3 are removed here for cleaner Part 4 display
        // They could be logged to stderr if needed.

        if (process_states[current_running_idx] == PROC_INITIALIZING) {
            if (kill(pid_arr[current_running_idx], SIGUSR1) == -1) {
                perror("PARENT: kill (SIGUSR1) for new child failed");
                // This child might be dead, mark and handle
                process_states[current_running_idx] = PROC_TERMINATED;
                active_children_count--;
                current_running_idx = -1; // Force re-evaluation
                sigalrm_fired = 1; // Reschedule immediately
                return;
            }
            process_states[current_running_idx] = PROC_RUNNING;
        } else if (process_states[current_running_idx] == PROC_STOPPED) {
            if (kill(pid_arr[current_running_idx], SIGCONT) == -1) {
                if (errno == ESRCH) { // Process already gone
                    process_states[current_running_idx] = PROC_TERMINATED;
                    active_children_count--; // This should ideally be caught by SIGCHLD
                } else {
                    perror("PARENT: kill (SIGCONT) for stopped child failed");
                }
                current_running_idx = -1; // Force re-evaluation
                sigalrm_fired = 1; // Reschedule immediately
                return;
            }
            process_states[current_running_idx] = PROC_RUNNING;
        }
        
        if (process_states[current_running_idx] == PROC_RUNNING) {
             alarm(TIME_SLICE_SECONDS);
        }

    } else if (active_children_count > 0) {
        // This case means all remaining active processes are already PROC_RUNNING (e.g. only one left)
        // or something went wrong. If current_running_idx is valid and running, just reset its alarm.
        if (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
            alarm(TIME_SLICE_SECONDS);
        } else {
            // No runnable process found, but active_children_count > 0. This is an anomaly.
            // Try to find any running process to set an alarm for, or trigger SIGALRM to re-evaluate.
            int found_running = 0;
            for(int i=0; i<num_children_launched; ++i){
                if(process_states[i] == PROC_RUNNING){
                    current_running_idx = i;
                    alarm(TIME_SLICE_SECONDS);
                    found_running = 1;
                    break;
                }
            }
            if(!found_running) alarm(0); // No one to run.
        }
    } else { // active_children_count == 0
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
            // Part 3 detailed printf removed for cleaner Part 4 display
            process_states[found_idx] = PROC_TERMINATED;
            active_children_count--;

            if (current_running_idx == found_idx) {
                alarm(0); // Cancel alarm for the terminated process
                current_running_idx = -1; // It's gone
                sigalrm_fired = 1; // Trigger scheduler immediately to pick a new process
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

    // PART 4: Initialize clk_tck and page_size
    clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        perror("sysconf _SC_CLK_TCK failed or returned invalid value, defaulting to 100");
        clk_tck = 100;
    }
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        perror("sysconf _SC_PAGESIZE failed or returned invalid value, defaulting to 4096");
        page_size = 4096;
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

    // Initialize command_display_names to NULL
    for(int i=0; i<MAX_COMMANDS; ++i) command_display_names[i] = NULL;

    // Forking child processes
    while ((getline(&line, &len, open_file)) != -1) {
        command_line token_buffer = str_filler(line, " \t\n"); // Use space, tab, newline as delimiters
        
        if (token_buffer.num_token == 0) { // Skip empty lines
            free_command_line(&token_buffer); // str_filler might allocate even for no tokens
            continue;
        }
        if (num_children_launched >= MAX_COMMANDS) {
            fprintf(stderr, "PARENT: Max commands (%d) reached. Skipping: %s\n", MAX_COMMANDS, line);
            free_command_line(&token_buffer);
            continue;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            perror("PARENT: Fork failed");
            free_command_line(&token_buffer); // Free tokens for this command
            // Potentially skip this command and continue
        } else if (child_pid == 0) { // Child process
            sigset_t set_for_usr1;
            int recvd_sig;
            sigemptyset(&set_for_usr1);
            sigaddset(&set_for_usr1, SIGUSR1);
            if (sigprocmask(SIG_BLOCK, &set_for_usr1, NULL) == -1) {
                perror("CHILD: sigprocmask block SIGUSR1 failed");
                free_command_line(&token_buffer);
                _exit(errno);
            }
            // Child waits for SIGUSR1 before exec-ing
            if (sigwait(&set_for_usr1, &recvd_sig) != 0) {
                fprintf(stderr, "CHILD (PID: %d): sigwait failed with error %d.\n", getpid(), errno);
                free_command_line(&token_buffer);
                _exit(errno);
            }
            if (recvd_sig == SIGUSR1) {
                // Unblock SIGUSR1 before exec
                if(sigprocmask(SIG_UNBLOCK, &set_for_usr1, NULL) == -1){
                    perror("CHILD: sigprocmask unblock SIGUSR1 failed");
                    // Not fatal, proceed to exec
                }
            } else {
                 fprintf(stderr, "CHILD (PID: %d): Received unexpected signal %d via sigwait.\n", getpid(), recvd_sig);
                 free_command_line(&token_buffer);
                 _exit(1);
            }

            if (execvp(token_buffer.command_list[0], token_buffer.command_list) == -1) {
                perror("CHILD: execvp failed");
                fprintf(stderr, "CHILD: Failed command: '%s'\n", token_buffer.command_list[0]);
                free_command_line(&token_buffer);
                _exit(errno); // Exit with execvp's errno
            }
        } else { // Parent process
            pid_arr[num_children_launched] = child_pid;
            process_states[num_children_launched] = PROC_INITIALIZING;

            // PART 4: Store command name
            if (token_buffer.num_token > 0 && token_buffer.command_list[0] != NULL) {
                command_display_names[num_children_launched] = strdup(token_buffer.command_list[0]);
                if (command_display_names[num_children_launched] == NULL) {
                    perror("PARENT: strdup for command_display_name failed");
                    command_display_names[num_children_launched] = strdup("?"); // Fallback
                }
            } else {
                command_display_names[num_children_launched] = strdup("?"); // Should not happen if num_token > 0
            }
            num_children_launched++;
            free_command_line(&token_buffer); // Parent frees its copy
        }
    }
    fclose(open_file);
    if (line) free(line);

    active_children_count = num_children_launched;
    if (num_children_launched == 0) {
        printf("PARENT: No commands to run. Exiting.\n");
        return 0;
    }
    
    // PART 4: Initial display of stats
    display_process_stats();
    // A small pause to see the initial table before scheduling starts
    // This might be removed or made conditional if rapid startup is needed.
    // sleep(1); // This sleep might interfere with quick startup/termination of initial processes

    sigalrm_fired = 1; // Trigger the first scheduling event to start a process

    // Main scheduling loop
    while (active_children_count > 0) {
        if (sigchld_fired) {
            sigchld_fired = 0;
            handle_terminated_children();
            if (active_children_count > 0) { // Update display if children still active
                display_process_stats();
            } else { // All children terminated
                display_process_stats(); // Show final "TERM" states
                break; 
            }
        }

        if (sigalrm_fired) {
            sigalrm_fired = 0;
            if (active_children_count == 0) break; // All children might have terminated

            if (current_running_idx != -1 && process_states[current_running_idx] == PROC_RUNNING) {
                if (kill(pid_arr[current_running_idx], SIGSTOP) == 0) {
                    process_states[current_running_idx] = PROC_STOPPED;
                } else {
                    if (errno == ESRCH) { // Process already terminated
                         // This should be caught by SIGCHLD and handle_terminated_children
                         // but we can defensively mark it here.
                        if(process_states[current_running_idx] != PROC_TERMINATED){
                           // process_states[current_running_idx] = PROC_TERMINATED;
                           // active_children_count--; // SIGCHLD handler should do this.
                        }
                    } else {
                        perror("PARENT: kill (SIGSTOP) failed for currently running child");
                    }
                }
            }
            
            // It's possible a SIGSTOP revealed a terminated process not yet caught by SIGCHLD
            // handle_terminated_children(); // Call again to be sure, or rely on next sigchld_fired flag check

            select_and_run_next_process();
            display_process_stats(); // Display stats after scheduling decision
        }
        
        // Only pause if no signals are flagged and there are active children.
        // This prevents missing a signal that arrived just before pause().
        if (active_children_count > 0 && !sigalrm_fired && !sigchld_fired) {
            pause();
        }
    }

    printf("\033[H\033[J"); // Clear screen one last time
    printf("PARENT (PID: %d): All child processes have completed. MCP exiting.\n", getpid());

    // PART 4: Free allocated command names
    for (int i = 0; i < num_children_launched; i++) {
        if (command_display_names[i] != NULL) {
            free(command_display_names[i]);
        }
    }

    return 0;
}