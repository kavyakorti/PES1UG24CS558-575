#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>

#include "monitor_ioctl.h"

#define CONTROL_PATH "/tmp/mini_runtime.sock"
#define LOG_DIR "./logs"
#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 64
#define LOG_BUFFER_CAPACITY 256
#define LOG_CHUNK 512
#define DEFAULT_SOFT_MIB 40
#define DEFAULT_HARD_MIB 64

typedef enum {
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_LOGS,
    CMD_STOP
} command_type_t;

typedef enum {
    STATE_STARTING,
    STATE_RUNNING,
    STATE_EXITED,
    STATE_STOPPED,
    STATE_HARD_LIMIT_KILLED,
    STATE_FAILED
} container_state_t;

typedef struct {
    char container_id[CONTAINER_ID_LEN];
    char data[LOG_CHUNK];
    int length;
} log_item_t;

typedef struct {
    log_item_t items[LOG_BUFFER_CAPACITY];
    int head;
    int tail;
    int count;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} bounded_buffer_t;

typedef struct {
    char id[CONTAINER_ID_LEN];
    pid_t pid;
    char rootfs[PATH_MAX];
    char command[256];
    unsigned long soft_limit;
    unsigned long hard_limit;
    int nice_value;
    time_t start_time;
    container_state_t state;
    int exit_status;
    int stop_requested;
    int log_pipe_fd;
    pthread_t producer_thread;
} container_t;

typedef struct {
    command_type_t type;
    char id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[256];
    unsigned long soft_mib;
    unsigned long hard_mib;
    int nice_value;
} control_request_t;

typedef struct {
    int status;
    int exit_code;
    char message[4096];
} control_response_t;

typedef struct {
    char id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[256];
    int log_write_fd;
    int nice_value;
} child_config_t;

typedef struct {
    bounded_buffer_t log_buffer;
    container_t containers[MAX_CONTAINERS];
    int container_count;
    pthread_mutex_t containers_mutex;
    pthread_t logger_thread;
    int monitor_fd;
    int server_fd;
    int shutting_down;
} supervisor_ctx_t;

typedef struct {
    supervisor_ctx_t *ctx;
    container_t *container;
} producer_arg_t;

static supervisor_ctx_t g_ctx;

static const char *state_to_string(container_state_t s)
{
    switch (s) {
    case STATE_STARTING: return "starting";
    case STATE_RUNNING: return "running";
    case STATE_EXITED: return "exited";
    case STATE_STOPPED: return "stopped";
    case STATE_HARD_LIMIT_KILLED: return "hard_limit_killed";
    case STATE_FAILED: return "failed";
    default: return "unknown";
    }
}

static void bounded_buffer_init(bounded_buffer_t *b)
{
    memset(b, 0, sizeof(*b));
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->not_empty, NULL);
    pthread_cond_init(&b->not_full, NULL);
}

static void bounded_buffer_shutdown(bounded_buffer_t *b)
{
    pthread_mutex_lock(&b->mutex);
    b->shutting_down = 1;
    pthread_cond_broadcast(&b->not_empty);
    pthread_cond_broadcast(&b->not_full);
    pthread_mutex_unlock(&b->mutex);
}

