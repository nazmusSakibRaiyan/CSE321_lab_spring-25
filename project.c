#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_TOKENS 100
#define HISTORY_SIZE 100

char *history[HISTORY_SIZE];
int history_count = 0;


void handle_sigint(int sig) {
	char cwd[1024];
	getcwd(cwd, sizeof(cwd));
	printf("\nCtrl + C triggered");
	printf("\n%s> ", cwd);
	fflush(stdout);
}


void add_to_history(const char *command) {
	if (history_count < HISTORY_SIZE) {
    	history[history_count++] = strdup(command);
	}
}


void show_history() {
	for (int i = 0; i < history_count; i++) {
    	printf("%d %s\n", i + 1, history[i]);
	}
}

char *remove_whitespace(char *str) {
	while (*str == ' ') {
		str++;
	
	}

	if (*str == 0) {
		return str;
	}

	char *end = str + strlen(str) - 1;

	while (end > str && *end == ' ') {
		end--;
	}

	*(end + 1) = '\0';

	return str;
}

char **split_by_character(char *line, const char *character, int *count) {
	char **tokens = malloc(sizeof(char *) * MAX_TOKENS);
	char *token = strtok(line, character);
	int i = 0;
	while (token != NULL) {
    	tokens[i++] = strdup(remove_whitespace(token));
    	token = strtok(NULL, character);
	}
	tokens[i] = NULL;
	*count = i;
	return tokens;
}


void execute_single(char *cmd) {
	int in_fd = -1, out_fd = -1, append = 0;
	char *input_file = NULL, *output_file = NULL;
    
	char *in_redirect = strchr(cmd, '<');
	char *out_redirect = strstr(cmd, ">>") ? strstr(cmd, ">>") : strchr(cmd, '>');
	if (in_redirect) {
    	*in_redirect = '\0';
    	input_file = remove_whitespace(in_redirect + 1);
	}
	if (out_redirect) {
    	append = (*out_redirect == '>' && *(out_redirect + 1) == '>') ? 1 : 0;
    	*out_redirect = '\0';
    	output_file = remove_whitespace(out_redirect + (append ? 2 : 1));
	}

	char *args[MAX_TOKENS];
	int i = 0;
	char *token = strtok(cmd, " ");
	while (token) {
    	args[i++] = token;
    	token = strtok(NULL, " ");
	}
	args[i] = NULL;

	if (args[0] == NULL) return;


	if (strcmp(args[0], "cd") == 0) {
		if (args[1] == NULL) {
			fprintf(stderr, "cd: expected argument\n");
		} else {
			if (chdir(args[1]) != 0) {
				perror("cd");
			}
		}
		return;
	}
	
	if (strcmp(args[0], "history") == 0) {
		show_history();
		return;
	}
	

	pid_t pid = fork();
	if (pid == 0) {
    	if (input_file) {
        	in_fd = open(input_file, O_RDONLY);
        	if (in_fd < 0) perror("open");
        	dup2(in_fd, 0);
    	}
    	if (output_file) {
        	out_fd = open(output_file, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
        	if (out_fd < 0) perror("open");
        	dup2(out_fd, 1);
    	}
    	execvp(args[0], args);
    	perror("execvp");
    	exit(EXIT_FAILURE);
	} else {
    	wait(NULL);
	}
}

void handle_pipes(char *cmd) {
	int count = 0;
	char **commands = split_by_character(cmd, "|", &count);
	int pipefd[2], in_fd = 0;

	for (int i = 0; i < count; i++) {
    	pipe(pipefd);
    	pid_t pid = fork();
    	if (pid == 0) {
        	dup2(in_fd, 0);

        	if (i < count - 1) {
				dup2(pipefd[1], 1);
			}
			
        	close(pipefd[0]);
        	execute_single(commands[i]);
        	exit(0);
    	} else {
        	wait(NULL);
        	close(pipefd[1]);
        	in_fd = pipefd[0];
    	}
	}
}

// Commands with ; and &&
void execute_command(char *cmd) {
	int count = 0;
	char **semi_cmds = split_by_character(cmd, ";", &count);
	for (int i = 0; i < count; i++) {
    	char *and_cmd = strstr(semi_cmds[i], "&&");
    	if (and_cmd) {
        	*and_cmd = '\0';
        	char *first = remove_whitespace(semi_cmds[i]);
        	char *second = remove_whitespace(and_cmd + 2);

        	pid_t pid = fork();
        	int status;
        	if (pid == 0) {
            	execute_command(first);
            	exit(0);
        	} else {
            	wait(&status);
            	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                	execute_command(second);
            	}
        	}
    	} else if (strchr(semi_cmds[i], '|')) {
        	handle_pipes(semi_cmds[i]);
    	} else {
        	execute_single(semi_cmds[i]);
    	}
	}
}

int main() {
	signal(SIGINT, handle_sigint);
	char input[MAX_COMMAND_LENGTH];

	while (1) {
		char cwd[1024];
		getcwd(cwd, sizeof(cwd));
		printf("%s> ", cwd);

    	fflush(stdout);

    	if (!fgets(input, sizeof(input), stdin)) {
			break;
		}

    	input[strcspn(input, "\n")] = 0; 
    	
		if (strlen(input) == 0) {	
			continue;
		}

    	if (strcmp(input, "exit") == 0){
			break;
		} 

    	add_to_history(input);
    	execute_command(input);
	}

	printf("Exiting shell.\n");
	return 0;
}



