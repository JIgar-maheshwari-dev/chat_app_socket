#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/select.h>
#include <poll.h>
#include "server_mgmt.h"
#include "server_queue.h"


/* GLOBAL VARIABLES */
int server_fd = INVALID_FD;
struct sockaddr_in address;
int addrlen = sizeof(address);
pthread_mutex_t client_data_mutex;
bool server_terminate = false;
/**************************/

/* FUNCTIONS DECLARATIONS */
void* client_thread(void* arg);
void handle_rx_msg(msg_t msg,int fd);
srv_err_type wait_to_recv_something(int fd,msg_t* rx_msg);
srv_err_type send_msg_to_fd(int fd,msg_t send_msg);
void handle_chat_connection_request(int fd,char* conn_client_name);
void handle_conn_accept(int fd, msg_t msg);
void handle_tx_msg(int fd, msg_t msg);
void handle_decline_conn_request(int fd);
void handle_change_conn_fd_req(int fd, msg_t msg);

/**************************/

void sig_int_handler(int sig)
{
    if(SIGINT==sig)
    {
        printf("Server termination signal by user.\n");
        server_terminate = true;
    }
}

void print_bin_info(void)
{
    printf("*******************************\n");
    printf("Owner             : %s \n",OWNER);
    printf("Server build date : %s \n",BUILD_DATE );
    printf("Server build time : %s \n",BUILD_TIME );
    printf("*******************************\n\n");
}

