
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WT_DEFAULT_DATA_DIR ".local/share/wt"

#define WEIGHT_HISTORY_DEFAULT_FILE ".local/share/wt/weight_history.csv"

#define FILE_PATH_MAX_SIZE 128

enum wt_cmd_tag {
  WT_CMD_LOG_WEIGHT,
};

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
    char const *home = getenv("HOME");
    int length = snprintf(cmd->log_weight_args.file_path, FILE_PATH_MAX_SIZE,
                          "%s/%s", home, WEIGHT_HISTORY_DEFAULT_FILE);
    if (length == FILE_PATH_MAX_SIZE) {
      res = -1;
      goto exit;
    }
    res = 0;
    goto exit;
  }
exit:
  return res;
}

static int wt_init(void) {
  int res = 0;
  char const *home = getenv("HOME");
  char data_dir[FILE_PATH_MAX_SIZE];
  int length =
      snprintf(data_dir, sizeof(data_dir), "%s/%s", home, WT_DEFAULT_DATA_DIR);
  if (length == sizeof(data_dir)) {
    res = -1;
    goto exit;
  }
  res = mkdir(data_dir, S_IRWXU);
  if (res < 0) {
    if (errno == EEXIST) {
      res = 0;
    } else {
      res = -1;
    }
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
  res = wt_init();
  if (res != 0) {
    fprintf(stderr, "init failed\n");
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
