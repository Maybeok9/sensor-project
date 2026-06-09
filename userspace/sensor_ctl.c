#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

char socket_path[256] = "/tmp/run.socket";

int main(int argc, char *argv[]) {

    
    if (argc < 2) {
        fprintf(stderr, "  %s stats <0|1|2|all>\n", argv[0]);
        fprintf(stderr, "  %s set-rate <0|1|2|all> <Hz>\n", argv[0]);
        fprintf(stderr, "  %s pause <0|1|2>\n", argv[0]);
        fprintf(stderr, "  %s resume <0|1|2>\n", argv[0]);
        fprintf(stderr, "  %s status\n", argv[0]);
        fprintf(stderr, "  %s reset <0|1|2|all>\n", argv[0]);
        fprintf(stderr, "  %s set-srate <0|1|2|all> <Hz>\n", argv[0]);
        return 1;
    }

    int cli_fd;
    struct sockaddr_un addr;
    char cmd_buf[128] = {0};
    char rx_buf[1024] = {0};

    for (int i = 1; i < argc; i++) {
        strcat(cmd_buf, argv[i]);
        if (i < argc - 1) {
            strcat(cmd_buf, " ");
        }
    }

    cli_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli_fd < 0) {
        write(2,"Cant create to socket\n",23);
        return 1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(cli_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
        write(2,"Cant connect to collector\n",27);
        close(cli_fd);
        return 1;
    }

    send(cli_fd, cmd_buf, strlen(cmd_buf), 0);

    int bytes_received = recv(cli_fd, rx_buf, sizeof(rx_buf) - 1, 0);
    if (bytes_received > 0) {
        printf("%s", rx_buf);
    } else {
        printf("Received nothing\n");
    }

    close(cli_fd);
    return 0;
}
