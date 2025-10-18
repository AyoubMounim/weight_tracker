
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum wt_cmd_tag {
  WT_CMD_LOG_WEIGHT,
};

#define FILE_PATH_MAX_SIZE 64
struct wt_cmd_log_weight_args {
  float weight;
  char file_path[FILE_PATH_MAX_SIZE];
};

struct wt_cmd {
  enum wt_cmd_tag tag;
  int (*execute_func)(void const *);
  union {
    struct wt_cmd_log_weight_args log_weight_args;
  };
};

static int wt_cmd_execute(struct wt_cmd const *cmd) {
  int res = -1;
  switch (cmd->tag) {
  case WT_CMD_LOG_WEIGHT:
    res = cmd->execute_func((void *)&cmd->log_weight_args);
    break;
  default:
    res = -1;
    break;
  }
  return res;
}

static int log_weight_get_fd(char const *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU);
  //  int fd = fileno(stdout);
  return fd;
}

static size_t log_weight_format_std(time_t unix_time, float weight,
                                    size_t buff_size, char buff[buff_size]) {
  char date_buff[32];
  strftime(date_buff, sizeof(date_buff), "%d/%m/%Y", localtime(&unix_time));
  return snprintf(buff, buff_size, "%s,%.2f\n", date_buff, weight);
}

static int log_weight(void const *args) {
  int res = 0;
  struct wt_cmd_log_weight_args const *log_weight_args = args;
  int fd = log_weight_get_fd(log_weight_args->file_path);
  if (fd < 0) {
    res = -1;
    goto exit;
  }
  char buff[32];
  size_t length = log_weight_format_std(time(NULL), log_weight_args->weight,
                                        sizeof(buff), buff);
  write(fd, buff, length);
  close(fd);
exit:
  return res;
}

static int parse_args(int argc, char *argv[], struct wt_cmd *cmd) {
  int res = -1;
  if (argc != 3) {
    res = -1;
    goto exit;
  }
  if (strcmp(argv[1], "log") == 0) {
    cmd->tag = WT_CMD_LOG_WEIGHT;
    cmd->execute_func = log_weight;
    cmd->log_weight_args.weight = strtof(argv[2], NULL);
    snprintf(cmd->log_weight_args.file_path, FILE_PATH_MAX_SIZE, "%s",
             "/home/ayoub/.local/share/wt/data.csv");
    res = 0;
    goto exit;
  }
exit:
  return res;
}

int main(int argc, char *argv[]) {
  struct wt_cmd cmd;
  int res = parse_args(argc, argv, &cmd);
  if (res != 0) {
    fprintf(stderr, "args parse failed\n");
    goto exit;
  }
  res = wt_cmd_execute(&cmd);
  if (res != 0) {
    fprintf(stderr, "cmd execution failed\n");
    goto exit;
  }

exit:
  return 0;
}
