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
#include "logger.h"
#include "server_mgmt.h"
#include "server_queue.h"



/* GLOBAL VARIABLES */

const char *msgTypeStr[] = {
	"UNDEFINED_MSG_TYPE",
    "MSG_CLIENT_RX_TYPE",
    "MSG_CLIENT_TX_TYPE",
    "MSG_CLIENT_ACK_TYPE",
    "MSG_CLIENT_NACK",
    "MSG_SET_NAME_REQ_TYPE",
    "MSG_SET_NAME_ACK_TYPE",
    "MSG_SET_NAME_NACK_TYPE",
    "MSG_SERVER",
    "MSG_CONN_ESTABLISH_REQ",
    "MSG_CONN_ESTABLISH_ACK",
    "MSG_GET_CLIENT_LIST_TYPE",

    "MSG_CONNECT_TO_CLIENT",
    "MSG_CONNECTION_REQ_RX",
    "MSG_CLIENT_BUSY",
    "MSG_CLIENT_FREE",
    "MSG_CLIENT_ACCEPT_CONNECTION",
	"MSG_CLIENT_DECLINE_CONNECTION",
	"MSG_CLIENT_DECLINE_CONNECTION_ACK",
    "MSG_CLIENT_NOT_EXIST",
    "MSG_ATTEMPT_TO_CONNECT_TO_SELF",
	"MSG_CLIENT_STATUS_REQ_PENDING",
	"MSG_CLIENT_ACCEPT_CONNECTION_ACK",
	"MSG_CLIENT_NO_MORE_FREE",
	"MSG_CLIENT_NO_MORE_EXIST",
	"MSG_CLIENT_TERMINATION",
	"MSG_CLIENT_DISCONNECTED",
	"MSG_CLIENT_CHAT_READY",
	"MSG_CLIENT_CHANGE_CONN_FD_REQ",
    "MSG_MAX_CLIENT_REACHED"
};

int server_fd = INVALID_FD;
struct sockaddr_in address;
int addrlen = sizeof(address);
pthread_mutex_t client_data_mutex;
bool server_terminate = false;
extern uint8_t total_available_clients;
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

const char *msgTypeToStr(msg_type_t type)
{
	if(type >= MSG_TYPE_MAX) return msgTypeStr[0];
	return msgTypeStr[type+1];
}

void sig_int_handler(int sig)
{
    if(SIGINT==sig)
    {
        LOGI("Server termination signal by user.");
        server_terminate = true;
    }
}

void print_bin_info(void)
{
    printf("*********************************************\n");
    printf("Owner             : %s \n",OWNER);
    printf("Server build date : %s \n",BUILD_DATE );
    printf("Server build time : %s \n",BUILD_TIME );
    printf("*********************************************\n\n");
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
        LOGE(" Failed to get socket for server.");
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
        LOGE("[ bind ] failed.");
        return ERR_LIB_INIT;
    }

    ret=listen(server_fd, MAX_LISTEN);
    if(-1==ret){
        LOGE("[ listen ] failed.");
        return ERR_LIB_INIT;
    }

    if (pthread_mutex_init(&client_data_mutex, NULL) != 0) {
        LOGE("[ server ] client_data_mutex init failed.");
        return ERR_LIB_INIT;
    }
    LOGI("Server init done.");
    return SERVER_SUCC;
}

