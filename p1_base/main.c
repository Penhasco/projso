#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

int readFile(int fd, int fdOut);

int main(int argc, char *argv[]) {

  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  DIR *dir;
  struct dirent *dp;
  int fd, fdOut;
  char path[128];
  char out_filepath[128];

  if (argc < 2) {

    fprintf(stderr, "Not enough arguments\n");
    return 1;
    
  }

  else if (argc > 2) {

    char *endptr;
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  dir = opendir(argv[1]);


  if (dir == NULL){

    perror("No such folder");
    exit(1);
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  for(;;){

    strcpy(path, argv[1]);
    strcat(path, "/");

    dp = readdir(dir);

    if(dp == NULL){
      break;
    }

    if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0){
      continue;
    }

    if (strstr(dp->d_name, ".jobs") == NULL) {

      continue;
    }

    printf("%s\n", dp -> d_name);

    strcat(path, dp -> d_name);

    strcpy(out_filepath, path);

    strcpy(strrchr(out_filepath, '.'), ".out");

    fd = open(path, O_RDONLY);

    

    fdOut = open(out_filepath, O_CREAT| O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);

    readFile(fd, fdOut);
  }

  ems_terminate();
  closedir(dir);
}


  
int readFile(int fd, int fdOut){

  while (1) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    fflush(stdout);

    switch (get_next(fd)) {
      case CMD_CREATE:
        if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
        }

        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) {
          fprintf(stderr, "Failed to reserve seats\n");
        }

        break;

      case CMD_SHOW:
        if (parse_show(fd, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_show(event_id, fdOut)) {
          fprintf(stderr, "Failed to show event\n");
        }

        break;

      case CMD_LIST_EVENTS:
        if (ems_list_events(fdOut)) {
          fprintf(stderr, "Failed to list events\n");
        }

        break;

      case CMD_WAIT:
        if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
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
        printf(//FAZER WRITE PARA DENTRO DO FICHEIRO
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
    
        return 0;
    }
  }
}