static int bounded_buffer_push(bounded_buffer_t *b, const log_item_t *item)
{
    pthread_mutex_lock(&b->mutex);
    while (b->count == LOG_BUFFER_CAPACITY && !b->shutting_down)
        pthread_cond_wait(&b->not_full, &b->mutex);

    if (b->shutting_down) {
        pthread_mutex_unlock(&b->mutex);
        return -1;
    }

    b->items[b->tail] = *item;
    b->tail = (b->tail + 1) % LOG_BUFFER_CAPACITY;
    b->count++;

    pthread_cond_signal(&b->not_empty);
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

static int bounded_buffer_pop(bounded_buffer_t *b, log_item_t *item)
{
    pthread_mutex_lock(&b->mutex);
    while (b->count == 0 && !b->shutting_down)
        pthread_cond_wait(&b->not_empty, &b->mutex);

    if (b->count == 0 && b->shutting_down) {
        pthread_mutex_unlock(&b->mutex);
        return -1;
    }

    *item = b->items[b->head];
    b->head = (b->head + 1) % LOG_BUFFER_CAPACITY;
    b->count--;

    pthread_cond_signal(&b->not_full);
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

static int register_with_monitor(int fd, pid_t pid, const char *id,
                                 unsigned long soft_limit, unsigned long hard_limit)
{
    struct monitor_request req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.soft_limit_bytes = soft_limit;
    req.hard_limit_bytes = hard_limit;
    strncpy(req.container_id, id, sizeof(req.container_id) - 1);

    if (fd < 0)
        return -1;

    return ioctl(fd, MONITOR_REGISTER, &req);
}

static int unregister_with_monitor(int fd, pid_t pid, const char *id)
{
    struct monitor_request req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    strncpy(req.container_id, id, sizeof(req.container_id) - 1);

    if (fd < 0)
        return -1;

    return ioctl(fd, MONITOR_UNREGISTER, &req);
}

static void *logging_thread(void *arg)
{
    supervisor_ctx_t *ctx = (supervisor_ctx_t *)arg;
    log_item_t item;
    char path[PATH_MAX];
    FILE *fp;

    mkdir(LOG_DIR, 0755);

    while (bounded_buffer_pop(&ctx->log_buffer, &item) == 0) {
        snprintf(path, sizeof(path), "%s/%s.log", LOG_DIR, item.container_id);
        fp = fopen(path, "a");
        if (!fp)
            continue;
        fwrite(item.data, 1, item.length, fp);
        fclose(fp);
    }
    return NULL;
}

static void *producer_thread_fn(void *arg)
{
    producer_arg_t *parg = (producer_arg_t *)arg;
    supervisor_ctx_t *ctx = parg->ctx;
    container_t *c = parg->container;
    char buf[LOG_CHUNK];
    ssize_t n;

    while ((n = read(c->log_pipe_fd, buf, sizeof(buf))) > 0) {
        log_item_t item;
        memset(&item, 0, sizeof(item));
        strncpy(item.container_id, c->id, sizeof(item.container_id) - 1);
        memcpy(item.data, buf, n);
        item.length = (int)n;
        if (bounded_buffer_push(&ctx->log_buffer, &item) != 0)
            break;
    }

    close(c->log_pipe_fd);
    free(parg);
    return NULL;
}

static int child_fn(void *arg)
{
    child_config_t *cfg = (child_config_t *)arg;

    sethostname(cfg->id, strlen(cfg->id));

    if (chroot(cfg->rootfs) < 0) {
        perror("chroot");
        return 1;
    }
    if (chdir("/") < 0) {
        perror("chdir");
        return 1;
    }

    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        perror("mount");
    }

    dup2(cfg->log_write_fd, STDOUT_FILENO);
    dup2(cfg->log_write_fd, STDERR_FILENO);
    close(cfg->log_write_fd);

    if (cfg->nice_value != 0)
        nice(cfg->nice_value);

    execl("/bin/sh", "sh", "-c", cfg->command, NULL);
    perror("execl");
    return 1;
}

static container_t *find_container_by_id(supervisor_ctx_t *ctx, const char *id)
{
    int i;
    for (i = 0; i < ctx->container_count; i++) {
        if (strcmp(ctx->containers[i].id, id) == 0)
            return &ctx->containers[i];
    }
    return NULL;
}

static void reap_children(supervisor_ctx_t *ctx)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int i;
        pthread_mutex_lock(&ctx->containers_mutex);
        for (i = 0; i < ctx->container_count; i++) {
            container_t *c = &ctx->containers[i];
            if (c->pid == pid) {
                if (WIFEXITED(status)) {
                    c->state = c->stop_requested ? STATE_STOPPED : STATE_EXITED;
                    c->exit_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    if (WTERMSIG(status) == SIGKILL && !c->stop_requested)
                        c->state = STATE_HARD_LIMIT_KILLED;
                    else
                        c->state = STATE_STOPPED;
                    c->exit_status = 128 + WTERMSIG(status);
                }
                unregister_with_monitor(ctx->monitor_fd, c->pid, c->id);
                break;
            }
        }
        pthread_mutex_unlock(&ctx->containers_mutex);
    }
}

static void sigchld_handler(int sig)
{
    (void)sig;
    reap_children(&g_ctx);
}

