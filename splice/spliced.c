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
#include <fcntl.h>

#define BUF 16384
#define BACKLOG 100
#define MAXCLIENTS 100

int listen_port;
char* upstream_ip;
int upstream_port;
int mode;

struct cli_struct
{
	int run;
	int client_socket;
	struct sockaddr_in client_addr;
	int upstream_socket;
	struct sockaddr_in upstream_addr;

	int client_active;
	int upstream_active;

	int pipes[2];
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

fd_set fds;
int high;
int build(int id)
{
	FD_ZERO(&fds);
	high = 0;

	int a;
	for (a = 0 ; a < MAXCLIENTS; a++)
	{
		if ((clients[a].run != 0) | (a == id))
		{
			//printf("build(): %d %d %d \n", a, clients[a].client_socket, clients[a].upstream_socket);
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
	//printf("build() high: %d \n", high);
	return 0;
}

int next()
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;

build(-1);

	//printf("next() high: %d \n", high);

    int retval;
    retval = select(high+1, &fds, NULL, NULL, &tv);
	if (retval == 0)
	{
		return -1;
	}

	int a;
	for (a = 0 ; a < MAXCLIENTS; a++)
	{
		if (clients[a].run != 0)
		{
			clients[a].client_active = 0;
			if (FD_ISSET(clients[a].client_socket, &fds))
			{
				clients[a].client_active = 1;
			}

			clients[a].upstream_active = 0;
			if (FD_ISSET(clients[a].upstream_socket, &fds))
			{
				clients[a].upstream_active = 1;
			}
		}
	}

	return 0;
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

	close(clients[id].pipes[0]);
	close(clients[id].pipes[1]);

	// set client as usabe //
	clients[id].client_active = 0;
	clients[id].upstream_active = 0;
	clients[id].run = 0;

	//build(-1);

	return 0;
}


void* worker()
{
	char buffer[BUF];
	int recv_size;
	size_t write_size;
	int splice_read;
	int splice_write;
	while (1)
	{
		int go = next();
		if (go == -1)
		{
			//printf("worker: nothing to do %d \n", go);
		}
		else
		{
			// start working //
			//printf("worker: something to do %d \n", go);
			int id;
			for (id = 0 ; id < MAXCLIENTS ; id++)
			{
				// read from client //
				if (clients[id].client_active == 1)
				{
					printf("worker(%d): client active \n", id);
					recv_size = 0;
					recv_size = recv(clients[id].client_socket, &buffer, BUF, MSG_DONTWAIT);
					if (recv_size == -1)
					{
						if (errno == EAGAIN)
						{
							printf("worker(%d): client recv() EAGAIN\n", id);
						}
						else 
						{
							printf("worker(%d): client recv() failed: %d: %s \n", id, errno, strerror(errno));
							clear(id);
							continue;
						}
					}
					else if (recv_size == 0)
					{
						clear(id);
						continue;
					}
					else
					{
						// write to upstream //
						if (recv_size != -1)
						{
							write_size = 0;
							write_size = write(clients[id].upstream_socket, buffer, recv_size);
							if (write_size == -1)
							{
								printf("worker(%d): upstream write() failed: %d: %s \n", id, errno, strerror(errno));
								clear(id);
								continue;
							}
							else
							{
								printf("> %.*s", recv_size, buffer);
							}
						}
						// write to upstream //
					}
					// end read from client //
				}
				if (clients[id].upstream_active == 1)
				{
				// read from upstream //
					//printf("worker(%d): upstream active \n", id);
					if (mode == 0)
					{
					// splice mode //
						splice_read = 0;
						splice_read = splice(clients[id].upstream_socket, NULL, clients[id].pipes[1], NULL, BUF, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
						if (splice_read == -1)
						{
								printf("worker(%d): upstream splice() failed: %d: %s \n", id, errno, strerror(errno));
								clear(id);
								continue;
						}
						if (splice_read == 0)
						{
								printf("worker(%d): nothing to splice() \n", id);
								clear(id);
								continue;
						}
						else
						{
						// splice to client //
							//printf("worker(%d): splice() %d \n", id, splice_read);
							splice_write = 0;
							splice_write = splice(clients[id].pipes[0], NULL, clients[id].client_socket, NULL, BUF, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
							if (splice_write == -1)
							{
									printf("worker(%d): client splice() failed: %d: %s \n", id, errno, strerror(errno));
									clear(id);
									continue;
							}
							else
							{
									//printf("worker(%d): client splice() write: %d \n", id, splice_write);
							}
						// end splice to client //
						}
					// end splice mode //
					}
					else
					{
					// buffer mode //
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
								continue;
							}
						}
						else if (recv_size == 0)
						{
							clear(id);
							continue;
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
									continue;
								}
								else
								{
									//printf("< %s", buffer);
								}
							}
						// end write to client //
						}
					// end buffer mode //
					}
				// end read from upstream //
				}
			}
		}
	}
}

void* server()
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
	serv_addr.sin_port = htons(listen_port);
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
		printf("server: bind() %d success: %d \n", listen_port, serv_bind);
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
					clients[id].upstream_addr.sin_family = AF_INET;
					clients[id].upstream_addr.sin_port = htons(upstream_port);
					int pton;
					pton = inet_pton(AF_INET, upstream_ip, &clients[id].upstream_addr.sin_addr);
					if (pton != 1)
					{
						printf("server(%d): upstream inet_pton() failed: %d: \n", id, pton);
						clear(id);
						goto restart;
					}
					else
					{
						printf("server(%d): inet_pton() success: %d %s \n", id, pton, upstream_ip);
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

						int createpipe;
						createpipe = pipe(clients[id].pipes);
						if (createpipe != 0)
						{
							printf("server(%d): pipe() failed: %d: %s \n", id, errno, strerror(errno));
							clear(id);
							goto restart;;
						}
						else
						{

							// rebuild fd_set //
							//build(id);

							// allow worker to work on this connection //
							clients[id].run = 1;
						}
					}
					// end upstream connect //
				}
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 5)
	{
		printf("Usage: spliced listen_port upstream_ip upstream_port splice/buffer\n");
		printf("Example: ./spliced 80 192.168.1.100 80 splice\n");
		printf("Exmaple: ./spliced 80 192.168.1.100 80 buffer\n");
		return 1;
	}

	listen_port = atoi(argv[1]);
	if (listen_port == 0)
	{
		printf("Invalid listen_port \n");
		return 1;
	}
	upstream_ip = argv[2];
	upstream_port = atoi(argv[3]);
	if (upstream_port == 0)
	{
		printf("Invalid upstream_port \n");
		return 1;
	}

	if (strcmp(argv[4], "splice") == 0)
	{
		mode = 0;
	}
	else if (strcmp(argv[4], "buffer") == 0)
	{
		mode = 1;
	}
	else
	{
		printf("Invalid mode: must be 'splice' or 'buffer' \n");
		return 1;
	}

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
	server();

	return 0;
}


























