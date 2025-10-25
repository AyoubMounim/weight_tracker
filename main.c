
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WT_DEFAULT_DATA_DIR ".local/share/wt"
#define WEIGHT_HISTORY_DEFAULT_FILE ".local/share/wt/weight_history.csv"

#define WT_AVG_DEFAULT_WINDOW_LENGTH_DAYS 7

#define FILE_PATH_MAX_SIZE 128

enum wt_cmd_tag {
  WT_CMD_LOG_WEIGHT,
  WT_CMD_AVG,
  WT_CMD_STATS,
  WT_CMDS_NUMBER,
};

struct wt_cmd_log_weight_args {
  float weight;
  char file_path[FILE_PATH_MAX_SIZE];
};

struct wt_cmd_avg_args {
  uint8_t avg_window_days;
  char file_path[FILE_PATH_MAX_SIZE];
};

struct wt_cmd_stats_args {
  uint8_t avg_window_days;
  char file_path[FILE_PATH_MAX_SIZE];
};

struct wt_cmd {
  enum wt_cmd_tag tag;
  int (*execute_func)(void const *);
  union {
    struct wt_cmd_log_weight_args log_weight_args;
    struct wt_cmd_avg_args avg_args;
    struct wt_cmd_stats_args stats_args;
  };
};

typedef float speed; ///< kg/day.

struct wt_stats {
  speed rate_of_change;
};

struct linear_fit_coeff {
  float m;
  float q;
};

static float compute_skx(size_t data_length, float const x[data_length],
                         uint32_t k) {
  float res = 0;
  for (size_t i = 0; i < data_length; i++) {
    res += powf(x[i], k);
  }
  return res;
}

static float compute_skxy(size_t data_length, float const x[data_length],
                          float const y[data_length], uint32_t k) {
  float res = 0;
  for (size_t i = 0; i < data_length; i++) {
    res += powf(x[i], k) * y[i];
  }
  return res;
}

static float compute_s0xy(size_t data_length, float const x[data_length],
                          float const y[data_length]) {
  float res = 0;
  for (size_t i = 0; i < data_length; i++) {
    res += y[i];
  }
  return res;
}

static int linear_fit(size_t data_length, float const data[data_length],
                      struct linear_fit_coeff *linear_fit) {
  if (data_length == 0) {
    return -1;
  }
  float *x = calloc(data_length, sizeof(*x));
  for (size_t i = 0; i < data_length; i++) {
    x[i] = i;
  }
  float const s0x = (float)data_length;
  float const s1x = data_length*(data_length-1)/2.0;
  float const s2x = data_length*(data_length-1)*(2*data_length - 1)/6.0;
  float const s0xy = compute_s0xy(data_length, x, data);
  float const s1xy = compute_skxy(data_length, x, data, 1);
  linear_fit->m = (s0x * s1xy - s1x * s0xy) / (s0x * s2x - s1x * s1x);
  linear_fit->q = (s0xy * s2x - s1xy * s1x) / (s0x * s2x - s1x * s1x);
  return 0;
}

static int wt_stats_from_history(struct wt_stats *self, size_t history_length,
                                 float const history[history_length]) {
  struct linear_fit_coeff lfit = {0};
  if (linear_fit(history_length, history, &lfit) < 0) {
    return -1;
  }
  self->rate_of_change = lfit.m;
  return 0;
}

static void wt_stats_print(struct wt_stats const *self) {
  printf("===\n"
         "[Stats]\n"
         "  rate of change: %.2f Kg/day\n"
         "===\n",
         self->rate_of_change);
}

