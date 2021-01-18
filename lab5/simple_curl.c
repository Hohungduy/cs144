/* DOWNLOAD THIS FILE AND SAVE AS simple_curl.c. DO NOT COPY-PASTE THIS FILE! */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/************ DO NOT CHANGE THIS VALUE OR YOUR SUBMISSION WILL FAIL ***********/
#define MAGIC "554beec6e4d00062621e"
#define GRADING_SERVER "184.72.104.217"

/* Used for pipes. */
#define NUM_PIPES          2
#define PARENT_WRITE_PIPE  0
#define PARENT_READ_PIPE   1

int pipes[NUM_PIPES][2];

#define READ_FD  0
#define WRITE_FD 1
#define PARENT_READ_FD  ( pipes[PARENT_READ_PIPE][READ_FD]   )
#define PARENT_WRITE_FD ( pipes[PARENT_WRITE_PIPE][WRITE_FD] )
#define CHILD_READ_FD   ( pipes[PARENT_WRITE_PIPE][READ_FD]  )
#define CHILD_WRITE_FD  ( pipes[PARENT_READ_PIPE][WRITE_FD]  )

int main(int argc, char *argv[]) {
  /* Get program name and parse args. */
  char *progname = strrchr(argv[0], '/');
  if (progname)
    progname++;
  else
    progname = argv[0];

  if (argc != 2) {
    printf("Usage: %s <url>\n", progname);
    exit(1);
  }

  /* Get URL. */
  char *url = argv[1];

  int outfd[2];
  int infd[2];
  pipe(pipes[PARENT_READ_PIPE]);
  pipe(pipes[PARENT_WRITE_PIPE]);

  int pid = fork();

  if (!pid) {
    char url_with_port[200];
    memset(url_with_port, 0, 200);
    snprintf(url_with_port, 200, "%s:80", url);
    srand(time(NULL));
    int port = rand() % 15360 + 1023;
    char port_str[6];
    memset(port_str, 0, 6);
    snprintf(port_str, 6, "%d", port);
    char *argv[] = { "./ctcp/ctcp", "-c", url_with_port, "-p", port_str, 0 };

    dup2(CHILD_READ_FD, STDIN_FILENO);
    dup2(CHILD_WRITE_FD, STDOUT_FILENO);
    close(CHILD_READ_FD);
    close(CHILD_WRITE_FD);
    close(PARENT_READ_FD);
    close(PARENT_WRITE_FD);

    execv(argv[0], argv);
  }
  else {
    close(CHILD_READ_FD);
    close(CHILD_WRITE_FD);

    int flags = fcntl(PARENT_READ_FD, F_GETFL, 0);
    fcntl(PARENT_READ_FD, F_SETFL, flags | O_NONBLOCK);

    char buffer[100];
    int count;
    time_t last_time = time(NULL);

    /* Write GET request. */
    write(PARENT_WRITE_FD, "GET / HTTP/1.1\r\n", 16);
    char url_string[200];
    memset(url_string, 0, 200);
    snprintf(url_string, 200, "Host: %s\r\n", url);
    write(PARENT_WRITE_FD, url_string, strlen(url_string));
    char magic_string[29];
    snprintf(magic_string, 29, "Magic: %s\n", MAGIC);
    if (strcmp(GRADING_SERVER, url) == 0)
      write(PARENT_WRITE_FD, magic_string, strlen(magic_string));
    write(PARENT_WRITE_FD, "\n", 1);

    /* Read from webpage. If no response in a while, terminate. */
    while (1) {
      time_t curr_time = time(NULL);
      int diff = difftime(curr_time, last_time);
      if (diff >= 5)
        break;

      count = read(PARENT_READ_FD, buffer, sizeof(buffer) - 1);
      if (count >= 0) {
          last_time = curr_time;
          buffer[count] = 0;
          printf("%s", buffer);
      }
    }

    /* Write out FIN. */
    char fin[1] = { 0x1a };
    write(PARENT_WRITE_FD, fin, 1);
    write(PARENT_WRITE_FD, "\n", 1);
    kill(pid, SIGKILL);
    exit(0);
  }
}