srv_err_type init_srv(void)
{
    print_bin_info();
    int ret=-1;
    struct sigaction sa;
    sa.sa_handler = sig_int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // <- do NOT set SA_RESTART
    sigaction(SIGINT, &sa, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_FD==server_fd){
        printf("[ socket ] failed, err : %s, err_no : %d\n",strerror(errno),errno);
        return ERR_LIB_INIT;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    // to remove re-use error
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ret=bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    if(-1==ret){
        printf("[ bind ] failed, err : %s, err_no : %d\n",strerror(errno),errno);
        return ERR_LIB_INIT;
    }

    ret=listen(server_fd, MAX_LISTEN);
    if(-1==ret){
        printf("[ listen ] failed, err : %s, err_no : %d\n",strerror(errno),errno);
        return ERR_LIB_INIT;
    }

    if (pthread_mutex_init(&client_data_mutex, NULL) != 0) {
        printf("[ server ] accepted idex mutex init failed\n");
        return ERR_LIB_INIT;
    }

    return SERVER_SUCC;
}

srv_err_type wait_for_client_conn_and_accept(void)
{
    pthread_t thread_id;
    fd_set rfds;
    while(!server_terminate)
    {
        int socket_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if(-1==socket_fd)
        {
            if(errno == EINTR && server_terminate)
            {
                printf("Server termination signal received, terminating server.\n");
                break;
            }
            printf("[ accept ] failed, err : %s, err_no : %d\n",strerror(errno),errno);
        }
        else
        {
            int* new_socket = malloc(sizeof(int));
            if (!new_socket)
            {
                printf("malloc failed to allocate memory for new_socket\n");
                sleep(1);
                continue;
            }    
            *new_socket = socket_fd;
            printf("[ server ] new client connection , fd : %d\n",*new_socket);
            if(pthread_create(&thread_id,
                              NULL,
                              client_thread,
                              new_socket)
            )
            {
                printf("[ pthread_create ] failed, err : %s, err_no : %d\n",strerror(errno),errno);
                return ERR_THREAD_CREATE;
            }
            // pthread_detach(thread_id);
        }
    }
    // printf("Server terminating.\n");
    join_all_client_threads();
    free_all_client_nodes();
    return SERVER_TERMINATE_DETECTED;
}

srv_err_type send_conn_establish_msg(int fd)
{
    msg_t send_msg={0};
    srv_err_type ret_val = ERR_CONN_EST;
    sprintf(send_msg.msg_data.buffer,SERVER_UNIQUEUE_ID);
    send_msg.msg_type=MSG_CONN_ESTABLISH_REQ;
    int size = send(fd,&send_msg,sizeof(send_msg),0);
    if(size!=sizeof(send_msg) || (-1==size)) 
    {
        printf("Error in sending conn. establishment msg: %d\n",fd);
    }
    else
    {
        printf("Connection etablish msg sent successfully : %d\n",fd);

        memset(&send_msg,0,sizeof(send_msg));
        size = recv(fd,&send_msg,sizeof(send_msg),0);
        printf(" fd : %d send_msg size = %ld, ACK size recv : %d\n",fd,sizeof(send_msg),size);
        if(size!=sizeof(send_msg))
        {
            printf("Connection establish ack recv. error\n");
        }
        else
        {
            if(MSG_CONN_ESTABLISH_ACK==send_msg.msg_type) ret_val = SERVER_SUCC;
        }
    }

    return ret_val;
}

void* client_thread(void* arg) 
{
    int my_socket = *(int*)arg;
    free(arg);
    pthread_mutex_lock(&client_data_mutex);
    srv_queue_err_type_t ret_val = add_client_node_to_queue(&my_socket);
    if(ret_val!=SERVER_QUEUE_SUCC)
    {
        printf("Adding client node failed : %s\n",queueErrToStr(ret_val));
        pthread_mutex_unlock(&client_data_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&client_data_mutex);

    printf("my thread id : %lu .\n",pthread_self());

    if(SERVER_SUCC != send_conn_establish_msg(my_socket))
    {
        printf("Terminating thread as, connection cannot be established :%d \n",my_socket);
        return NULL;
    }
    while(!server_terminate)
    {
        msg_t rx_msg={0};
        srv_err_type ret = wait_to_recv_something(my_socket,&rx_msg);
        if(ret == SERVER_SUCC)
        {
            handle_rx_msg(rx_msg,my_socket);
        } 
        else if(ret == ERR_CLIENT_CLOSED_CONN)
        {
            pthread_mutex_lock(&client_data_mutex);
            int conn_fd = get_conn_fd_by_fd(my_socket);
            if( INVALID_FD != conn_fd)
            {
                if( CHAT_STATUS_FREE != get_client_chatting_status_by_fd(conn_fd) )
                {
                    printf("chat status of fd : %d, to CHAT_STATUS_FREE. and  to_fd to INVALIDE_FD.\n",conn_fd);
                    set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_FREE);
                    set_to_fd_by_fd(my_socket,INVALID_FD);
                    
                    msg_t terminate_msg={0};
                    terminate_msg.msg_type = MSG_CLIENT_TERMINATION;
                    strcpy(terminate_msg.msg_data.buffer,get_client_name_by_fd(my_socket));
                    send_msg_to_fd(conn_fd,terminate_msg);
                }
            }
            srv_queue_err_type_t qret = remove_client_node_from_queue_by_fd(my_socket);
            pthread_mutex_unlock(&client_data_mutex);

            if(SERVER_QUEUE_SUCC != qret)
            {
                printf("[ %s ] Error in removing queue from list. err : %s\n",__FUNCTION__,queueErrToStr(qret));
            }
            break;
        }
        else
        {
            sleep(1);
        }
    }
    printf("Exiting thread with fd : %d.\n",my_socket);
    pthread_exit(NULL);
}

void set_name_handler(int fd,msg_t msg)
{
    printf("Name change request to : %s\n",msg.msg_data.buffer);
    pthread_mutex_lock(&client_data_mutex);

    name_find_type_t ret = check_client_with_same_name_exist_or_not(msg.msg_data.buffer);

    if(ret!=NAME_NOT_EXIST)
    {
        pthread_mutex_unlock(&client_data_mutex);

        printf("[ %s ] Name already exist, or error while finding. err : %d \n",__FUNCTION__,ret);
        memset(&msg,0,sizeof(msg));
        msg.msg_type=MSG_SET_NAME_NACK_TYPE;
    
        int send_bytes = send(fd,&msg,sizeof(msg),0);
        if (send_bytes == -1 || send_bytes != sizeof(msg))
        {
            printf("Error in send set name failure nack, client fd :%d\n",fd);
        }
        return;
    }

    srv_queue_err_type_t ret_val = set_name_of_client_by_client_fd(fd,msg.msg_data.buffer);
    if(ret_val != SERVER_QUEUE_SUCC)
    {
        printf("Error in settig name of client id : %d, err : %s\n",fd,queueErrToStr(ret_val));
        pthread_mutex_unlock(&client_data_mutex);
        return;
    }

    printf("Name changed of client with fd :%d to %s \n",fd,msg.msg_data.buffer);
    pthread_mutex_unlock(&client_data_mutex);

    memset(&msg,0,sizeof(msg));
    msg.msg_type=MSG_SET_NAME_ACK_TYPE;

    int send_bytes = send(fd,&msg,sizeof(msg),0);
    if (send_bytes == -1 || send_bytes != sizeof(msg))
    {
        printf("Error in send set name success ack, client fd :%d\n",fd);
    }
}

void send_client_list_handler(int fd)
{
    msg_t client_list={0};
    client_list.msg_type = MSG_GET_CLIENT_LIST_TYPE;
    memset(client_list.msg_data.buffer,'\0',sizeof(client_list.msg_data.buffer));

    srv_queue_err_type_t err = get_client_list(client_list.msg_data.buffer);

    printf("client list : %s \n",client_list.msg_data.buffer);
    printf("Got sock fd : %d \n",fd);
    if(INVALID_FD==fd)
    {
        printf("[ %s ] Invalid sock fd get.\n",__FUNCTION__);
    }
    else
    {
        send_msg_to_fd(fd,client_list);
    }
}

void handle_rx_msg(msg_t msg,int fd)
{
    srv_err_type ret;
    switch(msg.msg_type)
    {
        case MSG_SET_NAME_REQ_TYPE:
            set_name_handler(fd,msg);
            break;

        case MSG_GET_CLIENT_LIST_TYPE:
            send_client_list_handler(fd);
            break;

        case MSG_CONNECT_TO_CLIENT:
            handle_chat_connection_request(fd,msg.msg_data.buffer);
            break;

        case MSG_CLIENT_ACCEPT_CONNECTION:
            handle_conn_accept(fd,msg);
            break;

        case MSG_CLIENT_TX_TYPE:
            handle_tx_msg(fd,msg);
            break;

        case MSG_CLIENT_DECLINE_CONNECTION:
            handle_decline_conn_request(fd);
            break;

        case MSG_CLIENT_CHANGE_CONN_FD_REQ:
            handle_change_conn_fd_req(fd,msg);
            break;
    }
}

void handle_change_conn_fd_req(int fd, msg_t msg)
{
    if(INVALID_FD==fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
        return;
    }

}

void handle_decline_conn_request(int fd)
{
    if(INVALID_FD==fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
        return;
    }
    pthread_mutex_lock(&client_data_mutex);
    if(CHAT_STATUS_REQ_PENDING==get_client_chatting_status_by_fd(fd))
    {
        int conn_fd = get_conn_fd_by_fd(fd);
        char* my_name = get_client_name_by_fd(fd);
        pthread_mutex_unlock(&client_data_mutex);

        if(INVALID_FD==conn_fd)
        {
            printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
            return;
        }
        msg_t decline_resp_msg ={0};
        decline_resp_msg.msg_type=MSG_CLIENT_DECLINE_CONNECTION_ACK;
        strcpy(decline_resp_msg.msg_data.buffer,my_name);
        send_msg_to_fd(conn_fd,decline_resp_msg);

        pthread_mutex_lock(&client_data_mutex);
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        pthread_mutex_unlock(&client_data_mutex);
    }
    else
    {
        pthread_mutex_unlock(&client_data_mutex);
        printf("chat status is not CHAT_STATUS_REQ_PENDING , and got decline msg.\n");
    }
}

void handle_tx_msg(int fd, msg_t msg)
{
    pthread_mutex_lock(&client_data_mutex);
    if(CHAT_STATUS_BUSY==get_client_chatting_status_by_fd(fd))
    {
        int conn_fd = get_conn_fd_by_fd(fd);
        pthread_mutex_unlock(&client_data_mutex);
        if(0==strcmp(msg.msg_data.buffer,DISCONNECT_CMD))
        {
            if(INVALID_FD!=conn_fd)
            {
                msg_t disconnected_msg={0};
                disconnected_msg.msg_type = MSG_CLIENT_DISCONNECTED;

                pthread_mutex_lock(&client_data_mutex);
                strcpy(disconnected_msg.msg_data.buffer,get_client_name_by_fd(fd));
                set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
                set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_FREE);
                set_to_fd_by_fd(fd,INVALID_FD);
                set_to_fd_by_fd(conn_fd,INVALID_FD);
                pthread_mutex_unlock(&client_data_mutex);
                send_msg_to_fd(conn_fd,disconnected_msg);
            }
            else
            {
                printf("[1] [ %s ] Invalid fd found.\n",__FUNCTION__);
            }
        }
        else
        {
            if(INVALID_FD!=conn_fd)
            {
                msg.msg_type=MSG_CLIENT_RX_TYPE;
                printf("sending msg to : %d from %d.\n",conn_fd,fd);
                send_msg_to_fd(conn_fd,msg);
            }
            else
            {
                printf("[2] [ %s ] Invalid fd found.\n",__FUNCTION__);
            }
        }
    }
    else
    {
        pthread_mutex_unlock(&client_data_mutex);
    }
}