static int wt_cmd_execute(struct wt_cmd const *cmd) {
  int res = -1;
  switch (cmd->tag) {
  case WT_CMD_LOG_WEIGHT:
    res = cmd->execute_func((void *)&cmd->log_weight_args);
    break;
  case WT_CMD_AVG:
    res = cmd->execute_func((void *)&cmd->avg_args);
    break;
  case WT_CMD_STATS:
    res = cmd->execute_func((void *)&cmd->stats_args);
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

static ssize_t wt_get_weight_history(char const *history_file_path,
                                     float **history) {
  int res = 0;
  FILE *f = fopen(history_file_path, "r");
  if (f == NULL) {
    res = -1;
    goto exit;
  }
  char *line = NULL;
  size_t length = 0;
  ssize_t r;
  size_t history_length = 0;
  size_t history_capacity = 128;
  *history = calloc(history_capacity, sizeof(**history));
  if (*history == NULL) {
    res = -1;
    goto cleanup;
  }
  while ((r = getline(&line, &length, f)) >= 0) {
    char const *date = strtok(line, ",");
    char const *weight_str = strtok(NULL, ",");
    if (weight_str == NULL) {
      continue;
    }
    float const weight = strtof(weight_str, NULL);
    if (history_length == history_capacity) {
      history_capacity *= 2;
      *history = realloc(*history, history_capacity);
      if (*history == NULL) {
        res = -1;
        goto cleanup;
      }
    }
    (*history)[history_length++] = weight;
  }
  res = history_length;
cleanup:
  fclose(f);
  free(line);
exit:
  return res;
}

static void wt_free_weight_history(float **history) {
  free(*history);
  *history = NULL;
  return;
}

static ssize_t wt_moving_avg(size_t data_length, float data[data_length],
                             size_t avg_window_length, float **data_avg) {
  int res = 0;
  if (data_length < avg_window_length) {
    res = -1;
    goto exit;
  }
  *data_avg = calloc(data_length - avg_window_length + 1, sizeof(**data_avg));
  if (data_avg == NULL) {
    res = -1;
    goto exit;
  }
  for (size_t i = 0; i + avg_window_length - 1 < data_length; i++) {
    float sum = 0;
    for (size_t j = 0; j < avg_window_length; j++) {
      sum += data[i + j];
    }
    float const avg = sum / avg_window_length;
    (*data_avg)[i] = avg;
  }
  res = data_length - avg_window_length + 1;
exit:
  return res;
}

static void wt_free_moving_avg(float **data) {
  free(*data);
  *data = NULL;
  return;
}

static int avg(void const *args) {
  int res = 0;
  struct wt_cmd_avg_args const *avg_args = args;
  float *history = NULL;
  ssize_t history_length = wt_get_weight_history(avg_args->file_path, &history);
  if (history_length < 0) {
    res = -1;
    goto exit;
  }
  float *history_avg = NULL;
  ssize_t history_avg_length = wt_moving_avg(
      history_length, history, avg_args->avg_window_days, &history_avg);
  if (history_avg_length < 0) {
    res = -1;
    goto cleanup;
  }
  printf("===\n[Moving Average Weight History]\n");
  for (size_t i = 0; i < history_avg_length; i++) {
    printf("  %.2f Kg\n", history_avg[i]);
  }
  printf("===\n");
cleanup:
  wt_free_weight_history(&history);
  wt_free_moving_avg(&history_avg);
exit:
  return res;
}

static int stats(void const *args) {
  int res = 0;
  struct wt_cmd_stats_args const *stats_args = args;
  float *history;
  ssize_t history_length =
      wt_get_weight_history(stats_args->file_path, &history);
  if (history_length < 0) {
    res = -1;
    goto exit;
  }
  if (history_length < stats_args->avg_window_days) {
    printf("Not enough data to show stats.\n");
    res = 0;
    goto exit;
  }
  float *history_avg = NULL;
  ssize_t history_avg_length = wt_moving_avg(
      history_length, history, stats_args->avg_window_days, &history_avg);
  if (history_avg_length < 0) {
    res = -1;
    goto cleanup;
  }
  struct wt_stats stats;
  if (wt_stats_from_history(&stats, history_length, history) < 0) {
    res = -1;
    goto cleanup;
  }
  wt_stats_print(&stats);
  res = 0;
cleanup:
  wt_free_weight_history(&history);
  wt_free_moving_avg(&history_avg);
  free(history_avg);
exit:
  return res;
}

static int parse_args(int argc, char *argv[], struct wt_cmd *cmd) {
  int res = -1;
  if (argc < 2) {
    res = -1;
    goto exit;
  }
  if (strcmp(argv[1], "log") == 0) {
    if (argc == 3) {
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
    } else {
      assert(0 && "not implemented");
    }
    res = 0;
  } else if (strcmp(argv[1], "avg") == 0) {
    cmd->tag = WT_CMD_AVG;
    cmd->execute_func = avg;
    if (argc == 2) {
      cmd->avg_args.avg_window_days = WT_AVG_DEFAULT_WINDOW_LENGTH_DAYS;
      char const *home = getenv("HOME");
      int length = snprintf(cmd->avg_args.file_path, FILE_PATH_MAX_SIZE,
                            "%s/%s", home, WEIGHT_HISTORY_DEFAULT_FILE);
      if (length == FILE_PATH_MAX_SIZE) {
        res = -1;
        goto exit;
      }
    } else {
      assert(0 && "not implemented");
    }
    res = 0;
  } else if (strcmp(argv[1], "stats") == 0) {
    cmd->tag = WT_CMD_STATS;
    cmd->execute_func = stats;
    if (argc == 2) {
      cmd->stats_args.avg_window_days = WT_AVG_DEFAULT_WINDOW_LENGTH_DAYS;
      char const *home = getenv("HOME");
      int length = snprintf(cmd->stats_args.file_path, FILE_PATH_MAX_SIZE,
                            "%s/%s", home, WEIGHT_HISTORY_DEFAULT_FILE);
      if (length == FILE_PATH_MAX_SIZE) {
        res = -1;
        goto exit;
      }
    } else {
      assert(0 && "not implemented");
    }
    res = 0;
  } else {
    res = -1;
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
