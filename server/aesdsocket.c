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

#define RCV_FILE "/var/tmp/aesdsocketdata"
#define MAX_BUF 1024

int fd, rcvfd, file_fd;
bool break_f = false;
char *buf, *snd_buf, *client_addr;

static void signal_handler ( int signal_number )
{
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        printf("\nCaught signal\n");
        syslog(LOG_DEBUG, "Caught signal, exiting");
        
        //close opend file
        close(file_fd);	
        remove(RCV_FILE);        
        
        //close all socket
        shutdown(rcvfd, SHUT_RDWR);
        close(rcvfd);
        close(fd);
        break_f = true;	    
    }
}


int main(int argc, const char *argv[])
{
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
				
				remove(RCV_FILE);
				
				while(!break_f){
					
					printf("Listening...\n");				
					
					struct sockaddr_in rcvaddr;
					memset(&rcvaddr, 0, sizeof(rcvaddr));
					socklen_t addr_len = sizeof(rcvaddr);				
					
					//accept new client
					rcvfd = accept(fd, (struct sockaddr*)&rcvaddr, &addr_len);
					
					if(rcvfd != -1) {	 				
						if((buf = malloc(1000*sizeof(char))) == NULL) printf("ERROR: malloc return NULL");				
						if((client_addr = malloc(100*sizeof(char))) == NULL) printf("ERROR: malloc return NULL");										
						ssize_t recvd_size=0, byts_to_send=0;
						
						inet_ntop(AF_INET, &(rcvaddr.sin_addr), client_addr, 100);					
						printf("Accepted connection from: %s\n", client_addr);
						syslog(LOG_DEBUG, "Accepted connection from %s", client_addr);	
						
						if((snd_buf = malloc(MAX_BUF*sizeof(char))) == NULL) printf("ERROR: malloc return NULL");
						 
						do{
							recvd_size = recv(rcvfd, buf, sizeof(buf), 0);
							if (recvd_size == -1) perror("recv()");
							else if(recvd_size != 0){
								//printf("\nrecvd_size = %ld\n", recvd_size);
								//write buf to file
								file_fd = open (RCV_FILE, O_WRONLY|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);
								if (file_fd == -1) printf("file open error\n");	
								write(file_fd, buf, recvd_size);
								close(file_fd);							
								//check for new line						
								for(int i=0; i<recvd_size; i++){								
									if(*(buf+i) == '\n') {
										//printf("\nnew line detected\n");
										//calculate file size								
										file_fd = open (RCV_FILE, O_RDONLY);
										byts_to_send = lseek(file_fd, 0, SEEK_END);
										close(file_fd);
										//printf("\nbytes to send: %ld\n", byts_to_send);							
										//open the file to read								
										file_fd = open (RCV_FILE, O_RDONLY);
										int red_bytes=MAX_BUF;
										while(red_bytes == MAX_BUF){
											red_bytes = read(file_fd, snd_buf, MAX_BUF);											
											//send file to client
											int snt_byts = send(rcvfd, snd_buf, red_bytes, 0);
											if(snt_byts ==-1) perror("send()");
										}
										close(file_fd); 					 	
									}
								}							
							}						
						}
						while(recvd_size != 0 && recvd_size != -1);					
						close(file_fd);											
						
						shutdown(rcvfd, SHUT_RDWR);
						close(rcvfd);
						printf("Closed connection from: %s\n", client_addr);
						syslog(LOG_DEBUG, "Closed connection from %s", client_addr);
						
						free(client_addr);
						free(buf);
						free(snd_buf);						
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





