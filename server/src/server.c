#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../include/commands.h"
#include "../include/handlers.h"
#include "../include/dictionary.h"
#include "../include/snapshot.h"
#include "../include/active_expire.h"
#include "../include/background_snapshot.h"

#define MAX 1024
#define PORT 7379
#define MAX_EVENTS 64
#define LISTEN_BACKLOG 128

static CommandRegistry g_cmd_reg;
static volatile sig_atomic_t g_running = 1;

typedef struct
{
    int fd;
    size_t len;
    char buf[MAX];
} Client;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

static bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return false;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool mkdir_p(const char *path)
{
    char tmp[1024];

    if (snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp)
        return false;

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p != '/')
            continue;

        *p = '\0';

        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return false;

        *p = '/';
    }

    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static bool write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = write(fd, buf + sent, len - sent);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0)
            return false;

        sent += (size_t)n;
    }

    return true;
}


static bool buffer_to_command(char *buffer, int connfd)
{
    size_t len = strlen(buffer);

    while (len > 0 &&
           (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
    {
        buffer[--len] = '\0';
    }

    if (len == 0)
        return true;

    char scratch[MAX];
    snprintf(scratch, sizeof(scratch), "%s", buffer);

    ParsedLine line;

    if (!parse_line_universal(scratch, &line))
    {
        const char *err = "-ERR too many tokens\r\n";
        return write_all(connfd, err, strlen(err));
    }

    char resp[256] = {0};

    cmd_execute(
        &g_cmd_reg,
        &line,
        resp,
        sizeof(resp));

    return write_all(connfd, resp, strlen(resp));
}

static void close_client(int epfd, Client *c)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    printf("Client disconnected.\n");
    free(c);
}


static void handle_readable(int epfd, Client *c)
{
    ssize_t n = read(c->fd, c->buf + c->len, sizeof(c->buf) - 1 - c->len);

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;

    if (n <= 0)
    {
        close_client(epfd, c);
        return;
    }

    c->len += (size_t)n;
    c->buf[c->len] = '\0';

    char *start = c->buf;
    char *nl;

    while ((nl = memchr(start, '\n', c->len - (size_t)(start - c->buf))) != NULL)
    {
        *nl = '\0';

        char line[MAX];
        snprintf(line, sizeof line, "%s", start);
        start = nl + 1;

        /* Trim a trailing '\r' so "exit\r\n" is recognised. */
        size_t ll = strlen(line);
        while (ll > 0 && line[ll - 1] == '\r')
            line[--ll] = '\0';

        if (ll == 0)
            continue;

        printf("From client: %s\n", line);

        if (strcasecmp(line, "exit") == 0)
        {
            close_client(epfd, c);
            return;
        }

        if (!buffer_to_command(line, c->fd))
        {
            close_client(epfd, c);
            return;
        }
    }

    size_t remaining = c->len - (size_t)(start - c->buf);
    memmove(c->buf, start, remaining);
    c->len = remaining;

    if (c->len >= sizeof(c->buf) - 1)
    {
        const char *err = "-ERR line too long\r\n";
        write_all(c->fd, err, strlen(err));
        close_client(epfd, c);
    }
}

static void accept_clients(int epfd, int sockfd)
{
    for (;;)
    {
        int fd = accept(sockfd, NULL, NULL);

        if (fd < 0)
        {
            if (errno == EINTR)
                continue;

            return;
        }

        if (!set_nonblocking(fd))
        {
            close(fd);
            continue;
        }

        Client *c = calloc(1, sizeof *c);

        if (c == NULL)
        {
            close(fd);
            continue;
        }

        c->fd = fd;

        struct epoll_event ev;
        memset(&ev, 0, sizeof ev);
        ev.events = EPOLLIN;
        ev.data.ptr = c;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
        {
            perror("epoll_ctl");
            close(fd);
            free(c);
            continue;
        }

        printf("Server accepted a client..\n");
    }
}

int main(void)
{
    int result = 1;
    int sockfd = -1;
    int epfd = -1;
    bool expire_thread_started = false;
    bool snapshot_thread_started = false;
    bool save_snapshot = false;

    pthread_t expire_thread;
    pthread_t snapshot_thread;
    ActiveExpireContext expire_ctx;
    SnapshotContext snapshot_ctx;

    memset(&expire_ctx, 0, sizeof expire_ctx);
    memset(&snapshot_ctx, 0, sizeof snapshot_ctx);

    install_signal_handlers();

    Dictionary *dict = dict_create(INITIAL_CAPCITY);

    if (dict == NULL)
    {
        fprintf(stderr, "Failed to create dictionary\n");
        return 1;
    }

    const char *snapshot_path = default_snapshot_file();

    char directory[1024];

    if (snprintf(directory, sizeof directory, "%s",
                 snapshot_path) >= (int)sizeof directory)
    {
        fprintf(stderr, "Snapshot path too long\n");
        goto cleanup;
    }

    char *last_slash = strrchr(directory, '/');

    if (last_slash)
    {
        *last_slash = '\0';

        if (!mkdir_p(directory))
        {
            perror("mkdir");
            goto cleanup;
        }
    }

    if (!snapshot_load(dict, snapshot_path))
    {
        fprintf(stderr, "Snapshot loading failed\n");
        goto cleanup;
    }

    snapshot_ctx.dict = dict;
    snapshot_ctx.path = snapshot_path;
    atomic_init(&snapshot_ctx.stop, false);

    if (pthread_create(
            &snapshot_thread,
            NULL,
            snapshot_worker,
            &snapshot_ctx) == 0)
    {
        snapshot_thread_started = true;
    }
    else
    {
        fprintf(stderr, "Could not start snapshot worker\n");
    }

    if (!cmd_registry_load(&g_cmd_reg, "commands/commands.json"))
    {
        fprintf(stderr, "Failed to load commands\n");
        goto cleanup;
    }

    if (!handlers_init(dict))
    {
        fprintf(stderr, "Failed to register handlers\n");
        goto cleanup;
    }

    expire_ctx.dict = dict;
    atomic_init(&expire_ctx.stop, false);

    if (pthread_create(&expire_thread, NULL,
                       active_expire, &expire_ctx) != 0)
    {
        perror("Failed to create expiration thread");
        goto cleanup;
    }

    expire_thread_started = true;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        goto cleanup;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&servaddr,
             sizeof servaddr) != 0)
    {
        perror("bind");
        goto cleanup;
    }

    if (listen(sockfd, LISTEN_BACKLOG) != 0)
    {
        perror("listen");
        goto cleanup;
    }

    if (!set_nonblocking(sockfd))
    {
        perror("fcntl");
        goto cleanup;
    }

    epfd = epoll_create1(0);

    if (epfd < 0)
    {
        perror("epoll_create1");
        goto cleanup;
    }

    struct epoll_event lev;
    memset(&lev, 0, sizeof lev);
    lev.events = EPOLLIN;
    lev.data.ptr = NULL;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &lev) != 0)
    {
        perror("epoll_ctl");
        goto cleanup;
    }

    printf("Server listening on port %d..\n", PORT);

    struct epoll_event events[MAX_EVENTS];

    while (g_running)
    {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++)
        {
            if (events[i].data.ptr == NULL)
                accept_clients(epfd, sockfd);
            else
                handle_readable(epfd, (Client *)events[i].data.ptr);
        }
    }

    printf("Server shutting down...\n");

    save_snapshot = true;
    result = 0;

cleanup:

    if (epfd >= 0)
        close(epfd);

    if (sockfd >= 0)
        close(sockfd);

    if (expire_thread_started)
    {
        atomic_store(&expire_ctx.stop, true);
        pthread_join(expire_thread, NULL);
    }

    if (snapshot_thread_started)
    {
        atomic_store(&snapshot_ctx.stop, true);
        pthread_join(snapshot_thread, NULL);
    }

    if (save_snapshot)
    {
        if (!snapshot_save(dict, snapshot_path))
        {
            fprintf(stderr, "Snapshot saving failed\n");
            result = 1;
        }
    }

    dict_free(dict);

    return result;
}