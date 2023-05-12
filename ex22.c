
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>

#define ERROR                -1
#define SUCCESS              0
#define MAX_PATH             150
#define ARGUMENT_COUNT       2
#define CACHE_SIZE           1024
#define BUF_SIZE             1024
#define EXEC_TIMEOUT_SECONDS 5

#define STUDENT_EXEC_NAME   "a.out"
#define STUDENT_OUTPUT_NAME "output.txt"

typedef struct {
    int fd;
    char cache[CACHE_SIZE];
    unsigned int pos;
    unsigned int cache_len;
} OpenFile;

typedef struct {
    char parent_directory[MAX_PATH];
    char input_file[MAX_PATH];
    char output_file[MAX_PATH];
} Config;

typedef enum {
    NO_C_FILE = 0,
    COMPILATION_ERROR = 10,
    TIMEOUT = 20,
    WRONG = 50,
    SIMILAR = 75,
    EXCELLENT = 100
} Grade;

typedef enum {
    COMPARE_IDENTICAL = 1,
    COMPARE_DIFFERENT,
    COMPARE_SIMILAR
} CompareStatus;

typedef enum {
    STDIN = 0,
    STDOUT,
    STDERR
} std_no;

typedef enum {
    FALSE = 0,
    TRUE
} bool;

void print_error(char *message);
const char *get_reason(Grade grade);
int read_config(Config *config, char *config_path);
int open_file(OpenFile *file, char *file_path);
bool read_from_file(OpenFile *file, char *ch);
int start_testing(Config *config);
Grade test_student(Config *config, char *student_name);
bool find_file(char *dir_path, char *buffer);
void build_path(char *buffer, char *base_path, char *inner_path);
int compile_c_file(char *dir_path, char *c_file_name, char *error_file_path);
int run_exec_file(char *dir_path, char *exec_file_name, char *input_file_path, char *output_file_path, char *error_file_path);
Grade run_compare(char *error_file_path, char *exec_file_path, char *output_file_path, char *student_output_file_path);
void write_student_grade(int fd, char *student_name, Grade grade);
void kill_by_signal();

int main(int argc, char *argv[]) {
    if (argc != ARGUMENT_COUNT) {
        return ERROR;
    }

    Config config;
    char *config_path = argv[1];
    if (read_config(&config, config_path) == ERROR) {
        return ERROR;
    }

    if (start_testing(&config) == ERROR) {
        return ERROR;
    }

    return SUCCESS;
}

Grade test_student(Config *config, char *student_name) {
    char current_directory[MAX_PATH];
    build_path(current_directory, config->parent_directory, student_name);

    // find the C file in the current directory
    char c_file[MAX_PATH];
    if (find_file(current_directory, c_file) == FALSE) {
        return NO_C_FILE;
    }

    if (compile_c_file(current_directory, c_file, "errors.txt") != SUCCESS) {
        return COMPILATION_ERROR;
    }

    // set up paths for student output and executable files
    char student_output_file_path[MAX_PATH];
    build_path(student_output_file_path, current_directory, STUDENT_OUTPUT_NAME);
    char exec_file_path[MAX_PATH];
    build_path(exec_file_path, current_directory, STUDENT_EXEC_NAME);

    int exec_status = run_exec_file(current_directory, exec_file_path, config->input_file, student_output_file_path, "errors.txt");
    if (remove(exec_file_path) == ERROR) {
        print_error("Error in: remove()\n");
        return ERROR;
    }

    if (exec_status != SUCCESS) {
        return TIMEOUT;
    }

    // compare the student's output with the expected output
    Grade compare_result = run_compare("errors.txt", "./comp.out", config->output_file, student_output_file_path);
    if (remove(student_output_file_path) == ERROR) {
        print_error("Error in: remove()\n");
        return ERROR;
    }
    return compare_result;
}

// function to write the student's grade to a file
void write_student_grade(int fd, char *student_name, Grade grade) {
    char buffer[BUF_SIZE];
    snprintf(buffer, BUF_SIZE, "%s,%d,%s\n", student_name, grade, get_reason(grade));
    write(fd, buffer, strlen(buffer));
}

