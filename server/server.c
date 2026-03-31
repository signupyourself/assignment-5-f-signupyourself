#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/types.h>


#define BUF_SIZE 1024
#define PORT "9000"

const char* file_path = "/var/tmp/aesdsocketdata";

int client_socket, server_socket, fd;

void signal_handler(int signal){

    syslog(LOG_INFO,"Caught signal, exiting");
    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);
    close(server_socket);
    close(fd);
    unlink(file_path);
    exit(EXIT_SUCCESS);
}

void read_data(int socket, int fd);

int setup_server();

void create_daemon();

int setup_file(const char*);


int main(int argc, char *argv[])
{
    if(argc > 1){
        if(!strcmp(argv[1], "-d")){
            printf("Starting server as a daemon\n");
            create_daemon();
        }else{
            printf("Bad usage \n");
            exit(EXIT_FAILURE);
        }
    }

    sigset_t mask;
    sigemptyset (&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    struct sigaction si;
    memset(&si, 0, sizeof(si));
    si.sa_mask = mask;
    si.sa_handler = signal_handler;

    if(sigaction(SIGTERM, &si, NULL) == -1){
        return -1;
    }

    if(sigaction(SIGINT, &si, NULL) == -1){
        return -1;
    }

    fd = setup_file(file_path);
 
    server_socket = setup_server();

    int status = listen(server_socket, 1);

    if(status == -1){
        printf("Error listening : %s \n",gai_strerror(status));
        close(server_socket);
        return -1;
    }

    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_storage);

    while(1){

        client_socket = accept(
            server_socket,
            (struct sockaddr*)&client_addr,
            &client_addr_len
        );

        if(client_socket == -1){
            printf("Error accepting connection\n");
            return -1;
        }

        char client_addr_str[INET_ADDRSTRLEN]={0};
        
        inet_ntop(AF_INET, 
            &((struct sockaddr_in*)&client_addr)->sin_addr, 
            client_addr_str, client_addr_len
        );

        syslog(LOG_INFO, "Accepted connection from %s",client_addr_str);
        
        read_data(client_socket, fd);

	syslog(LOG_INFO, "Closed connection from %s", client_addr_str);

    }
   
    
}

int setup_server(){

    int server_socket, status;
    struct addrinfo hints, *result, *next;

    //setting hints
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_TCP;

    status = getaddrinfo(NULL, PORT, &hints, &result);

    if (status != 0) {
        printf("Getaddrinfo failed : %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    
    for (next = result; next != NULL; next = next->ai_next) {
        server_socket = socket(next->ai_family, next->ai_socktype,
                    next->ai_protocol);
        if (server_socket == -1)
            continue;
        int yes=1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (bind(server_socket, next->ai_addr, next->ai_addrlen) == 0)
            break;

        close(server_socket);
    }

    freeaddrinfo(result);

    if(next == NULL){
        printf("Error binding to the port \n");
        exit(EXIT_FAILURE);
    }

    return server_socket;
}

void read_data(int client_socket, int fd){

    char buffer[BUF_SIZE];
    int wbuffer_size=0, recv_bytes;
    char* wbuffer=NULL;

    do{
        recv_bytes = recv(client_socket, buffer, sizeof(buffer)-1, 0);
        if(recv_bytes > 0){
            buffer[recv_bytes]=0;
            if(buffer[recv_bytes-1] == '\n'){
                if(wbuffer_size == 0){
                    write(fd, buffer, recv_bytes);
                }else{                
                    write(fd, wbuffer, wbuffer_size);
                    write(fd, buffer, recv_bytes);
                    char sample_buf[1024];
                    free(wbuffer);
                    wbuffer = NULL;
                    wbuffer_size=0;
                }
                break;
            }else{
                if(!wbuffer){
                    wbuffer = malloc(recv_bytes);
                }else{
                    wbuffer = realloc(wbuffer, wbuffer_size+recv_bytes);
                }

                if(!wbuffer){
                    printf("Error allocating memory : %d \n", errno);
                    exit(EXIT_FAILURE);
                }
                strncpy(wbuffer+wbuffer_size, buffer, recv_bytes);
                wbuffer_size += recv_bytes;

            }
        }
    } while (recv_bytes > 0);

    if(wbuffer){
        free(wbuffer);
        wbuffer = NULL;
    }

    lseek(fd, 0, SEEK_SET);
    int len;
    char bf[1024] = {0};
    while((len=read(fd, bf, 1023))!=0){
        int sent_length = len;
        char* d_buffer = bf;
        do{
            bf[1024]=0;
            int sent_size = send(client_socket, d_buffer, len, 0);
            sent_length -= sent_size;
            d_buffer += sent_length;
        }while (sent_length > 0);  
        
    }
    
    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);
}

int setup_file(const char* file_path){

    int fd = open(file_path, O_RDWR|O_CREAT, 0666);
    
    if(fd == -1){
        printf("Error creating file \n : %d", errno);
        exit(EXIT_FAILURE);
    }

    return fd;

}

void create_daemon(){

    pid_t pid;
    pid = fork ();
    
    if(pid == -1){
        printf("Error creating daemon : %d\n", errno);
        exit(EXIT_FAILURE);
    }else if(pid != 0){
        exit(EXIT_SUCCESS);
    }

    //closing all file descriptors
    for(int i=0; i<NR_OPEN; i++){
        close(i);
    }

    //redirecting STDIN, STDOUT and STDERR to /dev/null
    open ("/dev/null", O_RDWR); 
    dup (0); 
    dup (0); 

}
