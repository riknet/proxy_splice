#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include<fcntl.h>

#define BUF 10


int ready(int fd)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd,&fds);

	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;

    int retval;
    retval = select(fd+1, &fds, NULL, NULL, &tv);
	printf("retval: %d\n", retval);
	
	if (FD_ISSET(fd, &fds))
	{
		return fd;
	}
	else
	{
		return -1;
	}
}


int main(int argc, char *argv[])
{
	// socket //
	int sock;
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1)
	{
		printf("socket() failed: %d: %s \n", errno, strerror(errno));
		exit(1);
	}
	else
	{
		printf("socket() success: %d \n", sock);
	}
	//int nb;
	//nb = fcntl(sock, F_SETFL, O_NONBLOCK);
	//printf("fnctl: %d \n", nb);
	// end socket //

	// address //
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(80);
	int pton;
	pton = inet_pton(AF_INET, "192.168.1.100", &addr.sin_addr);
	if (pton != 1)
	{
		printf("inet_pton() failed: %d: \n", pton);
		exit(1);
	}
	else
	{
		printf("inet_pton() success: %d \n", pton);
	}
	// end address //

	
	// connect //
	int conn;
	conn = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	while (conn != 0)
	{
		printf("connect() failed: %d: %s \n", errno, strerror(errno));
		//exit(1);
	}
	//else
	//{
		printf("connect() success: %d \n", sock);
	//}
	// end connect //


	// write //
	const char* get[] = {
							"GET / HTTP/1.1\n",
							"User-Agent: richard\n",
							"Host: 192.168.1.100\n",
							"\n"
						};

	int g;
	for (g = 0 ; g < (sizeof(get)/sizeof(get[0])) ; g++)
	{
		size_t write_size;
		write_size = write(sock, get[g], strlen(get[g]));
		if (write_size == -1)
		{
			printf("write() failed: %d: %s \n", errno, strerror(errno));
			exit(1);
		}
		else
		{
			//printf("write() success: %ld \n", write_size);
			printf("> %s", get[g]);
		}
	}
	// end write //


	// read //
	char buffer[BUF];
	size_t recv_size = -1;
	while (recv_size != 0)
	{
		int sel = ready(sock);
		if (sel != -1)
		{
			recv_size = recv(sel, &buffer, BUF, MSG_DONTWAIT);
			if (recv_size == -1)
			{
				if (errno == EAGAIN)
				{
					printf("EAGAIN %d\n", errno);
					ready(sock);
				}
				else 
				{
					printf("recv() failed: %d: %s \n", errno, strerror(errno));
					break;
				}
			}
			else
			{
				//printf("recv: %ld \n", recv_size);
				//printf("buffer: \n%s", buffer);
				printf("%s", buffer);
			}
		}
	}
	// end read //
	
	printf("DONE\n");

	exit(0);

}