srv_err_type wait_to_recv_something(int fd, msg_t* rx_msg)
{
    // printf("[ %s ] inside this function.\n",__FUNCTION__);
    if(!rx_msg) return ERR_NULL_PTR;

    struct pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    int ret = poll(fds, 1, 2000);

    if (ret < 0)
    {
        if (EINTR == errno ) 
        {
            printf("Poll interrupted by signal.\n");
            return ERR_CLIENT_CLOSED_CONN;
        }
        printf("[ %s ] Error in poll.\n",__FUNCTION__);
        return ERR_CLIENT_CLOSED_CONN;
    }
    else if(ret==0)
    {
        // printf("[ %s ] Poll timeout.\n",__FUNCTION__);
        return ERR_READ_TIMEOUT;
    }

    if (fds[0].revents & POLLIN)
    {
        int bytes = recv(fd, rx_msg, sizeof(*rx_msg), 0);
        if (bytes > 0)
        {
            return SERVER_SUCC;
        }
        else if (bytes == 0)
        {
            printf("Client termination detected fd : [ %d ].\n",fd);
            return ERR_CLIENT_CLOSED_CONN;
        }
        else
        {
            printf("[ %s ] error in receive from client fd : %d .\n",__FUNCTION__,fd);;
            return ERR_RECV;
        }
    }
    return ERR_RECV;
}

