#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    usleep(thread_func_args->wait_to_obtain_ms);
    
    int lock = pthread_mutex_lock(thread_func_args->mutex);
    if(lock != 0){
    	thread_func_args->thread_complete_success = false;
    	printf("Mutex Lock Failed\n");
    }
    else{
    	usleep(thread_func_args->wait_to_release_ms);
    	int unlock = pthread_mutex_unlock(thread_func_args->mutex);
    	if(unlock != 0){
    	    thread_func_args->thread_complete_success = false;
    	    printf("Mutex Release Failed\n");
    	} 
    }
    thread_func_args->thread_complete_success = true;    
    
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *thread_struct = malloc(sizeof(struct thread_data));
    memset(thread_struct,0,sizeof(struct thread_data));
    thread_struct->mutex = mutex;
    thread_struct->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_struct->wait_to_release_ms = wait_to_release_ms;
    
    int rc = pthread_create(thread,NULL,threadfunc,(void *)thread_struct);
    
    if(rc != 0){
    	printf("Unable to create the thread successfully");
    	thread_struct->thread_complete_success = false;
    }
    else{
    	thread_struct->thread_complete_success = true;
    }
    
    return thread_struct->thread_complete_success;    
     
}

