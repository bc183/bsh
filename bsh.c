#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  char **args;       // Command arguments
  char *input_file;  // < filename (or NULL)
  char *output_file; // > filename (or NULL)
  int append;        // 1 for >>, 0 for >
  int background;    // 1 for &
} command_t;

void free_command(command_t *cmd) {
  if (cmd == NULL)
    return;

  if (cmd->args) {
    for (int i = 0; cmd->args[i] != NULL; i++) {
      free(cmd->args[i]);
    }
    free(cmd->args);
  }

  free(cmd->input_file);
  free(cmd->output_file);
  free(cmd);
}

char **tokenizer(char *buf) {
  // Remove trailing newline if present
  int len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    buf[len - 1] = '\0';
  }

  char **tokens = malloc(64 * sizeof(char *)); // Max 64 tokens
  if (tokens == NULL) {
    perror("malloc");
    return NULL;
  }

  int i = 0;
  char *token = strtok(buf, " \t");
  while (token != NULL) {
    if (i >= 63) { // Leave room for NULL terminator
      fprintf(stderr, "bsh: too many tokens (max 63)\n");
      free(tokens);
      return NULL;
    }
    tokens[i++] = token;
    token = strtok(NULL, " \t");
  }
  tokens[i] = NULL;

  return tokens;
}

command_t *parse_user_input(char *buf) {
  command_t *cmd = malloc(sizeof(command_t));
  if (cmd == NULL) {
    perror("malloc");
    return NULL;
  }

  cmd->input_file = NULL;
  cmd->output_file = NULL;
  cmd->append = 0;
  cmd->background = 0;
  cmd->args = NULL;

  char **tokens = tokenizer(buf);
  if (tokens == NULL) {
    free(cmd);
    return NULL;
  }

  char **args = malloc(64 * sizeof(char *));
  if (args == NULL) {
    perror("malloc");
    free(tokens);
    free(cmd);
    return NULL;
  }

  int arg_idx = 0;
  int token_len = 0;
  for (int i = 0; tokens[i] != NULL; i++) {
    token_len++;
  }

  for (int i = 0; tokens[i] != NULL; i++) {
    if (strcmp(tokens[i], "<") == 0) { // read from input_file
      if (i == token_len - 1) {
        fprintf(stderr, "bsh: syntax error: < cannot be the last token\n");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->input_file = strdup(tokens[++i]);
      if (cmd->input_file == NULL) {
        perror("strdup");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
    } else if (strcmp(tokens[i], ">") == 0) { // write to output_file
      if (i == token_len - 1) {
        fprintf(stderr, "bsh: syntax error: > cannot be the last token\n");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->output_file = strdup(tokens[++i]);
      if (cmd->output_file == NULL) {
        perror("strdup");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->append = 0;
    } else if (strcmp(tokens[i], ">>") == 0) { // append to output_file
      if (i == token_len - 1) {
        fprintf(stderr, "bsh: syntax error: >> cannot be the last token\n");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->output_file = strdup(tokens[++i]);
      if (cmd->output_file == NULL) {
        perror("strdup");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->append = 1;
    } else if (strcmp(tokens[i], "&") == 0) { // execute in bg
      if (i != token_len - 1) {
        fprintf(stderr, "bsh: syntax error: & must be the last token\n");
        free(tokens);
        free_command(cmd);
        return NULL;
      }
      cmd->background = 1;
    } else {
      args[arg_idx] = strdup(tokens[i]);
      if (args[arg_idx] == NULL) {
        perror("strdup");
        // Free previously allocated args
        for (int j = 0; j < arg_idx; j++) {
          free(args[j]);
        }
        free(args);
        free(tokens);
        free(cmd->input_file);
        free(cmd->output_file);
        free(cmd);
        return NULL;
      }
      arg_idx++;
    }
  }
  args[arg_idx] = NULL;
  cmd->args = args;

  free(tokens);
  return cmd;
}

char *get_prompt() {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    return strdup("bsh> ");
  }

  char *home = getenv("HOME");
  char *prompt;

  if (home != NULL && strncmp(cwd, home, strlen(home)) == 0) {
    if (cwd[strlen(home)] == '\0') {
      prompt = strdup("~> ");
    } else {
      // Allocate: "~" + path_after_home + "> " + null
      int len = 1 + strlen(cwd + strlen(home)) + 2 + 1;
      prompt = malloc(len);
      snprintf(prompt, len, "~%s> ", cwd + strlen(home));
    }
  } else {
    int len = strlen(cwd) + 2 + 1;
    prompt = malloc(len);
    snprintf(prompt, len, "%s> ", cwd);
  }

  return prompt;
}

int run_command(command_t *cmd) {
  if (cmd == NULL) {
    return EXIT_FAILURE;
  }
  char *command = cmd->args[0];
  if (command == NULL) {
    return EXIT_FAILURE;
  }
  if (strcmp("exit", command) == 0) {
    exit(EXIT_SUCCESS);
    return EXIT_SUCCESS;
  }
  if (strcmp("cd", command) == 0) {
    if (cmd->args[1] == NULL) {
      char *home = getenv("HOME");
      if (home == NULL) {
        fprintf(stderr, "bsh: cd: HOME not set\n");
        return EXIT_FAILURE;
      }
      if (chdir(home) != 0) {
        perror("cd");
      }
    } else if (chdir(cmd->args[1]) != 0) {
      perror("cd");
    }
    return EXIT_SUCCESS;
  }
  pid_t pid = fork();
  if (pid == -1) {
    perror("fork failed");
    return EXIT_FAILURE;
  } else if (pid == 0) {
    // Child process
    // Handle input redirection
    if (cmd->input_file) {
      int fd = open(cmd->input_file, O_RDONLY);
      if (fd < 0) {
        perror("cannot open input_file");
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDIN_FILENO) == -1) {
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
      }
      close(fd);
    }

    // Handle output redirection
    if (cmd->output_file) {
      int flags = O_WRONLY | O_CREAT;
      flags |= cmd->append ? O_APPEND : O_TRUNC;

      int fd = open(cmd->output_file, flags, 0644);
      if (fd < 0) {
        perror("open output");
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
      }
      close(fd);
    }

    execvp(command, cmd->args);

    // Only reaches here if execvp fails
    perror("command run failed");
    exit(EXIT_FAILURE);
  } else {
    // Parent process
    if (!cmd->background) {
      // Foreground: wait for the process to complete
      int status;
      waitpid(pid, &status, 0);
    }
    // Background: don't wait, process runs in background
  }
  return EXIT_SUCCESS;
}

void sigchld_handler(int sig) {
  (void)sig; // Unused parameter
  // Reap all terminated child processes
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

void start_shell_loop() {
  while (1) {
    char *prompt = get_prompt();
    printf("%s", prompt);
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
      // EOF (Ctrl+D) - exit shell
      free(prompt);
      printf("\n");
      break;
    }
    command_t *cmd = parse_user_input(buf);
    if (cmd == NULL || cmd->args == NULL || cmd->args[0] == NULL) {
      // Empty command or parse error
      free_command(cmd);
      free(prompt);
      continue;
    }
    run_command(cmd);
    free(prompt);
    free_command(cmd);
  }
}

int main() {
  // Set up SIGCHLD handler to reap background processes
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    return EXIT_FAILURE;
  }

  start_shell_loop();
  return 0;
}