srv_err_type send_msg_to_fd(int fd,msg_t send_msg)
{
    int size = send(fd,&send_msg,sizeof(send_msg),0);
    if(size!=sizeof(send_msg) || (-1==size)) 
    {
        printf("Error in sending msg to fd : %d\n",fd);
        return ERR_MSG_SEND;
    }
    printf("msg sent successfully , %d\n",fd);

    return SERVER_SUCC;

}

void handle_chat_connection_request(int fd,char* conn_client_name)
{
    printf("[ %s ] inside this.\n",__FUNCTION__);
    if( (INVALID_FD==fd) || (!conn_client_name)) 
    {
        printf("Invalid fd or null client conn name found.\n");
        return;
    }

    printf("my fd : %d.\n",fd);
    msg_t conn_resp_msg={0};
    msg_t conn_req_send_msg={0};
    memcpy(conn_resp_msg.msg_data.buffer,conn_client_name,MAX_CLIENT_NAME_LEN);
    pthread_mutex_lock(&client_data_mutex);
    name_find_type_t ret = check_client_with_same_name_exist_or_not(conn_client_name);

    if(ret==NAME_EXISTS)
    {
        char *my_name = get_client_name_by_fd(fd);
        if(!strcmp(my_name,conn_client_name))
        {
            pthread_mutex_unlock(&client_data_mutex);
            printf("[ %s ] tries to connect with itself !!!\n",conn_client_name);
            conn_resp_msg.msg_type=MSG_ATTEMPT_TO_CONNECT_TO_SELF;
        }
        else
        {
            client_chat_status_t chat_status = get_client_chatting_status_by_name(conn_client_name);
            pthread_mutex_unlock(&client_data_mutex);
    
            if(chat_status == CHAT_STATUS_CLIENT_NOT_FOUND)
            {
                printf("[ 1 ]client not found with name : %s.\n",conn_client_name);
                conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
            }
            else
            {
                printf("Client with name %s exist. chat_status : %s\n",conn_client_name,chat_status_to_str(chat_status));
                if(CHAT_STATUS_FREE == chat_status)
                {
                    conn_resp_msg.msg_type=MSG_CLIENT_FREE;
                    pthread_mutex_lock(&client_data_mutex);
                    int conn_client_fd = get_client_fd_by_name(conn_client_name);
                    printf("[ 1 ] my_fd : %d, conn fd : %d.\n",fd,conn_client_fd);
                    set_client_chatting_status_by_fd(conn_client_fd,CHAT_STATUS_REQ_PENDING);
                    pthread_mutex_unlock(&client_data_mutex);
                    if(INVALID_FD==conn_client_fd)
                    {
                        printf("[ %s ] Invalid fd got.\n",__FUNCTION__);
                        conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
                    }
                    else
                    {
                        //sending conn request to client required.
                        pthread_mutex_lock(&client_data_mutex);
                        printf("[ 2 ] my_fd : %d, conn fd : %d.\n",fd,conn_client_fd);

                        set_to_fd_by_fd(fd,conn_client_fd);
                        set_to_fd_by_fd(conn_client_fd,fd);
                        pthread_mutex_unlock(&client_data_mutex);
                        printf("Sending conn request from [ %s ] : [ %s ] .\n",my_name,conn_client_name);
                        conn_req_send_msg.msg_type=MSG_CONNECTION_REQ_RX;
                        strcpy(conn_req_send_msg.msg_data.buffer,my_name);
                        send_msg_to_fd(conn_client_fd,conn_req_send_msg);
                    }
                }
                else if (CHAT_STATUS_BUSY == chat_status)
                {
                    conn_resp_msg.msg_type=MSG_CLIENT_BUSY;                    
                }
                else if(CHAT_STATUS_REQ_PENDING == chat_status)
                {
                    conn_resp_msg.msg_type=MSG_CLIENT_STATUS_REQ_PENDING;
                }
                else
                {
                    printf("[ %s ] Should Never come here.\n",__FUNCTION__);
                    //if came then mark client not found.
                    conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
                }
            }        
        }
    }
    else
    {
        pthread_mutex_unlock(&client_data_mutex);
        printf("[ 2 ]client not found with name : %s.\n",conn_client_name);
        conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
    }

    int send_bytes = send(fd,&conn_resp_msg,sizeof(conn_resp_msg),0);
    if (send_bytes == -1 || send_bytes != sizeof(conn_resp_msg))
    {
        printf("Error in sending ack for conn req to, client fd :%d\n",fd);
    }
}

