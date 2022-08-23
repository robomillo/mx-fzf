#include <_ctype.h>
#include <alloca.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <pwd.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TMX_DIR "/.config/tmuxinator"

sqlite3 *db;

typedef struct file {
  char *file_name;
} file;

struct tmuxFiles {
  int is_favourite;
  int len;
  FILE *fzf_fp;
  file *files;
};

struct tmuxFiles T;

char *strip_yml(char *file_name, size_t len) {
  char *stripped = malloc(len);
  char test = '.';
  for (size_t i = 0; i < len; i++) {
    if (test == file_name[i]) {
      stripped[i] = '\0';
      break;
    } else {
      stripped[i] = file_name[i];
    }
  }
  return stripped;
}

static int get_callback(void *not_used, int argc, char **argv,
                        char **col_name) {
  if (T.is_favourite == 0) {
    if (strcmp(argv[1], "1") == 0) {
      fprintf(T.fzf_fp, "\x1b[33;49;4m* %s *\x1b[0m\n", argv[0]);
    } else {
      fprintf(T.fzf_fp, "%s\n", argv[0]);
    }
  } else {
    if (strcmp(argv[1], "1") == 0) {
      printf("\x1b[33;49;4m* %s *\x1b[0m\n", argv[0]);
    } else {
      printf("%s\n", argv[0]);
    }
  }
  return 0;
}

void close_db() { sqlite3_close(db); }

void handle_error(int return_code, char *error_message) {
  if (return_code != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", error_message);
    sqlite3_free(error_message);
  }
}

void ddl() {
  char *sql;
  char *error_message = 0;
  int return_code;
  sql = "CREATE TABLE IF NOT EXISTS projects ("
        "access_count number  default 0,"
        "is_favourite integer default 0,"
        "name         text not null "
        "primary key "
        "unique"
        ");";
  return_code = sqlite3_exec(db, sql, NULL, 0, &error_message);
  handle_error(return_code, error_message);
}

void get_sorted() {
  char *sql;
  int return_code = 0;
  char *error_message = 0;
  sql = "SELECT name, is_favourite "
        "FROM projects "
        "ORDER BY is_favourite DESC, access_count DESC, name;";
  return_code = sqlite3_exec(db, sql, get_callback, 0, &error_message);
  handle_error(return_code, error_message);
}

void toggle_favourite(char *favourite) {
  char sql[1024];
  int return_code = 0;
  char *error_message = 0;
  return_code = sqlite3_exec(db, "BEGIN;", NULL, 0, &error_message);
  handle_error(return_code, error_message);
  snprintf(sql, sizeof(sql),
           "UPDATE projects SET is_favourite = CASE WHEN is_favourite = "
           "1 THEN 0 "
           "ELSE 1 END WHERE name = '%s';",
           favourite);
  return_code = sqlite3_exec(db, sql, NULL, 0, &error_message);
  handle_error(return_code, error_message);
  return_code = sqlite3_exec(db, "COMMIT;", NULL, 0, &error_message);
  handle_error(return_code, error_message);
  get_sorted();
}

void insert_files() {
  char sql[1024];
  int return_code = 0;
  char *error_message = 0;
  return_code = sqlite3_exec(db, "BEGIN;", NULL, 0, &error_message);
  handle_error(return_code, error_message);
  for (int i = 0; i < T.len; i++) {
    snprintf(sql, sizeof(sql),
             "INSERT OR IGNORE INTO projects(name) VALUES ('%s');",
             T.files[i].file_name);
    return_code = sqlite3_exec(db, sql, NULL, 0, &error_message);
    handle_error(return_code, error_message);
  }
  return_code = sqlite3_exec(db, "COMMIT;", NULL, 0, &error_message);
  handle_error(return_code, error_message);
}

void open_db() {
  int open_result;
  open_result = sqlite3_open("/tmp/tmux_db.db", &db);
  if (open_result) {
    fprintf(stderr, "Can't open database %s\n", sqlite3_errmsg(db));
    exit(1);
  }
}

void append_project(char *stripped) {
  T.files = realloc(T.files, sizeof(file) * (T.len + 1));
  T.files[T.len].file_name = malloc(strlen(stripped) + 1);
  memcpy(T.files[T.len].file_name, stripped, strlen(stripped) + 1);
  T.len++;
}

static int ptree(char *file_path[2]) {
  FTS *ftsp;
  FTSENT *p, *chp;
  int fts_options = FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR;
  if ((ftsp = fts_open(file_path, fts_options, NULL)) == NULL) {
    warn("fts_open");
    return -1;
  }
  chp = fts_children(ftsp, 0);
  if (chp == NULL) {
    return 0;
  }
  while ((p = fts_read(ftsp)) != NULL) {
    if (p->fts_info == FTS_F) {
      size_t len = strlen(p->fts_name);
      char *stripped = strip_yml(p->fts_name, len);
      append_project(stripped);
    }
  }
  fts_close(ftsp);
  return 0;
}

char *get_tmux_path() {
  const char *homedir;
  if ((homedir = getenv("HOME")) == NULL) {
    homedir = getpwuid(getuid())->pw_dir;
  }
  int new_size = strlen(homedir) + strlen(TMX_DIR);
  char *full_path = (char *)malloc(new_size);
  strcpy(full_path, homedir);
  strcat(full_path, TMX_DIR);
  return full_path;
}

void walk_tmux_dir() {
  int rc;
  char *path = get_tmux_path();
  char *fts_open_array[] = {path, NULL};
  if ((rc = ptree(fts_open_array)) != 0) {
    rc = 1;
  }
}

char *remove_stars(char *str) {
  char *end;
  while (!isalnum((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && !isalnum((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

char *trimwhitespace(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

void open_fzf() {
  char *command = "fzf-tmux -p 50% --multi --reverse --ansi"
                  " --bind "
                  "'ctrl-f:reload(mx-fzf favourite {})"
                  "' | sed -E 's/\\* | \\*//g' | tr -d "
                  "'\r'  | xargs -L 1 "
                  "tmuxinator start ";
  FILE *fp;
  fp = popen(command, "w");
  T.fzf_fp = fp;
  get_sorted();
  pclose(fp);
}

int main(int argc, char *argv[]) {
  open_db();
  if (argc > 1) {
    if (strcmp(argv[1], "favourite") == 0) {
      T.is_favourite = 1;
      char *thing = trimwhitespace(remove_stars(argv[2]));
      toggle_favourite(thing);
    }
  } else {
    T.is_favourite = 0;
    T.len = 0;
    T.files = NULL;
    T.fzf_fp = NULL;
    ddl();
    walk_tmux_dir();
    insert_files();
    open_fzf();
    free(T.files);
    pclose(T.fzf_fp);
  }
  close_db();
  return 0;
}
