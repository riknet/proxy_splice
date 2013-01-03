#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>

#define BUF 16384
#define BACKLOG 100
#define MAXCLIENTS 100	

struct cli_struct
{
	int run;
	int client_socket;
	struct sockaddr_in client_addr;
	int upstream_socket;
	struct sockaddr_in upstream_addr;
};

// set up cli_struct array //
struct cli_struct clients[MAXCLIENTS];

int find()
{
	// find an unused client //
	int id;
	for (id = 0 ; id < MAXCLIENTS ; id++)
	{
		if (clients[id].run == 0)
		{
			return id;
		}
	}

	// or return -1 if none are available //
	return -1;
}

int clear(int id)
{
	int closed;
	// close client //
	if (clients[id].client_socket != 0)
	{
		closed = close(clients[id].client_socket);
		if (closed == -1)
		{
			printf("worker: close() client failed: %d: %s \n", errno, strerror(errno));
		}
		else
		{
			printf("worker(%d): client closed \n", id);
		}
	}
	clients[id].client_socket = 0;
	// end close client

	// close proxy //
	if (clients[id].upstream_socket != 0)
	{
		closed = close(clients[id].upstream_socket);
		if (closed == -1)
		{
			printf("worker: close() proxy failed: %d: %s \n", errno, strerror(errno));
		}
		else
		{
			printf("worker(%d): proxy closed \n", id);
		}
	}
	clients[id].upstream_socket = 0;
	// end close proxy //

	// set client as usabe //
	clients[id].run = 0;

	return 0;
}

int next()
{
    fd_set fds;
    FD_ZERO(&fds);
	int a;
	int high;
	for (a = 0 ; a < MAXCLIENTS; a++)
	{
		if (clients[a].run != 0)
		{
		    FD_SET(clients[a].client_socket, &fds);
			if (clients[a].client_socket > high)
			{
				high = clients[a].client_socket;
			}

		    FD_SET(clients[a].upstream_socket, &fds);
			if (clients[a].upstream_socket > high)
			{
				high = clients[a].upstream_socket;
			}
		}
	}
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;

    int retval;
    retval = select(high+1, &fds, NULL, NULL, &tv);
	if (retval == 0)
	{
		return -1;
	}

	for (a = 0 ; a < MAXCLIENTS; a++)
	{
		if (clients[a].run != 0)
		{
			if (FD_ISSET(clients[a].client_socket, &fds))
			{
				return a;
			}
			if (FD_ISSET(clients[a].upstream_socket, &fds))
			{
				return a;
			}
		}
	}

	return -1;
}

void* worker()
{
	char buffer[BUF];
work:
	while (1)
	{
		//printf("worker: poll() \n");
		int id = next();
		if (id == -1)
		{
			//printf("worker: nothing to do \n");
		}
		else
		{
			//printf("worker(%d): something to do \n", id);

			// read from client //
			int recv_size;
			recv_size = recv(clients[id].client_socket, &buffer, BUF, MSG_DONTWAIT);
			if (recv_size == -1)
			{
				if (errno == EAGAIN)
				{
					//printf("worker(%d): client recv() EAGAIN\n", id);
				}
				else 
				{
					printf("worker(%d): client recv() failed: %d: %s \n", id, errno, strerror(errno));
					clear(id);
					goto work;
				}
			}
			else if (recv_size == 0)
			{
				clear(id);
				goto work;
			}
			else
			{
				// write to upstream //
				if (recv_size != -1)
				{
					size_t write_size;
					write_size = write(clients[id].upstream_socket, buffer, recv_size);
					if (write_size == -1)
					{
						printf("worker(%d): upstream write() failed: %d: %s \n", id, errno, strerror(errno));
						clear(id);
						goto work;
					}
					else
					{
						//printf("> %s", buffer);
					}
				}
				// write to upstream //
			}
			// end read from client //


			// read from upstream //
			recv_size = 0;
			recv_size = recv(clients[id].upstream_socket, &buffer, BUF, MSG_DONTWAIT);
			if (recv_size == -1)
			{
				if (errno == EAGAIN)
				{
					//printf("worker(%d): upstream recv() EAGAIN\n", id);
				}
				else 
				{
					printf("worker(%d): upstream recv() failed: %d: %s \n", id, errno, strerror(errno));
					clear(id);
					goto work;
				}
			}
			else if (recv_size == 0)
			{
				clear(id);
				goto work;
			}
			else
			{
				// write to client //
				if (recv_size != -1)
				{
					size_t write_size;
					write_size = write(clients[id].client_socket, buffer, recv_size);
					if (write_size == -1)
					{
						printf("worker(%d): client write() failed: %d: %s \n", id, errno, strerror(errno));
						clear(id);
						goto work;
					}
					else
					{
						//printf("< %s", buffer);
					}
				}
				// end write to client //
			}
			// end read from upstream //
		}
	}
}

