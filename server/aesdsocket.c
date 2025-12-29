#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <sys/queue.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define RCV_FILE "/dev/aesdchar"
#else
#define RCV_FILE "/var/tmp/aesdsocketdata"
#endif
#define MAX_BUF 1024*16
#define TIMESTAMP_INTERVAL 10

// Define the list entry structure
struct thread_entry {
    pthread_t thread;
    SLIST_ENTRY(thread_entry) entries;
};

// Define the list head
SLIST_HEAD(thread_list, thread_entry) head = SLIST_HEAD_INITIALIZER(head);

// Global variables
int fd;
bool break_f = false;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
#if !USE_AESD_CHAR_DEVICE
timer_t timerid;
#endif

// Thread data structure
struct thread_data {
    int rcvfd;
    struct sockaddr_in rcvaddr;
};

// Function prototypes
void add_thread_to_list(pthread_t thread);
void cleanup_threads(void);
void *connection_handler(void *thread_arg);
#if !USE_AESD_CHAR_DEVICE
void write_timestamp(void);
#endif

#if !USE_AESD_CHAR_DEVICE
static void timer_handler(union sigval sigval) {
    write_timestamp();
}

void write_timestamp(void) {
    time_t now;
    char timestamp[100];
    int file_fd;
    time(&now);
    strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", localtime(&now));
    
    pthread_mutex_lock(&file_mutex);
    file_fd = open(RCV_FILE, O_WRONLY|O_CREAT|O_APPEND, 0666);
    if (file_fd != -1) {
        write(file_fd, timestamp, strlen(timestamp));
        close(file_fd);
    }
    pthread_mutex_unlock(&file_mutex);
}
#endif

static void signal_handler(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        break_f = true;
        
#if !USE_AESD_CHAR_DEVICE
        // Stop timer
        timer_delete(timerid);
#endif
        
        // Cleanup threads
        cleanup_threads();
        
        // Cleanup files and sockets
#if !USE_AESD_CHAR_DEVICE
        remove(RCV_FILE);
#endif
        close(fd);
    }
}

void *connection_handler(void *thread_arg) {
    struct thread_data *data = (struct thread_data *)thread_arg;
    char *buf = malloc(MAX_BUF * sizeof(char));
    char *snd_buf = malloc(MAX_BUF * sizeof(char));
    char *client_addr = malloc(100 * sizeof(char));
    
    inet_ntop(AF_INET, &(data->rcvaddr.sin_addr), client_addr, 100);
    syslog(LOG_DEBUG, "Accepted connection from %s", client_addr);
    
    ssize_t recvd_size = 0;
    char *complete_msg = NULL;
    size_t complete_size = 0;
    
    // Read until we get a complete message (ending in newline)
    while ((recvd_size = recv(data->rcvfd, buf, MAX_BUF - 1, 0)) > 0) {
        buf[recvd_size] = '\0';
        
        // Append received data to complete message
        char *new_complete = realloc(complete_msg, complete_size + recvd_size);
        if (new_complete) {
            complete_msg = new_complete;
            memcpy(complete_msg + complete_size, buf, recvd_size);
            complete_size += recvd_size;
        }
        
        // Check if we have a complete message
        if (memchr(buf, '\n', recvd_size)) {
            int file_fd;
            // Lock the mutex before writing the complete message
            pthread_mutex_lock(&file_mutex);
            
            // Write the complete message
#if USE_AESD_CHAR_DEVICE
            file_fd = open(RCV_FILE, O_WRONLY);
#else
            file_fd = open(RCV_FILE, O_WRONLY|O_CREAT|O_APPEND, 0666);
#endif
            if (file_fd != -1) {
                write(file_fd, complete_msg, complete_size);
                close(file_fd);
                
                // Read and send back all data
                file_fd = open(RCV_FILE, O_RDONLY);
                if (file_fd != -1) {
#if USE_AESD_CHAR_DEVICE
                    // For char device, read until EOF
                    ssize_t read_size;
                    while ((read_size = read(file_fd, snd_buf, MAX_BUF - 1)) > 0) {
                        send(data->rcvfd, snd_buf, read_size, 0);
                    }
#else
                    ssize_t total_size = lseek(file_fd, 0, SEEK_END);
                    lseek(file_fd, 0, SEEK_SET);
                    
                    while (total_size > 0) {
                        ssize_t read_size = read(file_fd, snd_buf, MAX_BUF - 1);
                        if (read_size > 0) {
                            send(data->rcvfd, snd_buf, read_size, 0);
                            total_size -= read_size;
                        }
                    }
#endif
                    close(file_fd);
                }
            }
            
            pthread_mutex_unlock(&file_mutex);
            
            // Reset the complete message buffer
            free(complete_msg);
            complete_msg = NULL;
            complete_size = 0;
        }
    }
    
    // Cleanup
    free(complete_msg);
    shutdown(data->rcvfd, SHUT_RDWR);
    close(data->rcvfd);
    syslog(LOG_DEBUG, "Closed connection from %s", client_addr);
    
    free(buf);
    free(snd_buf);
    free(client_addr);
    free(data);
    return NULL;
}