srv_err_type wait_for_client_conn_and_accept(void)
{
    pthread_t thread_id;
    fd_set rfds;
    while(!server_terminate)
    {
        LOGI("Waiting for client connection.");
        int socket_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if(-1==socket_fd)
        {
            if(errno == EINTR && server_terminate)
            {
                LOGI("Server termination signal received, terminating server.");
                break;
            }
            LOGE("[ accept ] failed.");
        }
        else
        {
            LOCK_CLIENT_DATA_MUTEX();
            int temp_client_count = total_available_clients;
            UNLOCK_CLIENT_DATA_MUTEX();
            if(temp_client_count<MAX_CLIENT)
            {
                int* new_socket = malloc(sizeof(int));
                if (!new_socket)
                {
                    LOGE("malloc failed to allocate memory for new_socket.");
                    sleep(1);
                    continue;
                }    
                *new_socket = socket_fd;
                LOGI("new client connection , fd : %d.",*new_socket);
                if(pthread_create(&thread_id,
                                  NULL,
                                  client_thread,
                                  new_socket)
                )
                {
                    free(new_socket);
                    LOGE("[ pthread_create ] failed.");
                    return ERR_THREAD_CREATE;
                }    
            }
            else
            {
                LOGE("Max client limit reached !!!. client count : %d.",temp_client_count);
                msg_t max_client_msg={0};
                max_client_msg.msg_type=MSG_MAX_CLIENT_REACHED;
                send_msg_to_fd(socket_fd,max_client_msg);        
            }
        }
    }
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
    
    if(SERVER_SUCC!=send_msg_to_fd(fd,send_msg)) 
    {
        LOGE("Error in sending conn. establishment msg: %d.",fd);
    }
    else
    {
        LOGI("Connection etablish msg sent successfully to fd : %d.",fd);

        memset(&send_msg,0,sizeof(send_msg));
        int size = recv(fd,&send_msg,sizeof(send_msg),0);
        if(size!=sizeof(send_msg))
        {
            LOGE("Connection establish ack recv. error.");
        }
        else
        {
            LOGI("msg received successfully from, fd : %d, msg_type : %s.",fd, msgTypeToStr(send_msg.msg_type));
            if(MSG_CONN_ESTABLISH_ACK==send_msg.msg_type) 
            {
                ret_val = SERVER_SUCC;
                LOGI("Connection verified with client with fd : %d.",fd);
            }
        }
    }
    return ret_val;
}

void* client_thread(void* arg) 
{
    int my_socket = *(int*)arg;
    free(arg);
    LOGD("Client thread with fd : %d.",my_socket);

    LOCK_CLIENT_DATA_MUTEX();
    srv_queue_err_type_t ret_val = add_client_node_to_queue(&my_socket);
    if(ret_val!=SERVER_QUEUE_SUCC)
    {
        LOGE("Adding client node failed : %s. err : %d.",queueErrToStr(ret_val),ret_val);
        msg_t max_client_msg={0};
        max_client_msg.msg_type=MSG_MAX_CLIENT_REACHED;
        send_msg_to_fd(my_socket,max_client_msg);
        UNLOCK_CLIENT_DATA_MUTEX();
        return NULL;
    }
    UNLOCK_CLIENT_DATA_MUTEX();

    if(SERVER_SUCC != send_conn_establish_msg(my_socket))
    {
        LOGE("Terminating thread as, connection cannot be established :%d.",my_socket);
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
            LOCK_CLIENT_DATA_MUTEX();
            int conn_fd = get_conn_fd_by_fd(my_socket);
            if( INVALID_FD != conn_fd)
            {
                if( CHAT_STATUS_FREE != get_client_chatting_status_by_fd(conn_fd) )
                {
                    LOGI("chat status of fd : %d, to CHAT_STATUS_FREE. and  to_fd to INVALID_FD.",conn_fd);
                    set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_FREE);
                    set_to_fd_by_fd(my_socket,INVALID_FD);
                    
                    msg_t terminate_msg={0};
                    terminate_msg.msg_type = MSG_CLIENT_TERMINATION;
                    strcpy(terminate_msg.msg_data.buffer,get_client_name_by_fd(my_socket));
                    send_msg_to_fd(conn_fd,terminate_msg);
                }
            }
            srv_queue_err_type_t qret = remove_client_node_from_queue_by_fd(my_socket);
            UNLOCK_CLIENT_DATA_MUTEX();

            if(SERVER_QUEUE_SUCC != qret)
            {
                LOGE("Error in removing queue from list. err : %s.",queueErrToStr(qret));
            }
            break;
        }
        else
        {
            sleep(1);
        }
    }
    LOGD("[ %lu ] Exiting thread.",pthread_self());
    pthread_exit(NULL);
}

void set_name_handler(int fd,msg_t msg)
{
    LOGD("client with fd : %d has Name change request to : %s.",fd,msg.msg_data.buffer);
    LOCK_CLIENT_DATA_MUTEX();

    name_find_type_t ret = check_client_with_same_name_exist_or_not(msg.msg_data.buffer);

    if(ret!=NAME_NOT_EXIST)
    {
        UNLOCK_CLIENT_DATA_MUTEX();

        LOGE("Name already exist, or error while finding. err : %d.",ret);
        memset(&msg,0,sizeof(msg));
        msg.msg_type=MSG_SET_NAME_NACK_TYPE;
        send_msg_to_fd(fd,msg);
        return;
    }

    srv_queue_err_type_t ret_val = set_name_of_client_by_client_fd(fd,msg.msg_data.buffer);
    if(ret_val != SERVER_QUEUE_SUCC)
    {
        LOGE("Error in settig name of client with fd : %d, err : %s.",fd,queueErrToStr(ret_val));
        UNLOCK_CLIENT_DATA_MUTEX();
        return;
    }

    LOGI("Name changed of client with fd :%d to %s.",fd,msg.msg_data.buffer);
    UNLOCK_CLIENT_DATA_MUTEX();

    memset(&msg,0,sizeof(msg));
    msg.msg_type=MSG_SET_NAME_ACK_TYPE;
    send_msg_to_fd(fd,msg);
    return;
}

