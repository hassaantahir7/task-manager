/*
 * task_manager_bc230417440.c
 *
 * Mini task-processing simulation using fork(), two pipes and a text report.
 *
 *   Parent --(pipe1: 3 task times)--> Child
 *   Child  --(pipe2: total, longest task no., max time)--> Parent
 *
 * Compile: gcc task_manager_bc230417440.c -o task_manager
 * Run:     ./task_manager
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_TASKS 3
#define ID_MAX    64

/* Read exactly n bytes (handles partial reads). Returns 0 on success, -1 on error/EOF. */
static int read_full(int fd, void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, (char *)buf + done, n - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;              /* unexpected EOF */
        done += (size_t)r;
    }
    return 0;
}

/* Write exactly n bytes (handles partial writes). Returns 0 on success, -1 on error. */
static int write_full(int fd, const void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, (const char *)buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)w;
    }
    return 0;
}

/* Student ID is used in a filename, so allow only letters, digits, '_' and '-'. */
static int valid_id(const char *s)
{
    if (*s == '\0') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            return 0;
    return 1;
}

/* Prompt until the user enters a positive integer. */
static int read_positive_int(const char *prompt)
{
    char line[64];
    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            fprintf(stderr, "\nInput ended unexpectedly.\n");
            exit(EXIT_FAILURE);
        }
        char *end;
        errno = 0;
        long v = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
        if (end != line && *end == '\0' && errno == 0 && v > 0 && v <= 1000000000L)
            return (int)v;
        printf("Invalid input. Please enter a positive integer.\n");
    }
}

int main(void)
{
    char student_id[ID_MAX];
    int  times[NUM_TASKS];
    int  pipe1[2];   /* parent -> child */
    int  pipe2[2];   /* child  -> parent */

    /* ---------- Parent: take input ---------- */
    for (;;) {
        printf("Enter Student ID: ");
        fflush(stdout);
        if (!fgets(student_id, sizeof student_id, stdin)) {
            fprintf(stderr, "\nInput ended unexpectedly.\n");
            return EXIT_FAILURE;
        }
        student_id[strcspn(student_id, "\r\n")] = '\0';
        if (valid_id(student_id)) break;
        printf("Invalid ID. Use only letters, digits, '_' or '-'.\n");
    }

    for (int i = 0; i < NUM_TASKS; i++) {
        char prompt[48];
        snprintf(prompt, sizeof prompt, "Enter Task %d Execution Time: ", i + 1);
        times[i] = read_positive_int(prompt);
    }

    /* ---------- Create pipes ---------- */
    if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* ================= CHILD ================= */
        close(pipe1[1]);    /* child only reads from pipe1 */
        close(pipe2[0]);    /* child only writes to pipe2  */

        int t[NUM_TASKS];
        if (read_full(pipe1[0], t, sizeof t) == -1) {
            perror("child read");
            _exit(EXIT_FAILURE);
        }
        close(pipe1[0]);

        int total = 0;
        int longest_task = 1;
        int max_time = t[0];

        for (int i = 0; i < NUM_TASKS; i++) {
            total += t[i];
            /* strict '>' keeps the smaller task number when times are tied */
            if (t[i] > max_time) {
                max_time = t[i];
                longest_task = i + 1;
            }
        }

        int result[3] = { total, longest_task, max_time };
        if (write_full(pipe2[1], result, sizeof result) == -1) {
            perror("child write");
            _exit(EXIT_FAILURE);
        }
        close(pipe2[1]);
        _exit(EXIT_SUCCESS);
    }

    /* ================= PARENT ================= */
    close(pipe1[0]);        /* parent only writes to pipe1 */
    close(pipe2[1]);        /* parent only reads from pipe2 */

    if (write_full(pipe1[1], times, sizeof times) == -1) {
        perror("parent write");
        return EXIT_FAILURE;
    }
    close(pipe1[1]);

    int result[3];
    if (read_full(pipe2[0], result, sizeof result) == -1) {
        perror("parent read");
        return EXIT_FAILURE;
    }
    close(pipe2[0]);

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Child process failed.\n");
        return EXIT_FAILURE;
    }

    int total        = result[0];
    int longest_task = result[1];
    int max_time     = result[2];

    /* ---------- Build the report text ---------- */
    char report[512];
    int len = snprintf(report, sizeof report,
        "--- Task Processing Report ---\n"
        "Student ID: %s\n"
        "Task 1: %d units\n"
        "Task 2: %d units\n"
        "Task 3: %d units\n"
        "Total Execution Time: %d units\n"
        "Longest Task: Task %d\n"
        "Maximum Execution Time: %d units\n",
        student_id, times[0], times[1], times[2],
        total, longest_task, max_time);

    /* ---------- Display on screen ---------- */
    printf("\n%s", report);

    /* ---------- Save to StudentID_report.txt using Linux file I/O ---------- */
    char filename[ID_MAX + 16];
    snprintf(filename, sizeof filename, "%s_report.txt", student_id);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }
    if (write_full(fd, report, (size_t)len) == -1) {
        perror("write report");
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);

    printf("Report saved to: %s\n", filename);
    return EXIT_SUCCESS;
}