// function to compare the student's output with the expected output and return a grade
Grade run_compare(char *error_file_path, char *exec_file_path, char *output_file_path, char *student_output_file_path) {
    pid_t pid = fork();
    if (pid == ERROR) {
        print_error("Error in: fork()\n");
        return ERROR;
    }

    if (pid == 0) {
        // child process
        int fd = open(error_file_path, O_WRONLY | O_CREAT, 0666);
        if (fd == ERROR) {
            print_error("Error in: open()\n");
            exit(ERROR);
        }
        
        // move the file pointer to the end of the error file
        if (lseek(fd, 0, SEEK_END) == ERROR) {
            print_error("Error in: lseek()\n");
            exit(ERROR);
        }

        // redirect stderr to the error file
        if (dup2(fd, STDERR) == ERROR) {
            print_error("Error in: dup2()\n");
            exit(ERROR);
        }
        
        // close the file descriptor
        if (close(fd) == ERROR) {
            print_error("Error in: close()\n");
            exit(ERROR);
        }

        // set up arguments for the compare executable
        char *compile_argv[] = {
            exec_file_path,
            output_file_path,
            student_output_file_path,
            NULL
        };
        execvp(compile_argv[0], compile_argv);
        print_error("Error in: execvp()\n");
        exit(ERROR);
    } else {
        // parent process
        int status;
        // wait for the child process to complete and retrieve its status
        if (waitpid(pid, &status, 0) == ERROR) {
            print_error("Error in: waitpid()\n");
            return ERROR;
        }

        CompareStatus compare_return_value = ERROR;
        if (WIFEXITED(status)) {
            compare_return_value = WEXITSTATUS(status);
        }
        
        // determine the grade based on the comparison result
        switch (compare_return_value) {
            case COMPARE_IDENTICAL: return EXCELLENT;
            case COMPARE_SIMILAR:   return SIMILAR;
            case COMPARE_DIFFERENT: return WRONG;
            default:                return ERROR;
        }
    }
}

// function to kill the child process by raising a SIGUSR1 signal
void kill_by_signal() {
    raise(SIGUSR1);
}


// function to run the executable file produced by compiling the student's code
int run_exec_file(char *dir_path, char *exec_file_path, char *input_file_path, char *output_file_path, char *error_file_path) {
    // create a child process
    pid_t pid = fork();
    if (pid == ERROR) {
        print_error("Error in: fork()\n");
        return ERROR;
    }

    if (pid == 0) {
        // child process
        int fd_error = open(error_file_path, O_WRONLY | O_CREAT, 0666);
        int fd_input = open(input_file_path, O_RDONLY);
        int fd_output = open(output_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd_error == ERROR || fd_input == ERROR || fd_output == ERROR) {
            print_error("Error in: open()\n");
            exit(ERROR);
        }
        
        // move the file pointer to the end of the error file
        if (lseek(fd_error, 0, SEEK_END) == ERROR) {
            print_error("Error in: lseek()\n");
            exit(ERROR);
        }

        if (dup2(fd_error, STDERR) == ERROR || dup2(fd_input, STDIN) == ERROR || dup2(fd_output, STDOUT) == ERROR) {
            print_error("Error in: dup2()\n");
            exit(ERROR);
        }
        
        if (close(fd_error) == ERROR || close(fd_input) == ERROR || close(fd_output) == ERROR) {
            print_error("Error in: close()\n");
            exit(ERROR);
        }

        char *exec_argv[] = {
            exec_file_path,
            NULL
        };

        // set up a signal handler for the alarm and set an alarm for execution timeout
        signal(SIGALRM, kill_by_signal);
        alarm(EXEC_TIMEOUT_SECONDS);

        // execute the student's compiled code
        execvp(exec_argv[0], exec_argv);
        perror("error");
        print_error("Error in: execvp()\n");
        exit(ERROR);
    } else {
        // parent process
        int status;
        if (waitpid(pid, &status, 0) == ERROR) {
            print_error("Error in: waitpid()\n");
            return ERROR;
        }

        // return the success status or timeout if the child process did not exit normally
        return WIFEXITED(status) ? SUCCESS : TIMEOUT;
    }
}

