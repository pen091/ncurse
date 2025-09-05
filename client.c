/*
 * client.c
 * - Connects to server
 * - Sends username first
 * - ncurses UI with 3-pane layout:
 *    left: banner CARD (green)
 *    center: chat area (scrolling)
 *    right: user list (updated on special messages)
 *    bottom: input line with prompt [username] -->
 *
 * Compile:
 *   gcc -pthread -lncurses -o client client.c
 *
 * Run:
 *   ./client <server-ip> <port> <username>
 *
 * If server is behind ngrok (tcp), use the ngrok host:port for <server-ip> <port>.
 */

#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096
#define NAME_LEN 32

int sockfd;
char username[NAME_LEN];

WINDOW *win_left, *win_center, *win_right, *win_bottom;
pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

void draw_banner() {
    int h,w; getmaxyx(win_left, h, w);
    werase(win_left);
    // draw a simple CARD with greenish text (use color pair)
    wattron(win_left, COLOR_PAIR(2) | A_BOLD);
    mvwprintw(win_left, 1, 2, "####################");
    mvwprintw(win_left, 2, 2, "#     BLACKFISH    #");
    mvwprintw(win_left, 3, 2, "#   CLI CHAT APP   #");
    mvwprintw(win_left, 4, 2, "####################");
    wattroff(win_left, COLOR_PAIR(2) | A_BOLD);
    box(win_left, 0, 0);
    wrefresh(win_left);
}

void append_center(const char *s) {
    pthread_mutex_lock(&ui_mutex);
    // scroll center window
    int maxy, maxx; getmaxyx(win_center, maxy, maxx);
    // write at bottom-2, then scroll up
    wscrl(win_center, 1);
    mvwprintw(win_center, maxy-2, 1, "%.*s", maxx-3, s);
    box(win_center, 0, 0);
    wrefresh(win_center);
    pthread_mutex_unlock(&ui_mutex);
}

void update_userlist(const char *csv) {
    pthread_mutex_lock(&ui_mutex);
    werase(win_right);
    box(win_right, 0, 0);
    mvwprintw(win_right, 1, 1, "Users:");
    int row = 2;
    char tmp[BUF_SIZE]; strncpy(tmp, csv, sizeof(tmp)-1);
    char *p = strtok(tmp, ",");
    while (p) {
        if (strlen(p)>0) {
            mvwprintw(win_right, row++, 1, "%s", p);
        }
        p = strtok(NULL, ",");
    }
    wrefresh(win_right);
    pthread_mutex_unlock(&ui_mutex);
}

void *recv_thread(void *arg) {
    char buf[BUF_SIZE];
    while (1) {
        ssize_t r = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (r <= 0) {
            append_center("*** disconnected from server");
            break;
        }
        buf[r] = '\0';
        // special userlist message starts with \x01USERS:
        if (buf[0] == 0x01) {
            if (strncmp(buf+1, "USERS:", 6) == 0) {
                update_userlist(buf+7);
                continue;
            }
        }
        // otherwise normal message
        append_center(buf);
    }
    return NULL;
}

void resize_ui() {
    int height, width; getmaxyx(stdscr, height, width);
    int left_w = width/6; // left narrow column
    int right_w = width/6;
    int center_w = width - left_w - right_w;
    int bottom_h = 3;
    int center_h = height - bottom_h;

    // delete old windows if exist
    if (win_left) delwin(win_left);
    if (win_center) delwin(win_center);
    if (win_right) delwin(win_right);
    if (win_bottom) delwin(win_bottom);

    win_left = newwin(center_h, left_w, 0, 0);
    win_center = newwin(center_h, center_w, 0, left_w);
    win_right = newwin(center_h, right_w, 0, left_w+center_w);
    win_bottom = newwin(bottom_h, width, center_h, 0);

    // styling
    wbkgd(win_left, COLOR_PAIR(1));
    wbkgd(win_center, COLOR_PAIR(1));
    wbkgd(win_right, COLOR_PAIR(1));
    wbkgd(win_bottom, COLOR_PAIR(1));
    draw_banner();
    box(win_center, 0, 0);
    box(win_right, 0, 0);
    wrefresh(win_center);
    wrefresh(win_right);
    wrefresh(win_bottom);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server-ip> <port> <username>\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    strncpy(username, argv[3], NAME_LEN-1);

    // connect to server
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv.sin_addr);
    if (connect(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("connect");
        exit(1);
    }

    // send username as first message
    send(sockfd, username, strlen(username), 0);

    // init ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_WHITE, -1);
    init_pair(2, COLOR_GREEN, -1);
    curs_set(1);
    resize_ui();

    // start recv thread
    pthread_t rtid;
    pthread_create(&rtid, NULL, &recv_thread, NULL);

    // main input loop
    char input[BUF_SIZE];
    while (1) {
        // bottom input prompt
        pthread_mutex_lock(&ui_mutex);
        werase(win_bottom);
        box(win_bottom, 0, 0);
        mvwprintw(win_bottom, 1, 1, "[%s] --> ", username);
        wrefresh(win_bottom);
        // read input from user (use wgetnstr)
        echo();
        mvwgetnstr(win_bottom, 1, 6 + strlen(username) + 6, input, BUF_SIZE-1);
        noecho();
        pthread_mutex_unlock(&ui_mutex);

        if (strcmp(input, "/quit") == 0) break;
        // send to server
        if (send(sockfd, input, strlen(input), 0) < 0) {
            append_center("*** failed to send");
            break;
        }
    }

    // cleanup
    close(sockfd);
    endwin();
    return 0;
}
