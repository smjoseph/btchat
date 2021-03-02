/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

static long psm = 0x1213;
static char* dest = NULL;
static int server = 0;
static char* handle = NULL;
static size_t handle_len = 0;
static char* prompt = NULL;
static size_t prompt_len = 0;
static int sock = 0;
static int buf_size = 128;
static volatile sig_atomic_t connected = 0;

static void PrintUsage(void)
{
    printf("btchat - primitive chat implemented over bluetooth l2cap\n");
    printf("Usage: btchat [-c bdaddr] [-l] [-h handle] [-p psm]\n");
    printf("\t-c bdaddr  Connect to bdaddr (format: xx:xx:xx:xx:xx:xx)\n");
    printf("\t-l         Listen for connections\n");
    printf("\t-h handle  Set user handle\n");
    printf("\t-p psm     Set PSM in hex\n");
    printf("\t-b bufsize Set message buffer size in bytes. 128 by default.\n");
}

static int SetPrompt()
{
    prompt_len = handle_len + 2;
    prompt = malloc(sizeof(char) * prompt_len);
    snprintf(prompt, prompt_len + 1, "%s: ", handle);

    return 0;
}

static int SetDefaultHandle()
{
    char* username;
    username = getpwuid(getuid())->pw_name;

    char hostname[32];
    gethostname(hostname, sizeof(hostname));

    handle_len = strlen(username) + 1 + strlen(hostname);
    handle = malloc(sizeof(char) * (handle_len + 1));
    snprintf(handle, handle_len + 1, "%s@%s", username, hostname);

    return 0;
}

static int GetOptions(int argc, char** argv)
{
    if(argc < 2)
    {
        PrintUsage();
        return 1;
    }

    int c;
    while((c = getopt(argc, argv, "c:lh:p:b:")) != -1)
    {
        switch(c)
        {
            case 'c':
                server = 0;
                dest = optarg;
                break;
            case 'l':
                server = 1;
                break;
            case 'h':
                handle = optarg;
                handle_len = strlen(handle);
                break;
            case 'p':
                psm = strtol(optarg, NULL, 16);
                break;
            case 'b':
                buf_size = strtol(optarg, NULL, 10);
                break;
            default:
                PrintUsage();
                return 1;
        }
    }

    if(handle == NULL)
    {
        SetDefaultHandle();
    }

    SetPrompt();

    return 0;
}

static int GetDevInfo(bdaddr_t* bdaddr, struct hci_dev_info* hdi)
{
    int dev_id = hci_get_route(bdaddr);
    hci_devinfo(dev_id, hdi);

    return 0;
}

static int Connect()
{
    struct sockaddr_l2 addr = {0};
    addr.l2_family = AF_BLUETOOTH;
    str2ba(dest, &addr.l2_bdaddr);
    addr.l2_psm = htobs(psm);

    sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

    printf("connecting to: %s, 0x%04lx\n", dest, psm);
    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0)
    {
        connected = 1;
        printf("connected\n");
    }

    return sock;
}

static int Listen()
{
    int listen_sock = 0;
    struct hci_dev_info dev_info = {0};
    struct sockaddr_l2 local_sockaddr = {0}, remote_sockaddr = {0};

    GetDevInfo(NULL, &dev_info);
    local_sockaddr.l2_family = AF_BLUETOOTH;
    bacpy(&local_sockaddr.l2_bdaddr, &dev_info.bdaddr);
    local_sockaddr.l2_psm = htobs(psm);
    socklen_t remote_sockaddr_len = sizeof(remote_sockaddr);

    listen_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    bind(listen_sock, (struct sockaddr*)&local_sockaddr, sizeof(local_sockaddr));
    listen(listen_sock, 1);

    char local_addr_str[18];
    ba2str(&local_sockaddr.l2_bdaddr, local_addr_str);
    printf("listening on: %s, 0x%04lx\n", local_addr_str, psm);

    struct pollfd pollfds[] = {{.fd = listen_sock, .events = POLLIN}};
    int pollfds_size = sizeof(pollfds)/sizeof(pollfds[0]);
    struct pollfd* listen_sock_pollfd = &pollfds[0];

    while(!connected)
    {
        if(poll(pollfds, pollfds_size, 500) > 0)
        {
            if(listen_sock_pollfd->revents & POLLIN)
            {
                sock = accept(listen_sock, (struct sockaddr*)&remote_sockaddr, &remote_sockaddr_len);

                char remote_addr_str[18];
                ba2str(&remote_sockaddr.l2_bdaddr, remote_addr_str);
                printf("accepted connection from: %s\n", remote_addr_str);

                connected = 1;
                break;
            }
        }
    }

    close(listen_sock);

    return sock;
}

static int MessageOutgoing(struct pollfd* stdin_pollfd, char* buf)
{
    if((stdin_pollfd->fd != STDIN_FILENO) || !(stdin_pollfd->revents & POLLIN))
    {
        return 1;
    }

    snprintf(buf, prompt_len + 1, "%s", prompt);

    char* s = fgets(buf + prompt_len, buf_size - prompt_len, stdin);

    printf("%s", buf);

    if(write(sock, buf, buf_size) <= 0)
    {
        printf("failed to send\n");
    }

    return 0;
}

static int MessageIncoming(struct pollfd* sock_pollfd, char* buf)
{
    if((sock_pollfd->fd != sock) || !(sock_pollfd->revents & POLLIN))
    {
        return 1;
    }

    if(read(sock, buf, buf_size) > 0)
    {
        printf("%s", buf);
    }

    return 0;
}

static int MessagingLoop()
{
    char buf[buf_size];
    struct pollfd pollfds[] = {{ .fd = STDIN_FILENO, .events = POLLIN}, { .fd = sock, .events = POLLIN}};
    int pollfds_size = sizeof(pollfds)/sizeof(pollfds[0]);
    struct pollfd* stdin_pollfd = &pollfds[0];
    struct pollfd* sock_pollfd = &pollfds[1];

    while(connected)
    {
        poll(pollfds, pollfds_size, 500);
        MessageOutgoing(stdin_pollfd, buf);
        MessageIncoming(sock_pollfd, buf);
    }

    return 0;
}

int main(int argc, char** argv)
{
    if(!GetOptions(argc, argv))
    {
        server ? Listen() : Connect();
    }

    MessagingLoop();

    close(sock);

    return 0;
}
