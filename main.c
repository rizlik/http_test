#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#define address "0.0.0.0"
#define port "80"

static int listen_fd;
static int epoll_fd;

static char buf[4096];
static char *payload = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n!";

static char *payload0 = "HTTP/1.1 200 OK\r\n";
static char *payload1 = "Content-Length: 1\r\n\r\n!";

static bool two_write;

static inline int fd_nonblock(int fd)
{
	return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static int new_connection()
{
	struct sockaddr_in caddr;
	socklen_t addr_size = sizeof(caddr);
	struct epoll_event evt;
	int nfd, r;

	nfd = accept(listen_fd, (struct sockaddr*)&caddr, &addr_size);
	if (nfd == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		printf("err accept:%d\n", -errno);
		/* XXX: propagate error in accept_cb */
		return -errno;
	}

	fd_nonblock(nfd);

	evt.events = EPOLLIN | EPOLLRDHUP;
	evt.data.ptr = (void *)(0UL | nfd);

	r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nfd, &evt);
	if (r == -1)
		printf("err epoll ctl: %d\n", -errno);

	return r;
}

static int process_event(struct epoll_event *evt)
{
	int fd;
	int r;

	fd = (int)evt->data.ptr;
	if (fd == listen_fd)
		return new_connection();

	if (evt->events & EPOLLRDHUP) {
		close(fd);
		return 0;
	}

	if (evt->events & EPOLLIN) {
		r = read(fd, buf, 4096);
		if (r == -1) {
			printf("err read %d\n", -errno);
		}

		if (two_write) {

			r = write(fd, payload0, strlen(payload0));
			if (r == -1 || r != (int)strlen(payload0)) {
				printf("err write %d/%lu\n",
				       r, strlen(payload0));
				return -errno;
			}

			r = write(fd, payload1, strlen(payload1));
			if (r == -1 || r != (int)strlen(payload1)) {
				printf("err write %d/%lu\n",
				       r, strlen(payload1));
				return -errno;
			}

		} else {

			r = write(fd, payload, strlen(payload));
			if (r == -1 || r != (int)strlen(payload)) {
				printf("err write %d/%lu\n",
				       r, strlen(payload1));
				return -errno;
			}

		}

		return 0;
	}

	printf("events? %d\n", evt->events);
	return 0;
}

int main(int argc, char *argv[])
{
	struct addrinfo *rp;
	struct epoll_event evt[10];
	int i, r;

	if (argc > 1) {
		printf("using two writes\n");
		two_write = true;
	}

	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE,
	};

	epoll_fd = epoll_create(1);
	if (epoll_fd == -1)
		return -errno;

	r = getaddrinfo(address, port, &hints, &rp);
	if (r != 0) {
		switch (r) {
		case EAI_MEMORY:
			return -ENOMEM;
		case EAI_NONAME:
			return -ENOENT;
		default:
			return -EINVAL;
		}
	}

	listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	if (listen_fd == -1)
		return -errno;

	if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) != 0)
		return -errno;

	if (listen(listen_fd, 511))
		return -errno;

	evt[0].events = EPOLLIN;
	evt[0].data.ptr = (void*)(0UL | listen_fd);
	r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &evt[0]);
	if (r == -1)
		return -errno;

	while (1) {
		r = epoll_wait(epoll_fd, evt, 10, 500);
		if (r == 0)
			continue;
		if (r == -1 && errno == EINTR)
			continue;

		for (i = 0; i < r; i++)
			process_event(&evt[i]);

	}


	return 0;
}
