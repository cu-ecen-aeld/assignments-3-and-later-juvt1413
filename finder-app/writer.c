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

	if(argc != 3){
		printf("Arguments error\n");
		return 1;
	}	
	else {
		filename= argv[1];
		writestr= argv[2];	
	}

	fd = open (filename, O_RDWR|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);

	if (fd == -1){
		printf("file open error: %s\n",strerror(errno));
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
