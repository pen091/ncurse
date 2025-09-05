/*
 * server.c
 * Simple chat server:
 * - Accepts multiple clients (pthread per client)
 * - Maintains list of clients and usernames
 * - Broadcasts public messages
 * - Routes private messages starting with "@username "
 * - Logs all messages to chat.log with timestamps
 *
 * Compile:
 *   gcc -pthread -o server server.c
 *
 * Run:
 *   ./server 12345
 *
 * Use ngrok to expose: `ngrok tcp 12345`
 */

#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 1024
#define BUF_SIZE 4096
#define NAME_LEN 32
#define LOGFILE "chat.log"

typedef struct {
    int sock;
    char name[NAME_LEN];
} client_t;

client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *s) {
    FILE *f = fopen(LOGFILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    char buf[64];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(f, "[%s] %s\n", buf, s);
    fclose(f);
}

void send_to_sock(int sock, const char *msg) {
    if (send(sock, msg, strlen(msg), 0) < 0) {
        perror("send");
    }
}

void broadcast(const char *sender, const char *msg) {
    char out[BUF_SIZE+128];
    snprintf(out, sizeof(out), "%s: %s\n", sender, msg);
    pthread_mutex_lock(&clients_mutex);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i]) {
            send_to_sock(clients[i]->sock, out);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    log_msg(out);
}

client_t *find_by_name(const char *name) {
    client_t *found = NULL;
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i] && strcmp(clients[i]->name, name) == 0) {
            found = clients[i];
            break;
        }
    }
    return found;
}

void notify_userlist() {
    // Build userlist string and broadcast as special message prefixed with "\x01USERS:"
    char list[BUF_SIZE] = {0};
    strcat(list, "\x01USERS:");
    pthread_mutex_lock(&clients_mutex);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i]) {
            strcat(list, clients[i]->name);
            strcat(list, ",");
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    // send to all clients
    pthread_mutex_lock(&clients_mutex);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i]) send_to_sock(clients[i]->sock, list);
    }
    pthread_mutex_unlock(&clients_mutex);
}

void add_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    notify_userlist();
}

void remove_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i] == cl) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    notify_userlist();
}

void *handle_client(void *arg) {
    client_t *cli = (client_t*)arg;
    char buf[BUF_SIZE];
    // first message should be the username (null-terminated)
    ssize_t r = recv(cli->sock, buf, NAME_LEN-1, 0);
    if (r <= 0) { close(cli->sock); remove_client(cli); free(cli); return NULL; }
    buf[r] = '\0';
    strncpy(cli->name, buf, NAME_LEN-1);
    // announce
    char joinmsg[128]; snprintf(joinmsg, sizeof(joinmsg), "*** %s joined", cli->name);
    broadcast("server", joinmsg);

    while (1) {
        ssize_t len = recv(cli->sock, buf, BUF_SIZE-1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        // check private message: starts with @username<space>
        if (buf[0] == '@') {
            // parse name
            char target[NAME_LEN] = {0};
            int i=1, j=0;
            while (i < len && buf[i] != ' ' && j < NAME_LEN-1) {
                target[j++] = buf[i++]; 
            }
            target[j] = '\0';
            char *message = buf + i;
            client_t *rcv = find_by_name(target);
            char out[BUF_SIZE+64];
            snprintf(out, sizeof(out), "(private) %s -> %s: %s\n", cli->name, target, message);
            log_msg(out);
            // send to target and sender and server
            if (rcv) send_to_sock(rcv->sock, out);
            send_to_sock(cli->sock, out);
        } else {
            // public broadcast
            broadcast(cli->name, buf);
        }
    }

    // disconnect
    close(cli->sock);
    char leavemsg[128]; snprintf(leavemsg, sizeof(leavemsg), "*** %s left", cli->name);
    broadcast("server", leavemsg);
    remove_client(cli);
    free(cli);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]); exit(1);
    }
    int port = atoi(argv[1]);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv;
    serv.sin_family = AF_INET; serv.sin_addr.s_addr = INADDR_ANY; serv.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) { perror("bind"); exit(1); }
    if (listen(listenfd, 10) < 0) { perror("listen"); exit(1); }
    printf("Server listening on port %d\n", port);
    // clear log
    FILE *f = fopen(LOGFILE, "a"); if (f) fclose(f);

    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int conn = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn < 0) { perror("accept"); continue; }
        client_t *cli = (client_t*)malloc(sizeof(client_t));
        cli->sock = conn;
        cli->name[0] = '\0';
        add_client(cli);
        pthread_t tid;
        pthread_create(&tid, NULL, &handle_client, (void*)cli);
        pthread_detach(tid);
    }
    close(listenfd);
    return 0;
}