static int start_container(supervisor_ctx_t *ctx, const control_request_t *req, control_response_t *resp)
{
    int pipefd[2];
    void *stack;
    pid_t pid;
    child_config_t *cfg;
    producer_arg_t *parg;
    container_t *c;

    pthread_mutex_lock(&ctx->containers_mutex);
    if (ctx->container_count >= MAX_CONTAINERS) {
        pthread_mutex_unlock(&ctx->containers_mutex);
        snprintf(resp->message, sizeof(resp->message), "too many containers");
        resp->status = 1;
        return 1;
    }

    if (find_container_by_id(ctx, req->id) != NULL) {
        pthread_mutex_unlock(&ctx->containers_mutex);
        snprintf(resp->message, sizeof(resp->message), "container id already exists");
        resp->status = 1;
        return 1;
    }

    c = &ctx->containers[ctx->container_count];
    memset(c, 0, sizeof(*c));
    strncpy(c->id, req->id, sizeof(c->id) - 1);
    strncpy(c->rootfs, req->rootfs, sizeof(c->rootfs) - 1);
    strncpy(c->command, req->command, sizeof(c->command) - 1);
    c->soft_limit = req->soft_mib * 1024UL * 1024UL;
    c->hard_limit = req->hard_mib * 1024UL * 1024UL;
    c->nice_value = req->nice_value;
    c->start_time = time(NULL);
    c->state = STATE_STARTING;
    c->stop_requested = 0;
    pthread_mutex_unlock(&ctx->containers_mutex);

    if (pipe(pipefd) < 0) {
        snprintf(resp->message, sizeof(resp->message), "pipe failed: %s", strerror(errno));
        resp->status = 1;
        return 1;
    }

    cfg = malloc(sizeof(*cfg));
    if (!cfg) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(resp->message, sizeof(resp->message), "malloc failed");
        resp->status = 1;
        return 1;
    }

    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->id, req->id, sizeof(cfg->id) - 1);
    strncpy(cfg->rootfs, req->rootfs, sizeof(cfg->rootfs) - 1);
    strncpy(cfg->command, req->command, sizeof(cfg->command) - 1);
    cfg->log_write_fd = pipefd[1];
    cfg->nice_value = req->nice_value;

    stack = malloc(STACK_SIZE);
    if (!stack) {
        free(cfg);
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(resp->message, sizeof(resp->message), "malloc stack failed");
        resp->status = 1;
        return 1;
    }

    pid = clone(child_fn, (char *)stack + STACK_SIZE,
                CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, cfg);
    if (pid < 0) {
        free(stack);
        free(cfg);
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(resp->message, sizeof(resp->message), "clone failed: %s", strerror(errno));
        resp->status = 1;
        return 1;
    }

    close(pipefd[1]);

    pthread_mutex_lock(&ctx->containers_mutex);
    c->pid = pid;
    c->state = STATE_RUNNING;
    c->log_pipe_fd = pipefd[0];
    ctx->container_count++;
    pthread_mutex_unlock(&ctx->containers_mutex);

    parg = malloc(sizeof(*parg));
    parg->ctx = ctx;
    parg->container = c;
    pthread_create(&c->producer_thread, NULL, producer_thread_fn, parg);

    register_with_monitor(ctx->monitor_fd, pid, c->id, c->soft_limit, c->hard_limit);

    snprintf(resp->message, sizeof(resp->message),
             "started container %s pid=%d", c->id, c->pid);
    resp->status = 0;
    resp->exit_code = 0;
    return 0;
}

static void handle_ps(supervisor_ctx_t *ctx, control_response_t *resp)
{
    int i;
    char line[1024];

    resp->message[0] = '\0';
    strcat(resp->message, "ID\tPID\tSTATE\tSOFT\tHARD\tNICE\tCOMMAND\n");

    pthread_mutex_lock(&ctx->containers_mutex);
    for (i = 0; i < ctx->container_count; i++) {
        container_t *c = &ctx->containers[i];
        snprintf(line, sizeof(line), "%s\t%d\t%s\t%lu\t%lu\t%d\t%s\n",
                 c->id, c->pid, state_to_string(c->state),
                 c->soft_limit / (1024UL * 1024UL),
                 c->hard_limit / (1024UL * 1024UL),
                 c->nice_value, c->command);
        strncat(resp->message, line, sizeof(resp->message) - strlen(resp->message) - 1);
    }
    pthread_mutex_unlock(&ctx->containers_mutex);

    resp->status = 0;
}

static void handle_logs(const control_request_t *req, control_response_t *resp)
{
    char path[PATH_MAX];
    FILE *fp;
    size_t n;

    snprintf(path, sizeof(path), "%s/%s.log", LOG_DIR, req->id);
    fp = fopen(path, "r");
    if (!fp) {
        snprintf(resp->message, sizeof(resp->message), "log file not found for %s", req->id);
        resp->status = 1;
        return;
    }

    n = fread(resp->message, 1, sizeof(resp->message) - 1, fp);
    resp->message[n] = '\0';
    fclose(fp);
    resp->status = 0;
}

static void handle_stop(supervisor_ctx_t *ctx, const control_request_t *req, control_response_t *resp)
{
    container_t *c;

    pthread_mutex_lock(&ctx->containers_mutex);
    c = find_container_by_id(ctx, req->id);
    if (!c) {
        pthread_mutex_unlock(&ctx->containers_mutex);
        snprintf(resp->message, sizeof(resp->message), "container not found");
        resp->status = 1;
        return;
    }

    c->stop_requested = 1;
    kill(c->pid, SIGTERM);
    pthread_mutex_unlock(&ctx->containers_mutex);

    snprintf(resp->message, sizeof(resp->message), "stop signal sent to %s", req->id);
    resp->status = 0;
}