void handle_conn_accept(int fd, msg_t msg)
{
    printf("[ fd :%d, %s ] Inside this fn.\n",fd,__FUNCTION__);
    pthread_mutex_lock(&client_data_mutex);
    client_chat_status_t my_chat_status = get_client_chatting_status_by_fd(fd);
    printf("chatting status of %d is %s.\n",fd,chat_status_to_str(my_chat_status));
    int conn_fd = get_conn_fd_by_fd(fd);
    if(INVALID_FD==conn_fd)
    {
        printf("[ %s ] Invalid fd got.\n",__FUNCTION__);
        return;
    }
    client_chat_status_t conn_client_chat_status = get_client_chatting_status_by_fd(conn_fd);
    char *conn_client_name = get_client_name_by_fd(conn_fd);
    if(0==strcmp(conn_client_name,UNDEF_NAME))
    {
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        set_to_fd_by_fd(fd,INVALID_FD);
    
        pthread_mutex_unlock(&client_data_mutex);
        printf("Client with fd : %d, no more exist.\n",conn_fd);
        msg_t client_not_exit_msg={0};
        strcpy(client_not_exit_msg.msg_data.buffer,"Client_not_exist");
        client_not_exit_msg.msg_type= MSG_CLIENT_NO_MORE_EXIST;
        send_msg_to_fd(fd,client_not_exit_msg);
        return;
    }
    printf("%d connects to %s.\n",fd,conn_client_name);
    pthread_mutex_unlock(&client_data_mutex);
    printf("conn_cliennt_chat status : %s.\n",chat_status_to_str(conn_client_chat_status));
    if(CHAT_STATUS_BUSY == conn_client_chat_status)
    {
        printf("[ %d ] is no more free.\n",conn_fd);
        pthread_mutex_lock(&client_data_mutex);
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        pthread_mutex_unlock(&client_data_mutex);
        msg_t nack_resp_msg={0};
        strcpy(nack_resp_msg.msg_data.buffer,conn_client_name);
        nack_resp_msg.msg_type= MSG_CLIENT_NO_MORE_FREE;
        send_msg_to_fd(fd,nack_resp_msg);
        return;
    }

    if(CHAT_STATUS_REQ_PENDING!=my_chat_status)
    {
        printf("[ fd:%d ] There is no current pending request. Ignoring accept request.\n",fd);
        msg_t accept_ign_msg={0};
        strcpy(accept_ign_msg.msg_data.buffer,"SOMETHING_IS_WRONG");
        accept_ign_msg.msg_type= MSG_CLIENT_ACCEPT_CONNECTION_ACK;
        send_msg_to_fd(fd,accept_ign_msg);
        return;
    }
    else
    {
        msg_t accept_respt_msg={0};

        pthread_mutex_lock(&client_data_mutex);
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_BUSY);
        set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_BUSY);

        set_to_fd_by_fd(fd,conn_fd);
        set_to_fd_by_fd(conn_fd,fd);  

        char *conn_name = get_client_name_by_fd(conn_fd);
        char *my_name   = get_client_name_by_fd(fd);
        pthread_mutex_unlock(&client_data_mutex);

        if((0==strcmp(conn_name,UNDEF_NAME)) || (0==strcmp(my_name,UNDEF_NAME)))
        {
            printf("[ %s ] Undef name found.\n",__FUNCTION__);
            return;
        }

        strcpy(accept_respt_msg.msg_data.buffer,my_name);
        accept_respt_msg.msg_type= MSG_CLIENT_ACCEPT_CONNECTION_ACK;

        msg_t self_ack_msg={0};
        self_ack_msg.msg_type = MSG_CLIENT_CHAT_READY;
        strcpy(self_ack_msg.msg_data.buffer,conn_name);
        send_msg_to_fd(fd,self_ack_msg);
        send_msg_to_fd(conn_fd,accept_respt_msg);
    }
}