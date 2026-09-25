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
#include <pthread.h>
#include <time.h>
#include <bits/types/timer_t.h>
#include <sys/queue.h>

#define PORT "9000"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold
#define MAXBUFLEN 100

pthread_mutex_t file_mutex;
#define DATA_FILE "/var/tmp/aesdsocketdata"

FILE * output_file = NULL;

typedef struct connection_data_s connection_data_t;
struct connection_data_s {
  int fd;
	struct sockaddr_storage their_addr; 
	socklen_t sin_size;
};

// SLIST.
typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    pthread_t thread_id; 
    SLIST_ENTRY(slist_data_s) entries;
};

SLIST_HEAD(slisthead, slist_data_s) g_head;
// listen on sock_fd, new connection on new_fd
int sockfd;
int is_exit = 0;

void handle_sigint(int sig) {
    // Note: It is only safe to call async-signal-safe functions here.
    // write() is safe; printf() is technically NOT safe inside a handler.
    const char msg[] = "\nCaught SIGINT (Ctrl+C). Exiting cleanly...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    syslog(LOG_INFO, "Caught signal,exiting");
    // Perform cleanup if necessary, then exit
    is_exit = 1;
    close(sockfd);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void* process_connection(void* data) {
  connection_data_t* connection = (connection_data_t*)data;
  char s[INET6_ADDRSTRLEN];
  char buf[MAXBUFLEN];

  inet_ntop(connection->their_addr.ss_family,
            get_in_addr((struct sockaddr*)&connection->their_addr), s,
            sizeof s);
  printf("server: got connection from %s\n", s);
  syslog(LOG_INFO, "Accepted connection from %s", s);

  int numbytes;

  pthread_mutex_lock(&file_mutex);

  while (1) {
    buf[0] = 0;
    if ((numbytes = recvfrom(connection->fd, buf, MAXBUFLEN - 1, 0,
                             (struct sockaddr*)&connection->their_addr, &connection->sin_size)) == -1) {
      syslog(LOG_ERR, "recvfrom() failed %s", strerror(errno));
      break;
    }
    buf[numbytes] = 0;
    fprintf(output_file, "%s", buf);
    if (numbytes != MAXBUFLEN - 1) {
      break;
    }
  }

  fflush(output_file);
  // send data to client
  FILE* in = fopen(DATA_FILE, "rb");
  if (!in) {
    syslog(LOG_ERR, "Cannot open file %s to read", DATA_FILE);
    pthread_mutex_lock(&file_mutex);
    return NULL;
  }

  while (1) {
    int n_read = fread(buf, 1, MAXBUFLEN, in);
    if (send(connection->fd, buf, n_read, 0) == -1) {
      syslog(LOG_ERR, "send() failed %s", strerror(errno));
    }
    if (feof(in)) {
      fclose(in);
      break;
    }
  }
  pthread_mutex_unlock(&file_mutex);
  close(connection->fd);
  free(connection);
  return NULL;
}

// The callback function that runs when the timer expires
void timer_callback(union sigval sv) {
    char time_str[200];
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", tmp);
    syslog(LOG_INFO, "timestamp = %s, output file=%p", time_str, output_file);
    if(output_file) {
      pthread_mutex_lock(&file_mutex);
      fprintf(output_file, "timestamp:%s\n", time_str);
      fflush(output_file);
      pthread_mutex_unlock(&file_mutex);
    }
}

 timer_t timerid;

int set_timer() {
    struct sigevent sev;
    struct itimerspec its;

    // 1. Configure the sigevent structure to use a thread callback
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_callback;
    sev.sigev_notify_attributes = NULL;
    sev.sigev_value.sival_ptr = (void *)"Hello from the timer!"; // Data passed to callback

    // 2. Create the POSIX timer
    // We use CLOCK_MONOTONIC so the timer is immune to system time changes
    if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
        syslog(LOG_ERR, "timer_create failed %s", strerror(errno));
        return 1;
    }

    // 3. Configure the timer intervals
    // it_value: Time until the very first expiration (2 seconds)
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 10;
    
    // it_interval: Period for consecutive expirations (every 1 second)
    its.it_interval.tv_sec = 10;
    its.it_interval.tv_nsec = 0;

    // 4. Arm (start) the timer
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime failed");
        syslog(LOG_ERR, "timer_settime failed %s", strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char ** argv)
{
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address info
	int yes=1;
	int rv;
  SLIST_INIT(&g_head);

    int is_daemon = 0;

    if(argc >=2 && strcmp(argv[1], "-d") == 0 ) {
        is_daemon = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    unlink(DATA_FILE);
    output_file = fopen(DATA_FILE, "wt");
    if (!output_file) {
      syslog(LOG_ERR, "Cannot open file %s to write", DATA_FILE);
      exit(0);
    }

   if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "File mutex initialization failed");
        return 1;
   }

   if (is_daemon && daemon(0, 0) == -1) {
        syslog(LOG_ERR,  "aesdsocket: failed to daemonize\n");
        exit(EXIT_FAILURE);
    }

    if(set_timer()) {
      syslog(LOG_ERR, "Timer initialization failed");
      fclose(output_file);
    }

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
      syslog(LOG_ERR, "server: setsockopt() failed %s", strerror(errno));
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

	printf("server: waiting for connections...\n");

        while (1) {
          if (listen(sockfd, BACKLOG) == -1) {
            if (!is_exit) {
              syslog(LOG_ERR, "server: listen() failed %s", strerror(errno));
            }
            break;
          }

          connection_data_t * connection_data = (connection_data_t *)malloc(sizeof(connection_data_t));
          if(!connection_data) {
            syslog(LOG_ERR, "Connection data allocation failed failed %s", strerror(errno));
            exit(1);
          }
	
          connection_data->sin_size = sizeof their_addr;
          connection_data->fd = accept(sockfd, (struct sockaddr*)&connection_data->their_addr, &connection_data->sin_size);
          if (connection_data->fd == -1) {
            if (!is_exit) {
              syslog(LOG_ERR, "server: accept() failed %s", strerror(errno));
            }
            free(connection_data);
            continue;
          }

          pthread_t thread_id; 

          if (pthread_create(&thread_id, NULL,
                             process_connection, (void*)connection_data) != 0) {
            syslog(LOG_ERR, "Failed to create thread");
            exit(1);
          }

          // insert thread_id in the list
          slist_data_t *datap=NULL;
          datap = malloc(sizeof(slist_data_t));
          datap->thread_id = thread_id;
          SLIST_INSERT_HEAD(&g_head, datap, entries);
        }

        syslog(LOG_INFO, "Stopped main processing loop");

        // join threads
        int n_threads = 0;
        while (!SLIST_EMPTY(&g_head)) {
          slist_data_t* datap = NULL;
          datap = SLIST_FIRST(&g_head);
          SLIST_REMOVE_HEAD(&g_head, entries);
          pthread_join(datap->thread_id, NULL);
          ++n_threads;
          free(datap);
        }

        syslog(LOG_INFO, "Joined threads %d", n_threads);

        // remove file
       fclose(output_file);
       unlink(DATA_FILE);
       timer_delete(timerid);
        syslog(LOG_INFO, "socket server terminated");
        return 0;
}