int main(int argc, const char *argv[]) {
	openlog(NULL,0,LOG_USER);
	
	struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler=signal_handler;
    
    if(sigaction(SIGINT, &new_action, NULL)) 
    	printf("Error %d (%s) registering for SIGINT",errno,strerror(errno));
	if( sigaction(SIGTERM, &new_action, NULL) != 0 )
        printf("Error %d (%s) registering for SIGTERM",errno,strerror(errno));
        
        
	fd = socket(PF_INET, SOCK_STREAM, 0);
	
	if (fd != -1){
	
		int status;
		struct addrinfo hints;
		struct addrinfo *servinfo;
		
		u_int yes = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
				
		if( (status = getaddrinfo(NULL, "9000", &hints, &servinfo)) !=0) {
			printf("getaddrinfo error: %s\n", gai_strerror(status));
			exit(1);
		}
                     
		//binding
		if(bind(fd, servinfo->ai_addr, sizeof(struct sockaddr)) == 0){
			
			if(argc == 2 && strcmp(argv[1],"-d") == 0) 
				daemon(0, 0);
			
			//start listining
			if(listen(fd, 5) == 0){
				
#if !USE_AESD_CHAR_DEVICE
				remove(RCV_FILE);
				
				// Setup timer
				struct sigevent sev;
				struct itimerspec its;
				
				sev.sigev_notify = SIGEV_THREAD;
				sev.sigev_notify_function = timer_handler;
				sev.sigev_notify_attributes = NULL;
				sev.sigev_value.sival_ptr = NULL;
				
				timer_create(CLOCK_REALTIME, &sev, &timerid);
				
				its.it_value.tv_sec = TIMESTAMP_INTERVAL;
				its.it_value.tv_nsec = 0;
				its.it_interval.tv_sec = TIMESTAMP_INTERVAL;
				its.it_interval.tv_nsec = 0;
				
				timer_settime(timerid, 0, &its, NULL);
#endif
				
				while(!break_f){
					
					printf("Listening...\n");				
					
					struct sockaddr_in rcvaddr;
					memset(&rcvaddr, 0, sizeof(rcvaddr));
					socklen_t addr_len = sizeof(rcvaddr);				
					
					//accept new client
					int rcvfd = accept(fd, (struct sockaddr*)&rcvaddr, &addr_len);
					
					if(rcvfd != -1) {	 				
						struct thread_data *data = malloc(sizeof(struct thread_data));
						data->rcvfd = rcvfd;
						data->rcvaddr = rcvaddr;
						
						pthread_t thread;
						pthread_create(&thread, NULL, connection_handler, data);
						add_thread_to_list(thread);
						
						printf("Accepted connection from: %s\n", inet_ntoa(rcvaddr.sin_addr));
						syslog(LOG_DEBUG, "Accepted connection from %s", inet_ntoa(rcvaddr.sin_addr));	
					}
					else perror("accept()");															
				}
			}
			else perror("listen()");		
		}
		else perror("bind()");		
		
		//free servinfo memory
		freeaddrinfo(servinfo);

		//close socket
        close(fd);        		
	}
	else perror("socket()");
	
	return 0;
}

void add_thread_to_list(pthread_t thread) {
    struct thread_entry *entry = malloc(sizeof(struct thread_entry));
    entry->thread = thread;
    
    pthread_mutex_lock(&list_mutex);
    SLIST_INSERT_HEAD(&head, entry, entries);
    pthread_mutex_unlock(&list_mutex);
}

void cleanup_threads(void) {
    pthread_mutex_lock(&list_mutex);
    struct thread_entry *entry;
    while (!SLIST_EMPTY(&head)) {
        entry = SLIST_FIRST(&head);
        pthread_join(entry->thread, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(entry);
    }
    pthread_mutex_unlock(&list_mutex);
}