static int parse_request_from_argv(int argc, char **argv, control_request_t *req)
{
    int i;

    memset(req, 0, sizeof(*req));
    req->soft_mib = DEFAULT_SOFT_MIB;
    req->hard_mib = DEFAULT_HARD_MIB;
    req->nice_value = 0;

    if (argc < 2)
        return -1;

    if (strcmp(argv[1], "start") == 0 || strcmp(argv[1], "run") == 0) {
        if (argc < 5)
            return -1;

        req->type = (strcmp(argv[1], "start") == 0) ? CMD_START : CMD_RUN;
        strncpy(req->id, argv[2], sizeof(req->id) - 1);
        strncpy(req->rootfs, argv[3], sizeof(req->rootfs) - 1);
        strncpy(req->command, argv[4], sizeof(req->command) - 1);

        for (i = 5; i < argc; i++) {
            if (strcmp(argv[i], "--soft-mib") == 0 && i + 1 < argc) {
                req->soft_mib = strtoul(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--hard-mib") == 0 && i + 1 < argc) {
                req->hard_mib = strtoul(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--nice") == 0 && i + 1 < argc) {
                req->nice_value = atoi(argv[++i]);
            }
        }
        return 0;
    }

    if (strcmp(argv[1], "ps") == 0) {
        req->type = CMD_PS;
        return 0;
    }

    if (strcmp(argv[1], "logs") == 0 && argc >= 3) {
        req->type = CMD_LOGS;
        strncpy(req->id, argv[2], sizeof(req->id) - 1);
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0 && argc >= 3) {
        req->type = CMD_STOP;
        strncpy(req->id, argv[2], sizeof(req->id) - 1);
        return 0;
    }

    return -1;
}

static int send_control_request(const control_request_t *req)
{
    int fd;
    struct sockaddr_un addr;
    control_response_t resp;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    if (write(fd, req, sizeof(*req)) != (ssize_t)sizeof(*req)) {
        perror("write");
        close(fd);
        return 1;
    }

    if (read(fd, &resp, sizeof(resp)) > 0) {
        printf("%s\n", resp.message);
    }

    close(fd);
    return resp.status;
}

static void run_supervisor(const char *base_rootfs)
{
    struct sockaddr_un addr;
    int client_fd;
    (void)base_rootfs;

    memset(&g_ctx, 0, sizeof(g_ctx));
    bounded_buffer_init(&g_ctx.log_buffer);
    pthread_mutex_init(&g_ctx.containers_mutex, NULL);
    mkdir(LOG_DIR, 0755);

    g_ctx.monitor_fd = open("/dev/container_monitor", O_RDWR);
    if (g_ctx.monitor_fd < 0)
        perror("open /dev/container_monitor");

    signal(SIGCHLD, sigchld_handler);

    unlink(CONTROL_PATH);
    g_ctx.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ctx.server_fd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_PATH, sizeof(addr.sun_path) - 1);

    if (bind(g_ctx.server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(g_ctx.server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    pthread_create(&g_ctx.logger_thread, NULL, logging_thread, &g_ctx);

    printf("supervisor running on %s\n", CONTROL_PATH);

    while (!g_ctx.shutting_down) {
        control_request_t req;
        control_response_t resp;

        client_fd = accept(g_ctx.server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        memset(&req, 0, sizeof(req));
        memset(&resp, 0, sizeof(resp));

        if (read(client_fd, &req, sizeof(req)) <= 0) {
            close(client_fd);
            continue;
        }

        reap_children(&g_ctx);

        switch (req.type) {
        case CMD_START:
        case CMD_RUN:
            start_container(&g_ctx, &req, &resp);
            break;
        case CMD_PS:
            handle_ps(&g_ctx, &resp);
            break;
        case CMD_LOGS:
            handle_logs(&req, &resp);
            break;
        case CMD_STOP:
            handle_stop(&g_ctx, &req, &resp);
            break;
        default:
            resp.status = 1;
            snprintf(resp.message, sizeof(resp.message), "unknown command");
        }

        write(client_fd, &resp, sizeof(resp));
        close(client_fd);
    }

    bounded_buffer_shutdown(&g_ctx.log_buffer);
    pthread_join(g_ctx.logger_thread, NULL);
    if (g_ctx.monitor_fd >= 0)
        close(g_ctx.monitor_fd);
    close(g_ctx.server_fd);
    unlink(CONTROL_PATH);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s supervisor <base-rootfs>\n"
            "  %s start <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s run   <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s ps\n"
            "  %s logs <id>\n"
            "  %s stop <id>\n",
            prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    control_request_t req;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }
        run_supervisor(argv[2]);
        return 0;
    }

    if (parse_request_from_argv(argc, argv, &req) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    return send_control_request(&req);
}
