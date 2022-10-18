#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, const char *argv[]) {	
				
	int fd;
	ssize_t written;
	const char *filename;
	const char *writestr;	

	openlog(NULL,0,LOG_USER);

	if(argc != 3){
		syslog(LOG_ERR, "Invalid number of arguments: %d",argc-1);
		return 1;
	}	
	else {
		filename= argv[1];
		writestr= argv[2];	
	}
	
	syslog(LOG_DEBUG, "Writing %s to %s", writestr, filename);

	fd = open (filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
	if (fd == -1){
		syslog(LOG_ERR, "error in opening file, %s",strerror(errno));
		return 1;
	}
	
 	written = write (fd, writestr, strlen(writestr));
 	write (fd, "\n", 1);
 	
 	if (written == -1) {
	 	close(fd);
	 	return 1;
	 }
	 
	 close(fd);	

	return 0;
}
