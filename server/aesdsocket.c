#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>

#define PORT "9000"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold
#define MAXBUFLEN 100

#define DATA_FILE "/var/tmp/aesdsocketdata"

void handle_sigint(int sig) {
    // Note: It is only safe to call async-signal-safe functions here.
    // write() is safe; printf() is technically NOT safe inside a handler.
    const char msg[] = "\nCaught SIGINT (Ctrl+C). Exiting cleanly...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    unlink(DATA_FILE);
    syslog(LOG_INFO, "Caught signal,exiting");
    // Perform cleanup if necessary, then exit
    exit(0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char ** argv)
{
	// listen on sock_fd, new connection on new_fd
	int sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address info
	socklen_t sin_size;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
    char buf[MAXBUFLEN];
    int is_daemon = 0;

    if(argc >=2 && strcmp(argv[1], "-d") == 0 ) {
        is_daemon = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // set sigint handler
    struct sigaction sa;
    sa.sa_handler = &handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
      syslog(LOG_ERR, "Error registering SIGINT handler");
      return 1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
      syslog(LOG_ERR, "Error registering SIGINT handler");
      return 1;
    }

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
            syslog(LOG_ERR, "server: socket() failed %s", strerror(errno));
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
            syslog(LOG_ERR, "server: bind() failed %s", strerror(errno));
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "aesdsocket: failed to bind\n");
        syslog(LOG_ERR,  "aesdsocket: failed to bind()\n");
		exit(1);

	}

    unlink(DATA_FILE);

    if (is_daemon && daemon(0, 0) == -1) {
        syslog(LOG_ERR,  "aesdsocket: failed to daemonize\n");
        exit(EXIT_FAILURE);
    }

	printf("server: waiting for connections...\n");

        while (1) {
          if (listen(sockfd, BACKLOG) == -1) {
            syslog(LOG_ERR, "server: listen() failed %s", strerror(errno));
            exit(1);
          }

          sin_size = sizeof their_addr;
          new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size);
          if (new_fd == -1) {
            syslog(LOG_ERR, "server: accept() failed %s", strerror(errno));
            continue;
          }

          inet_ntop(their_addr.ss_family,
                    get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);
          printf("server: got connection from %s\n", s);
          syslog(LOG_INFO, "Accepted connection from %s", s);

          int numbytes;
          FILE * out = fopen(DATA_FILE, "a+t");
          if(!out) {
            syslog(LOG_ERR, "Cannot open file %s to write", DATA_FILE);
            exit(0);
          }

          while (1) {
            buf[0] = 0;
            if ((numbytes = recvfrom(new_fd, buf, MAXBUFLEN - 1, 0,
                                     (struct sockaddr*)&their_addr,
                                     &sin_size)) == -1) {
              syslog(LOG_ERR, "recvfrom() failed %s", strerror(errno));
              break;
            }
            buf[numbytes] = 0;;
            fprintf(out, "%s", buf);
            if(numbytes!=MAXBUFLEN - 1) {
                break;
            }
          }
          fclose(out);

          // send data to client
          FILE * in = fopen(DATA_FILE, "rb");
          if(!in) {
            syslog(LOG_ERR, "Cannot open file %s to read", DATA_FILE);
            exit(0);
          }

          while (1) {
            int n_read = fread(buf, 1, MAXBUFLEN, in);
            if(send(new_fd, buf, n_read, 0) == -1) {
               syslog(LOG_ERR, "send() failed %s", strerror(errno)); 
            }
            if (feof(in)) {
              fclose(in);
              break;
            }
          }

          close(new_fd);

        }

	return 0;
}

