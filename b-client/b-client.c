#include <unistd.h>
#include <stdint.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "../b-server/b-protocol.h"
#include "../b-server/b-tools.h"

#define MAX_CONNECTIONS 50
#define BUFFER_SIZE (1024 * 64L)

message_t *get_message(request_t *request)
{

	size_t size;
	read(request->fd, &size, sizeof(uint64_t));

	hash_t hash;
	read(request->fd, &hash, sizeof(hash_t));

	size_t buffer_size = size > BUFFER_SIZE ? BUFFER_SIZE : size;

	message_t *message = create_message(NULL, 0);
	char *chunk = calloc(buffer_size, sizeof(char));

	// expected message hash
	message->hash = hash;

	while (1)
	{
		int received = pass(read(request->fd, chunk, buffer_size), ERROR_READ);

		message->bytes = realloc(message->bytes, message->size + received);
		memcpy(&message->bytes[message->size], chunk, received);
		message->size += received;

		// EOF
		if (received == 0)
			break;
		//if (received < buffer_size)
		//	break;
	}

	free(chunk);
	return message;
}

void *client(void *args)
{
	request_t *request = (request_t *)args;
	message_t *message;
	//time_t start, end;
	char buffer[25];
	int *error = malloc(sizeof(int));

	request_message_t *request_message = create_request_message(request);
	if (write(request->fd, request_message, sizeof(request_message_t)) == -1)
	{
		printf("Error writing to socket %d\n", request->fd);
		*error = 1;
		pthread_exit(error);
	}

	//double elapsed = 0.0;
	//start = clock();
	//time(&start);
	ElapsedStart();
	message = get_message(request);
	ElapsedEnd();
	//end = clock();
	//time(&end);

	// check_message_hash(message);

	printf("[%2d:%3d] %s RCVD: Message %s in %.4fs - %.2f Mb/s\n",
				 request->fd,
				 request->serial,
				 message->hash == 0 ? "FAIL" : "-OK-",
				 bytes_to_human(message->size, buffer),
				 ELAPSED_SEC,
				 SPEED_MBS(message->size));

	*error = message->hash == 0 ? 1 : 0;

	delete_message(message);
	close(request->fd);
	delete_request(request);
	delete_request_message(request_message);

	pthread_exit(error);
}

int connect_server(char *server, int port)
{

	int sockfd;
	struct sockaddr_in servaddr;

	// socket create and varification
	sockfd = pass(socket(AF_INET, SOCK_STREAM, 0), ERROR_SOCKET);

	// assign IP, PORT
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(server);
	servaddr.sin_port = htons(port);

	// connect the client socket to server socket
	pass(connect(sockfd,
							 (struct sockaddr *)&servaddr,
							 sizeof(servaddr)),
			 ERROR_CONNECTION);

	return sockfd;
}

int main(int argc, char **argv)
{
	int opt;
	int port = DEFAULT_PORT;
	char *server = DEFAULT_SERVER;
	int n_requests = 0;
	char *command = NULL;
	pthread_t threads[MAX_CONNECTIONS];
	int n_threads = 0;
	//time_t start, end;

	while ((opt = getopt(argc, argv, "s:p:f:c:")) != -1)
	{
		switch (opt)
		{
		case 's':
			server = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'f':
			n_requests = atoi(optarg);
			break;
		case 'c':
			command = optarg;
			break;
		default:
			if (opt == 's')
				fprintf(stderr, "Option -%c requires an argument.\n", opt);
			else if (isprint(opt))
				fprintf(stderr, "Unknown option `-%c'.\n", opt);
			exit(0);
		}
	}

	if (!server)
	{
		printf("Usage: %s -s <server> [-p <port>] -c <size_in_kb> -f <n_requests>\n", argv[0]);
		exit(0);
	}

	printf("n_requests: %d\n", n_requests);

	int errors = 0;
	size_t size = atoi(command) * 1024;
	ElapsedStart();

	for (int i = 0; i < n_requests; i++)
	{
		int connfd = connect_server(server, port);

		request_t *request = create_request(connfd, i, size, 0);

		if (pthread_create(&threads[n_threads++], NULL, client, (void *)request) < 0)
		{
			perror("could not create thread");
			exit(1);
		}

		if (n_threads > MAX_CONNECTIONS)
		{
			printf("Maximum connections reached. (%d)\n", MAX_CONNECTIONS);
			errors += wait_threads_end(threads, MAX_CONNECTIONS);
			n_threads = 0;
		}
	}

	errors += wait_threads_end(threads, n_threads);

	ElapsedEnd();

	char buffer[25];
	printf("Elapsed time %2fs - %d errors - %s received\n",
				 ELAPSED_SEC,
				 errors,
				 bytes_to_human(size * n_requests, buffer));

	printf("Speed %.2f Mb/s\n", SPEED_MBS(size * n_requests));

	return 0;
}