void send_client_list_handler(int fd)
{
    msg_t client_list={0};
    client_list.msg_type = MSG_GET_CLIENT_LIST_TYPE;
    memset(client_list.msg_data.buffer,'\0',sizeof(client_list.msg_data.buffer));

    srv_queue_err_type_t err = get_client_list(client_list.msg_data.buffer);

    LOGI("client list : %s .",client_list.msg_data.buffer);
    if(INVALID_FD==fd)
    {
        LOGE("Invalid sock fd get.");
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
        LOGE("Invalid fd found.");
        return;
    }
}

void handle_decline_conn_request(int fd)
{
    if(INVALID_FD==fd)
    {
        LOGE("fd : %d,Invalid fd found.",fd);
        return;
    }
    LOCK_CLIENT_DATA_MUTEX();
    if(CHAT_STATUS_REQ_PENDING==get_client_chatting_status_by_fd(fd))
    {
        int conn_fd = get_conn_fd_by_fd(fd);
        char* my_name = get_client_name_by_fd(fd);
        UNLOCK_CLIENT_DATA_MUTEX();

        if(INVALID_FD==conn_fd)
        {
            LOGE("fd : %d,Invalid fd found.",fd);
            return;
        }
        msg_t decline_resp_msg ={0};
        decline_resp_msg.msg_type=MSG_CLIENT_DECLINE_CONNECTION_ACK;
        strcpy(decline_resp_msg.msg_data.buffer,my_name);
        send_msg_to_fd(conn_fd,decline_resp_msg);

        LOCK_CLIENT_DATA_MUTEX();
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        UNLOCK_CLIENT_DATA_MUTEX();
    }
    else
    {
        UNLOCK_CLIENT_DATA_MUTEX();
        LOGI("fd : %d, chat status is not CHAT_STATUS_REQ_PENDING , and got decline msg.",fd);
    }
}

void handle_tx_msg(int fd, msg_t msg)
{
    LOGD("fd : %d.",fd);
    LOCK_CLIENT_DATA_MUTEX();
    if(CHAT_STATUS_BUSY==get_client_chatting_status_by_fd(fd))
    {
        int conn_fd = get_conn_fd_by_fd(fd);
        UNLOCK_CLIENT_DATA_MUTEX();
        if(0==strcmp(msg.msg_data.buffer,DISCONNECT_CMD))
        {
            if(INVALID_FD!=conn_fd)
            {
                msg_t disconnected_msg={0};
                disconnected_msg.msg_type = MSG_CLIENT_DISCONNECTED;

                LOCK_CLIENT_DATA_MUTEX();
                strcpy(disconnected_msg.msg_data.buffer,get_client_name_by_fd(fd));
                set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
                set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_FREE);
                set_to_fd_by_fd(fd,INVALID_FD);
                set_to_fd_by_fd(conn_fd,INVALID_FD);
                UNLOCK_CLIENT_DATA_MUTEX();
                send_msg_to_fd(conn_fd,disconnected_msg);
            }
            else
            {
                LOGE("fd : %d,Invalid fd found.",fd);
            }
        }
        else
        {
            if(INVALID_FD!=conn_fd)
            {
                msg.msg_type=MSG_CLIENT_RX_TYPE;
                LOGI("sending msg to : %d from %d.",conn_fd,fd);
                send_msg_to_fd(conn_fd,msg);
            }
            else
            {
                LOGE("fd : %d,Invalid fd found.",fd);
            }
        }
    }
    else
    {
        UNLOCK_CLIENT_DATA_MUTEX();
    }
}