// function to compile a C file and store any errors in an error file
int compile_c_file(char *dir_path, char *c_file_name, char *error_file_path) {
    pid_t pid = fork();
    if (pid == ERROR) {
        print_error("Error in: fork()\n");
        return ERROR;
    }

    if (pid == 0) {
        // Child process
        int fd = open(error_file_path, O_WRONLY | O_CREAT, 0666);
        if (fd == ERROR) {
            print_error("Error in: open()\n");
            exit(ERROR);
        }
        if (lseek(fd, 0, SEEK_END) == ERROR) {
            print_error("Error in: lseek()\n");
            exit(ERROR);
        }

        if (dup2(fd, STDERR) == ERROR) {
            print_error("Error in: dup2()\n");
            exit(ERROR);
        }
        
        if (close(fd) == ERROR) {
            print_error("Error in: close()\n");
            exit(ERROR);
        }
        
        // Build paths for the C file and the output executable file
        char c_file_path[MAX_PATH] = { 0 };
        build_path(c_file_path, dir_path, c_file_name);
        char out_file_path[MAX_PATH] = { 0 };
        build_path(out_file_path, dir_path, STUDENT_EXEC_NAME);
        
        // Set up arguments for the gcc compiler
        char *compile_argv[] = {
            "gcc",
            c_file_path,
            "-o",
            out_file_path,
            NULL
        };
        
        // Execute the gcc compiler with the provided arguments
        execvp(compile_argv[0], compile_argv);
        print_error("Error in: execvp()\n");
        exit(ERROR);
    } else {
        // Parent process
        int status;
        if (waitpid(pid, &status, 0) == ERROR) {
            print_error("Error in: waitpid()\n");
            return ERROR;
        }

        int gcc_return_value = ERROR;
        if (WIFEXITED(status)) {
            gcc_return_value = WEXITSTATUS(status);
        }
        return gcc_return_value;
    }
}

// Function to build a path by concatenating the base path and inner path
void build_path(char *buffer, char *base_path, char *inner_path) {
    bool addSlash = base_path[strlen(base_path) - 1] != '/';
    snprintf(buffer, MAX_PATH, "%s%s%s", base_path, addSlash ? "/" : "", inner_path);
}

// Function to find the first C file in the specified directory
bool find_file(char *dir_path, char *buffer) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return ERROR;
    }
    
    struct dirent *entry = NULL;
    // Iterate through the directory entries
    while ((entry = readdir(dir))) {
        // Check if the entry is a regular C file
        char *last_occ = strrchr(entry->d_name, '.');
        bool is_c_file = entry->d_type == DT_REG && last_occ != NULL && strcmp(last_occ, ".c") == 0;
        if (is_c_file) {
            // Copy the file name to the buffer and close the directory
            strncpy(buffer, entry->d_name, MAX_PATH);
            closedir(dir);
            return TRUE;
        }
    }
    
    // Close the directory and return FALSE if no C file is found
    closedir(dir);
    return FALSE;
}

// Function to start testing the students' code based on the given configuration
int start_testing(Config *config) {
    // Open the parent directory containing student directories
    DIR *dir = opendir(config->parent_directory);
    if (dir == NULL) {
        return ERROR;
    }

    // Create and open a CSV file to store the results
    int fd_csv = open("results.csv", O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd_csv == ERROR) {
        closedir(dir);
        return ERROR;
    }

    struct dirent *entry = NULL;
    // Iterate through the directory entries
    while ((entry = readdir(dir))) {
        // Check if the entry is a valid student directory
        bool is_directory = entry->d_type == DT_DIR;
        bool is_legal_directory = strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0;
        if (is_directory && is_legal_directory) {
            // Test the student's code and store the result in the CSV file
            char *dir_name = entry->d_name;
            Grade grade = test_student(config, dir_name);
            if (grade != ERROR) {
                write_student_grade(fd_csv, dir_name, grade);
            }
        }
    }
    
    closedir(dir);
    close(fd_csv);
}

