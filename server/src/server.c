#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../include/commands.h"
#include "../include/handlers.h"
#include "../include/dictionary.h"
#include "../include/snapshot.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define MAX 1024
#define PORT 7379

static CommandRegistry g_cmd_reg;

static bool write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

static void buffer_to_command(char *buffer, int connfd)
{
    size_t len = strlen(buffer);

    while (len > 0 &&
           (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
    {
        buffer[--len] = '\0';
    }

    if (len == 0)
        return;

    char scratch[MAX];
    snprintf(scratch, sizeof(scratch), "%s", buffer);

    ParsedLine line;

    if (!parse_line_universal(scratch, &line))
    {
        const char *err = "-ERR too many tokens\r\n";

        if (!write_all(connfd, err, strlen(err)))
        {
            perror("write");
            exit(EXIT_FAILURE);
        }

        return;
    }

    char resp[256] = {0};

    cmd_execute(
        &g_cmd_reg,
        &line,
        resp,
        sizeof(resp));

    if (!write_all(connfd, resp, strlen(resp)))
    {
        perror("write");
        exit(EXIT_FAILURE);
    }
}

static void handle_client(int connfd)
{
    char buff[MAX];

    for (;;)
    {
        memset(buff, 0, sizeof buff);

        /* Leave 1 byte for '\0' so %s is always safe */
        ssize_t n = read(connfd, buff, sizeof(buff) - 1);
        if (n <= 0)
        {
            printf("Client disconnected.\n");
            break;
        }
        buff[n] = '\0';

        buffer_to_command(buff, connfd);
        printf("From client: %s\n", buff);

        if (strcasecmp(buff, "exit") == 0)
        {
            printf("Server Exit...\n");
            break;
        }
    }
}

int main(void)
{
    Dictionary *dict = dict_create(INITIAL_CAPCITY);

    if (dict == NULL)
    {
        fprintf(stderr, "Failed to create dictionary\n");
        return 1;
    }

    const char *snapshot_path = default_snapshot_file();
    char directory[1024];
    snprintf(directory, sizeof(directory),
             "%s", snapshot_path);

    char *last_slash = strrchr(directory, '/');
    if (last_slash)
    {
        *last_slash = '\0';

        if (mkdir(directory, 0755) != 0 && errno != EEXIST)
        {
            perror("mkdir");
            return 1;
        }
    }
    if (!snapshot_load(dict, default_snapshot_file()))
    {
        fprintf(stderr, "Snapshot loading failed\n");
        dict_free(dict);
        return 1;
    }

    if (!cmd_registry_load(&g_cmd_reg, "commands/commands.json"))
    {
        fprintf(stderr, "Failed to load commands\n");
        dict_free(dict);
        return 1;
    }

    if (!handlers_init(dict))
    {
        fprintf(stderr, "Failed to register handlers\n");
        dict_free(dict);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }
    printf("Socket successfully created..\n");

    /* Avoid "Address already in use" when restarting the server */
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof servaddr) != 0)
    {
        perror("bind");
        close(sockfd);
        return 1;
    }
    printf("Socket successfully binded..\n");

    if (listen(sockfd, 5) != 0)
    {
        perror("listen");
        close(sockfd);
        return 1;
    }
    printf("Server listening on port %d..\n", PORT);

    struct sockaddr_in cli;
    socklen_t len = sizeof cli;
    int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
    if (connfd < 0)
    {
        perror("accept");
        close(sockfd);
        return 1;
    }
    printf("Server accepted the client..\n");

    handle_client(connfd);

    close(connfd);
    close(sockfd);
    if (!snapshot_save(dict, default_snapshot_file()))
    {
        fprintf(stderr, "Snapshot saving failed\n");
        dict_free(dict);
        return 1;
    }
    dict_free(dict);
    return 0;
}