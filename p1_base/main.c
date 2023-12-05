#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

void process_file(const char *filename);
void report_state(const char *filename);

int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
    DIR *dir;
    struct dirent *dp;

    if (argc < 2) {
        fprintf(stderr, "Not enough arguments\n");
        return 1;
    } else if (argc > 2) {
        char *endptr;
        unsigned long int delay = strtoul(argv[2], &endptr, 10);

        if (*endptr != '\0' || delay > UINT_MAX) {
            fprintf(stderr, "Invalid delay value or value too large\n");
            return 1;
        }

        state_access_delay_ms = (unsigned int)delay;
    }

    dir = opendir(argv[1]);

    if (dir == NULL) {
        perror("No such folder");
        exit(1);
    }

    for (;;) {
        dp = readdir(dir);

        if (dp == NULL) {
            break;
        }

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        printf("Processing file: %s\n", dp->d_name);

        // Construct the full path of the ".jobs" file
        char filepath[PATH_MAX];
        snprintf(filepath, PATH_MAX, "%s/%s", argv[1], dp->d_name);

        // Process the commands from the ".jobs" file
        process_file(filepath);

        // Report the state by creating a corresponding ".out" file
        report_state(filepath);
    }

    if (ems_init(state_access_delay_ms)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    while (1) {
        unsigned int event_id, delay;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

        printf("> ");
        fflush(stdout);

        switch (get_next(STDIN_FILENO)) {
            case CMD_CREATE:
                if (parse_create(STDIN_FILENO, &event_id, &num_rows, &num_columns) != 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_create(event_id, num_rows, num_columns)) {
                    fprintf(stderr, "Failed to create event\n");
                }

                break;

            case CMD_RESERVE:
                num_coords = parse_reserve(STDIN_FILENO, MAX_RESERVATION_SIZE, &event_id, xs, ys);

                if (num_coords == 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_reserve(event_id, num_coords, xs, ys)) {
                    fprintf(stderr, "Failed to reserve seats\n");
                }

                break;

            case CMD_SHOW:
                if (parse_show(STDIN_FILENO, &event_id) != 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_show(event_id)) {
                    fprintf(stderr, "Failed to show event\n");
                }

                break;

            case CMD_LIST_EVENTS:
                if (ems_list_events()) {
                    fprintf(stderr, "Failed to list events\n");
                }

                break;

            case CMD_WAIT:
                if (parse_wait(STDIN_FILENO, &delay, NULL) == -1) {  // thread_id is not implemented
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (delay > 0) {
                    printf("Waiting...\n");
                    ems_wait(delay);
                }

                break;

            case CMD_INVALID:
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                break;

            case CMD_HELP:
                printf(
                    "Available commands:\n"
                    "  CREATE <event_id> <num_rows> <num_columns>\n"
                    "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                    "  SHOW <event_id>\n"
                    "  LIST\n"
                    "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
                    "  BARRIER\n"                      // Not implemented
                    "  HELP\n");

                break;

            case CMD_BARRIER:  // Not implemented
            case CMD_EMPTY:
                break;

            case EOC:
                ems_terminate();
                return 0;
        }
    }
}

void process_file(const char *filename) {
    // Open the file for reading
    int file = open(filename, O_RDONLY);
    if (file == -1) {
        perror("Error opening file");
        return;
    }

    // Save the current standard input
    int saved_stdin = dup(STDIN_FILENO);

    // Redirect standard input to the file
    dup2(file, STDIN_FILENO);


    // Restore the standard input
    dup2(saved_stdin, STDIN_FILENO);

    // Close the file
    close(file);
}

void report_state(const char *filename) {
    // Construct the full path of the ".out" file
    char out_filepath[PATH_MAX];
    snprintf(out_filepath, PATH_MAX, "%s.out", filename);

    // Open the ".out" file for writing
    int out_file = open(out_filepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (out_file == -1) {
        perror("Error opening .out file");
        return;
    }

    // Create a pipe for capturing the output
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Error creating pipe");
        return;
    }

    // Fork to create a child process
    pid_t pid = fork();

    if (pid == -1) {
        perror("Error forking process");
        close(out_file);
        return;
    }

    if (pid == 0) {  // Child process
        // Close the write end of the pipe
        close(pipe_fd[1]);

        // Redirect standard input to the read end of the pipe
        dup2(pipe_fd[0], STDIN_FILENO);

        // Close the read end of the pipe
        close(pipe_fd[0]);

        // Redirect standard output to the ".out" file
        dup2(out_file, STDOUT_FILENO);

        // Execute ems_show (modify if necessary)
        unsigned int event_id;
        while (scanf("%u", &event_id) == 1) {
            ems_show(event_id);
        }

        // Close standard input
        close(STDIN_FILENO);

        // Exit the child process
        _exit(EXIT_SUCCESS);
    } else {  // Parent process
        // Close the read end of the pipe
        close(pipe_fd[0]);

        // Redirect standard output to the write end of the pipe
        dup2(pipe_fd[1], STDOUT_FILENO);

        // Close the write end of the pipe
        close(pipe_fd[1]);

        // Open the ".jobs" file for reading
        int file = open(filename, O_RDONLY);
        if (file == -1) {
            perror("Error opening file");
            close(out_file);
            return;
        }

        // Redirect standard input to the file
        dup2(file, STDIN_FILENO);

        // Execute ems_show for each event in the ".jobs" file
        unsigned int event_id;
        while (scanf("%u", &event_id) == 1) {
            ems_show(event_id);
        }

        // Close standard input and the file
        close(STDIN_FILENO);
        close(file);

        // Wait for the child process to complete
        waitpid(pid, NULL, 0);

        // Close standard output
        close(STDOUT_FILENO);
        // Close the ".out" file
        close(out_file);
    }
}