srv_err_type wait_to_recv_something(int fd, msg_t* rx_msg)
{
    // LOGD("fd :%d.",fd);
    if(!rx_msg) return ERR_NULL_PTR;

    struct pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    int ret = poll(fds, 1, 2000);

    if (ret < 0)
    {
        if (EINTR == errno ) 
        {
            LOGE("fd : %d,Poll interrupted by signal.",fd);
            return ERR_CLIENT_CLOSED_CONN;
        }
        LOGE("fd : %d,Error in poll.",fd);
        return ERR_CLIENT_CLOSED_CONN;
    }
    else if(ret==0)
    {
        // LOGI("fd : %d, Poll timeout.",fd);
        return ERR_READ_TIMEOUT;
    }

    if (fds[0].revents & POLLIN)
    {
        int bytes = recv(fd, rx_msg, sizeof(*rx_msg), 0);
        if (bytes > 0)
        {
            LOGI("msg received successfully from, fd : %d, msg_type : %s.",fd,msgTypeToStr(rx_msg->msg_type));
            return SERVER_SUCC;
        }
        else if (bytes == 0)
        {
            LOGI("Client termination detected fd : [ %d ].",fd);
            return ERR_CLIENT_CLOSED_CONN;
        }
        else
        {
            LOGE("error in receive from client fd : %d .",fd);;
            return ERR_RECV;
        }
    }
    return ERR_RECV;
}

srv_err_type send_msg_to_fd(int fd,msg_t send_msg)
{
    LOGD("");
    int size = send(fd,&send_msg,sizeof(send_msg),0);
    if(size!=sizeof(send_msg) || (-1==size)) 
    {
        LOGE("Error in sending msg to fd : %d.",fd);
        return ERR_MSG_SEND;
    }
    LOGI("msg sent successfully to fd : %d msg_type : %s.",fd,msgTypeToStr(send_msg.msg_type));

    return SERVER_SUCC;

}

void handle_chat_connection_request(int fd,char* conn_client_name)
{
    LOGD("fd :%d.",fd);
    if( (INVALID_FD==fd) || (!conn_client_name)) 
    {
        LOGE("fd : %d, Invalid fd or null client conn name found.",fd);
        return;
    }

    msg_t conn_resp_msg={0};
    msg_t conn_req_send_msg={0};
    memcpy(conn_resp_msg.msg_data.buffer,conn_client_name,MAX_CLIENT_NAME_LEN);
    LOCK_CLIENT_DATA_MUTEX();
    name_find_type_t ret = check_client_with_same_name_exist_or_not(conn_client_name);

    if(ret==NAME_EXISTS)
    {
        char *my_name = get_client_name_by_fd(fd);
        if(!strcmp(my_name,conn_client_name))
        {
            UNLOCK_CLIENT_DATA_MUTEX();
            LOGI("[ %s ] tries to connect with itself !!!",conn_client_name);
            conn_resp_msg.msg_type=MSG_ATTEMPT_TO_CONNECT_TO_SELF;
        }
        else
        {
            client_chat_status_t chat_status = get_client_chatting_status_by_name(conn_client_name);
            UNLOCK_CLIENT_DATA_MUTEX();
    
            if(chat_status == CHAT_STATUS_CLIENT_NOT_FOUND)
            {
                LOGI("fd : %d, client not found with name : %s.",fd,conn_client_name);
                conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
            }
            else
            {
                LOGI("fd : %d, Client with name %s exist. chat_status : %s.",fd,conn_client_name,chat_status_to_str(chat_status));
                if(CHAT_STATUS_FREE == chat_status)
                {
                    conn_resp_msg.msg_type=MSG_CLIENT_FREE;
                    LOCK_CLIENT_DATA_MUTEX();
                    int conn_client_fd = get_client_fd_by_name(conn_client_name);
                    set_client_chatting_status_by_fd(conn_client_fd,CHAT_STATUS_REQ_PENDING);
                    UNLOCK_CLIENT_DATA_MUTEX();
                    if(INVALID_FD==conn_client_fd)
                    {
                        LOGE("fd : %d, Invalid fd got.",fd);
                        conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
                    }
                    else
                    {
                        //sending conn request to client required.
                        LOCK_CLIENT_DATA_MUTEX();
                        set_to_fd_by_fd(fd,conn_client_fd);
                        set_to_fd_by_fd(conn_client_fd,fd);
                        UNLOCK_CLIENT_DATA_MUTEX();
                        LOGI("Sending conn request from [ %s ] : [ %s ] .",my_name,conn_client_name);
                        conn_req_send_msg.msg_type=MSG_CONNECTION_REQ_RX;
                        strcpy(conn_req_send_msg.msg_data.buffer,my_name);
                        send_msg_to_fd(conn_client_fd,conn_req_send_msg);
                    }
                }
                else if (CHAT_STATUS_BUSY == chat_status)
                {
                    LOGI("fd : %d, conn_client is busy.",fd);
                    conn_resp_msg.msg_type=MSG_CLIENT_BUSY;                    
                }
                else if(CHAT_STATUS_REQ_PENDING == chat_status)
                {
                    LOGI("fd : %d, conn_client has pending connectoin request.",fd);
                    conn_resp_msg.msg_type=MSG_CLIENT_STATUS_REQ_PENDING;
                }
                else
                {
                    LOGE("fd : %d,somethig is wrong.",fd);
                    //if came then mark client not found.
                    conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
                }
            }        
        }
    }
    else
    {
        UNLOCK_CLIENT_DATA_MUTEX();
        LOGE("fd : %d,client not found with name : %s.",fd,conn_client_name);
        conn_resp_msg.msg_type=MSG_CLIENT_NOT_EXIST;
    }

    send_msg_to_fd(fd,conn_resp_msg);
}

