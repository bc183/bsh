#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char **parse_user_input(char *buf) {
  // Remove trailing newline if present
  int len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    buf[len - 1] = '\0';
  }

  char **tokens = malloc(64 * sizeof(char *)); // Max 64 tokens
  int i = 0;

  char *token = strtok(buf, " \t");
  while (token != NULL) {
    tokens[i++] = token;
    token = strtok(NULL, " \t");
  }
  tokens[i] = NULL;

  return tokens;
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

void start_shell_loop() {
  while (1) {
    char *prompt = get_prompt();
    printf("%s", prompt);
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
      free(prompt);
      continue;
    }
    char **parsed_input = parse_user_input(buf);
    char *command = parsed_input[0];
    if (command == NULL) {
      free(prompt);
      free(parsed_input);
      continue;
    }
    if (strcmp("exit", command) == 0) {
      free(prompt);
      free(parsed_input);
      return;
    }
    if (strcmp("cd", parsed_input[0]) == 0) {
      if (parsed_input[1] == NULL) {
        chdir(getenv("HOME"));
      } else if (chdir(parsed_input[1]) != 0) {
        perror("cd");
      }
      free(prompt);
      free(parsed_input);
      continue;
    }
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork failed");
      free(prompt);
      free(parsed_input);
      exit(EXIT_FAILURE);
    } else if (pid == 0) {
      execvp(parsed_input[0], parsed_input);

      // only return if there is an error
      perror("command run failed");
      free(prompt);
      free(parsed_input);
      exit(EXIT_FAILURE);
    } else {
      // wait for the running process
      waitpid(pid, NULL, 0);
    }
    free(prompt);
    free(parsed_input);
  }
}

int main() {
  start_shell_loop();
  return 0;
}