void* server(int listenport)
{
	// server socket //
	int serv_socket;
	serv_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serv_socket == -1)
	{
		printf("server: socket() failed: %d: %s \n", errno, strerror(errno));
		exit(1);
	}
	else
	{
		printf("server: socket() success: %d \n", serv_socket);
	}
	// server socket //


	// server address //
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(listenport);
	// end server address //


	// bind server address & socket //
	int serv_bind;
	serv_bind = bind(serv_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	if (serv_bind == -1)
	{
		printf("server: bind() failed: %d: %s \n", errno, strerror(errno));
		exit(1);
	}
	else
	{
		printf("server: bind() %d success: %d \n", listenport, serv_bind);
	}
	// end bind server address & socket //

	// server start listening //
	int serv_listen;
	serv_listen = listen(serv_socket, BACKLOG);
	if (serv_bind == -1)
	{
		printf("server: listen() failed: %d: %s \n", errno, strerror(errno));
		exit(1);
	}
	else
	{
		printf("server: listen() success: %d \n", serv_listen);
	}
	// end server start listening //
	char* client_ip = calloc(64, 1);

	// server start accepting clients //
restart:
	while(1)
	{
		int id;
		id = find();
		if (id == -1)
		{
			printf("server: MAXCLIENTS reached: %d \n", MAXCLIENTS);
			sleep(1);
		}
		else
		{
			printf("server(%d): waiting for client \n", id);
			socklen_t cli_len;
			cli_len = sizeof(clients[id].client_addr);
			clients[id].client_socket = accept(serv_socket, (struct sockaddr *)&clients[id].client_addr, &cli_len);
			if (clients[id].client_socket == -1)
			{
				printf("server(%d): accept() failed: %d: %s \n", id, errno, strerror(errno));
				// reset the client struct so it can be conncted to again //
				clear(id);
				goto restart;
			}
			else
			{
				printf("server(%d): accept() success: %d \n", id, clients[id].client_socket);
				client_ip[0] = '\0';
				if (inet_ntop(AF_INET, &clients[id].client_addr.sin_addr, client_ip, cli_len) == NULL)
				{
					printf("server(%d): inet_ntop() failed: %d: %s \n", id, errno, strerror(errno));
					clear(id);
					goto restart;
				}
				else
				{
					int port = ntohs(clients[id].client_addr.sin_port);
					printf("server(%d): client connected: %s:%d \n", id, client_ip, port);

					// upstream socket //
					clients[id].upstream_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					if (clients[id].upstream_socket == -1)
					{
						printf("server(%d): upstream socket() failed: %d: %s \n", id, errno, strerror(errno));
						clear(id);
						goto restart;
					}
					else
					{
						printf("server(%d): upstream socket() success: %d \n", id, serv_socket);
					}
					// end upstream socket //

					// upstream address //
					//struct sockaddr_in upstream_addr;
					clients[id].upstream_addr.sin_family = AF_INET;
					clients[id].upstream_addr.sin_port = htons(80);
					int pton;
					pton = inet_pton(AF_INET, "192.168.1.100", &clients[id].upstream_addr.sin_addr);
					if (pton != 1)
					{
						printf("server(%d): upstream inet_pton() failed: %d: \n", id, pton);
						clear(id);
						goto restart;
					}
					else
					{
						printf("server(%d): inet_pton() success: %d \n", id, pton);
					}
					// end upstream address //


					// upstream connect //
					int conn;
					conn = connect(clients[id].upstream_socket, (struct sockaddr*)&clients[id].upstream_addr, sizeof(clients[id].upstream_addr));
					if (conn != 0)
					{
						printf("server(%d): upstream connect() failed: %d: %s \n", id, errno, strerror(errno));
						clear(id);
						goto restart;;
					}
					else
					{
						printf("server(%d): upstream connect() success: %d \n", id, clients[id].upstream_socket);
					}
					// end upstream connect //

					clients[id].run = 1;


				}
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	// start worker thread //
	pthread_t thread_id;
	int res;
	res = pthread_create(&thread_id, NULL, worker, NULL);
	if (res != 0)
	{
		printf("server: worker thread failed: %d: %s \n", errno, strerror(errno));
		exit(1);
	}
	else
	{
		printf("server: worker thread success: %d \n", res);
	}
	// end start worker thread //

	// run server forever //
	server(atoi(argv[1]));

	return 0;
}


