void handle_conn_accept(int fd, msg_t msg)
{
    LOGD("fd : %d, Inside this fn.",fd);
    LOCK_CLIENT_DATA_MUTEX();
    client_chat_status_t my_chat_status = get_client_chatting_status_by_fd(fd);
    LOGI("chatting status of %d is %s.",fd,chat_status_to_str(my_chat_status));
    int conn_fd = get_conn_fd_by_fd(fd);
    if(INVALID_FD==conn_fd)
    {
        LOGE("fd : %d,Invalid fd found.",fd);
        return;
    }
    client_chat_status_t conn_client_chat_status = get_client_chatting_status_by_fd(conn_fd);
    char *conn_client_name = get_client_name_by_fd(conn_fd);
    if(0==strcmp(conn_client_name,UNDEF_NAME))
    {
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        set_to_fd_by_fd(fd,INVALID_FD);
    
        UNLOCK_CLIENT_DATA_MUTEX();
        LOGI("fd : %d,Client with fd : %d, no more exist.",fd,conn_fd);
        msg_t client_not_exit_msg={0};
        strcpy(client_not_exit_msg.msg_data.buffer,"Client_not_exist");
        client_not_exit_msg.msg_type= MSG_CLIENT_NO_MORE_EXIST;
        send_msg_to_fd(fd,client_not_exit_msg);
        return;
    }
    LOGI("%d connects to %s.",fd,conn_client_name);
    UNLOCK_CLIENT_DATA_MUTEX();
    LOGI("conn_cliennt_chat status : %s.",chat_status_to_str(conn_client_chat_status));
    if(CHAT_STATUS_BUSY == conn_client_chat_status)
    {
        LOGI("fd: %d, [ %d ] is no more free.",fd,conn_fd);
        LOCK_CLIENT_DATA_MUTEX();
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_FREE);
        UNLOCK_CLIENT_DATA_MUTEX();
        msg_t nack_resp_msg={0};
        strcpy(nack_resp_msg.msg_data.buffer,conn_client_name);
        nack_resp_msg.msg_type= MSG_CLIENT_NO_MORE_FREE;
        send_msg_to_fd(fd,nack_resp_msg);
        return;
    }

    if(CHAT_STATUS_REQ_PENDING!=my_chat_status)
    {
        LOGI("fd : %d, There is no current pending request. Ignoring accept request.",fd);
        msg_t accept_ign_msg={0};
        strcpy(accept_ign_msg.msg_data.buffer,"SOMETHING_IS_WRONG");
        accept_ign_msg.msg_type= MSG_CLIENT_ACCEPT_CONNECTION_ACK;
        send_msg_to_fd(fd,accept_ign_msg);
        return;
    }
    else
    {
        msg_t accept_respt_msg={0};

        LOCK_CLIENT_DATA_MUTEX();
        set_client_chatting_status_by_fd(fd,CHAT_STATUS_BUSY);
        set_client_chatting_status_by_fd(conn_fd,CHAT_STATUS_BUSY);

        set_to_fd_by_fd(fd,conn_fd);
        set_to_fd_by_fd(conn_fd,fd);  

        char *conn_name = get_client_name_by_fd(conn_fd);
        char *my_name   = get_client_name_by_fd(fd);
        UNLOCK_CLIENT_DATA_MUTEX();

        if((0==strcmp(conn_name,UNDEF_NAME)) || (0==strcmp(my_name,UNDEF_NAME)))
        {
            LOGE("fd : %d, Undef name found.",fd);
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

char* get_current_time(void) {
    static char buf[20];
    time_t now = time(NULL);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}