// Function to open a file and store the file descriptor and related information in the OpenFile structure
int open_file(OpenFile *file, char *file_path) {
    int fd = open(file_path, O_RDONLY);
    if (fd == ERROR) {
        return ERROR;
    }

    // If the OpenFile pointer is not NULL, store the file descriptor and set position and cache length
    if (file != NULL) {
        file->fd = fd;
        file->pos = 0;
        file->cache_len = 0;
    } else {
        // If the OpenFile pointer is NULL, close the file
        close(fd);
    }

    return SUCCESS;
}

// Function to print an error message to the standard error output
void print_error(char *message) {
    write(STDERR, message, strlen(message));
}

// returns FALSE if reached end-of-file
bool read_from_file(OpenFile *file, char *ch) {
    // did we reach all of the characters in our cache?
    if (file->pos >= file->cache_len) {
        // read up to CACHE_SIZE characters into cache
        ssize_t byte_count = read(file->fd, file->cache, CACHE_SIZE);
        if (byte_count == 0) {
            // reached end-of-file
            return FALSE;
        }

        // reset cache pointer and update len
        file->pos = 0;
        file->cache_len = byte_count;
    }

    // read a character into 'ch' from our cache
    *ch = file->cache[file->pos];
    // advance to the next character in the cache
    file->pos++;

    return TRUE;
}

// Function to read a single line from an open file and store it in the provided buffer
unsigned int read_line(OpenFile *file, char *buffer, unsigned int buf_size) {
    char ch;
    unsigned pos = 0;
    // Iterate through the file characters until reaching the end of the line or the buffer limit
    while (pos < buf_size - 1 && read_from_file(file, &ch)) {
        if (ch == '\n') {
            // If a newline character is encountered, break the loop
            break;
        }
        // Store the character in the buffer and increment the position
        buffer[pos] = ch;
        pos++;
    }
    // Add a null terminator at the end of the buffer
    buffer[pos] = '\0';
    // Return the number of characters read
    return pos;
}

// Function to read the configuration file and store the data in the Config structure
int read_config(Config *config, char *config_path) {
    OpenFile file;
    if (open_file(&file, config_path) == ERROR) {
        return ERROR;
    }

    char buffer[BUF_SIZE];
    // Read the parent directory path, input file path, and output file path from the configuration file
    if (read_line(&file, buffer, BUF_SIZE) > 0) {
        strncpy(config->parent_directory, buffer, MAX_PATH);
    }
    if (read_line(&file, buffer, BUF_SIZE) > 0) {
        strncpy(config->input_file, buffer, MAX_PATH);
    }
    if (read_line(&file, buffer, BUF_SIZE) > 0) {
        strncpy(config->output_file, buffer, MAX_PATH);
    }

    // Check if the parent directory is valid
    DIR *dir = opendir(config->parent_directory);
    if (dir == NULL) {
        print_error("Not a valid directory\n");
        close(file.fd);
        return ERROR;
    }
    closedir(dir);

    // Check if the input file exists
    if (open_file(NULL, config->input_file) == ERROR) {
        print_error("Input file not exist\n");
        close(file.fd);
        return ERROR;
    }

    // Check if the output file exists
    if (open_file(NULL, config->output_file) == ERROR) {
        print_error("Output file not exist\n");
        close(file.fd);
        return ERROR;
    }

    close(file.fd);
    return SUCCESS;
}

// Function to get the string representation of a grade
const char *get_reason(Grade grade) {
    switch (grade) {
        case NO_C_FILE:         return "NO_C_FILE";
        case COMPILATION_ERROR: return "COMPILATION_ERROR";
        case TIMEOUT:           return "TIMEOUT";
        case WRONG:             return "WRONG";
        case SIMILAR:           return "SIMILAR";
        case EXCELLENT:         return "EXCELLENT";
    }

    return "";
}