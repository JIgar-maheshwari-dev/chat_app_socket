#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <signal.h>
#include "client_lib.h"
#include "chat_app_common.h"

#define RETVAL(x) ((void*)(intptr_t)(x))
#define GETVAL(ptr)   ((client_err_type_t)(intptr_t)(ptr))

client_err_type_t recv_with_timeout(int sock, void *buf, size_t len, int timeout_sec);
pthread_t io_thread_id;

// client_err_type_t (*rx_msg_handle_cb)(msg_t rx_msg) = NULL;
int sock = INVALID_FD;
lib_params_t *cb_parameters=NULL;
bool conn_request_rx       = false;

const char *errStr[] = {
    "UNDEFINED_CLIENT_ERR",
    "CLIENT_SUCCESS",
    "CONNECTION_FAILED",
    "CLIENT_NOT_CONNECTED",
    "CLIENT_MSG_SEND_ERR",
    "CLIENT_ERR_THREAD_CREATE",	
    "CLIENT_READ_ERROR",
    "CLIENT_READ_TIMEOUT",
    "CLIENT_CB_PARAMS_NOT_SET",
    "CLIENT_MAX_ERR"
};

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
	"MSG_CLIENT_CHANGE_CONN_FD_REQ"
};

void handle_rx_msg_lib(int sock,msg_t rx_msg);

void print_bin_info(void)
{
    printf("*******************************\n");
    printf("Owner             : %s \n",OWNER);
    printf("Cleint build date : %s \n",BUILD_DATE );
    printf("Cleint build time : %s \n",BUILD_TIME );
    printf("*******************************\n\n");
}

void sig_int_handler(int sig)
{
	if(sig==SIGINT)
	{
		printf("Client termination signal received.\n");
		if(NULL==cb_parameters)
		{
			printf("cb_params not set yet.\n");
			return;
		}
		else
		{
			*(cb_parameters->client_shut_down_flag) = true;
		}
	}
}

client_err_type_t check_lib_params(void)
{
	if(!cb_parameters)
	{
		return CLIENT_ERR_NULL_PTR;
	}
	else if( (!cb_parameters->client_shut_down_flag) || 
	         (!cb_parameters->server_shut_down_flag) || 
			 (!cb_parameters->msg_handle_cb) ||
			 (!cb_parameters->busy_in_chat)
			)
	{
		return CLIENT_ERR_NULL_PTR;
	}
	return CLIENT_SUCCESS;
}

client_err_type_t connect_to_server(void)
{
	print_bin_info();
    struct sockaddr_in serv_addr;

	if(CLIENT_SUCCESS!=check_lib_params())
	{
		printf("Lib cb params not set yet.\n");
		return CLIENT_CB_PARAMS_NOT_SET;
	}

    struct sigaction sa = {0};
    sa.sa_handler = sig_int_handler;   
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;          
    sigaction(SIGINT, &sa, NULL);


    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)))
	{
		printf("[ connect ] failed, err : %s, errno : %d\n",strerror(errno),errno);
		return CONNECTION_FAILED;
	}
    printf("Connected to server.\n");

	msg_t temp_msg={0};
	client_err_type_t err = recv_with_timeout(sock,&temp_msg,sizeof(temp_msg),5);
	// printf("err : %d , server_key : %s , msg_type : %d\n",err,temp_msg.msg_data.buffer, temp_msg.msg_type);

	if(CLIENT_SUCCESS != err )
	{
		printf("Connection verification failed.\n");
		return CONNECTION_FAILED;
	}
	else
	{
		if( (MSG_CONN_ESTABLISH_REQ==temp_msg.msg_type) && (0==strcmp(temp_msg.msg_data.buffer,SERVER_UNIQUEUE_ID) ))
		{
			printf("Connection verified successfully\n");
			msg_t send_node={0};
			send_node.msg_type=MSG_CONN_ESTABLISH_ACK;
			client_err_type_t err = send_msg_to_server(send_node);
			if(err!=CLIENT_SUCCESS) return CONNECTION_FAILED;
		}
		else
		{
			printf("Server key mismatched.\n");
			return CONNECTION_FAILED;
		}
	}
	return CLIENT_SUCCESS;
}

void set_my_name(char *name_to_set)
{
	if(NULL==name_to_set)
	{
		printf("[ %s ] Null ptr found\n",__FUNCTION__);
		return;
	}

	msg_t send_msg={0};
	send_msg.to_client_id=MSG_SERVER;
	strcpy(send_msg.msg_data.buffer,name_to_set);
	send_msg.msg_type = MSG_SET_NAME_REQ_TYPE;
	client_err_type_t ret = send_msg_to_server(send_msg);
	if(CLIENT_SUCCESS!=ret)
		printf("set-name msg send : failed\n");
	else
		printf("set-name msg send : success,name : %s\n",name_to_set);
}

client_err_type_t send_msg_to_server(msg_t msg_to_send)
{
	if(sock==INVALID_FD) return CLIENT_NOT_CONNECTED;

	int ret = send(sock,&msg_to_send,sizeof(msg_to_send),0);
	if( (sizeof(msg_to_send)!=ret) || (-1==ret))
	{
		printf("Error in sending msg to server\n");
		return CLIENT_MSG_SEND_ERR;
	}
	printf("msg send to server successfuly\n");
	return CLIENT_SUCCESS;
}

void* io_thread(void* arg)
{
    struct pollfd fds[2];
    fds[0].fd = sock;
    fds[0].events = POLLIN;

    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

	msg_t rx_msg;

    while ( (!(*cb_parameters->client_shut_down_flag)) && (!(*cb_parameters->server_shut_down_flag)) ) 
    {
        int ret = poll(fds, 2, 2000);

        if (ret < 0)
        {
			if (EINTR == errno ) {
                printf("Poll interrupted by signal\n");
                break;
            }
            printf("[ %s ] Error in poll.\n",__FUNCTION__);
            break;
        }
		else if(ret==0)
		{
			// printf("[ %s ] Poll timeout.\n",__FUNCTION__);
			continue;
		}

        if (fds[0].revents & POLLIN)
        {
            int bytes = recv(sock, &rx_msg, sizeof(rx_msg), 0);
            if (bytes > 0)
            {
				if( (NULL!=cb_parameters) && (NULL!=cb_parameters->msg_handle_cb)) 
				{
					cb_parameters->msg_handle_cb(rx_msg);
					handle_rx_msg_lib(sock,rx_msg);
				}
			}
            else if (bytes == 0)
            {
                printf("Server shut-down detected.\n");
				*cb_parameters->server_shut_down_flag = true;
				*cb_parameters->server_shut_down_flag = true;
                break;
            }
            else
            {
                printf("[ %s ] error in receive from server.\n",__FUNCTION__);;
                break;
            }
        }
		if(fds[1].revents & POLLIN)
		{
			char buffer[MAX_MSG_LEN];
			int bytes = read(STDIN_FILENO, buffer, sizeof(buffer)-1);
            if (bytes > 0)
            {
				// printf("%d bytes received.\n",bytes);
				if((1==bytes) && (buffer[0]=='\n'))
				{
					// printf("Hehhe its only new line.\n");
					continue;
				}

				buffer[bytes] = '\0';
				if(buffer[bytes-1] == '\n')
					buffer[bytes-1] = '\0';

				char *start = buffer;
				while (*start == ' ' || *start == '\t')
					start++;
				
				// Move the trimmed string to the beginning of buf
				if (start != buffer) {
					memmove(buffer, start, strlen(start) + 1);
				}

				process_send_msg(buffer);
			}
            else if (bytes == 0)
            {
				printf("0 bytes received in poll of stdin.\n");
				continue;
            }
            else
            {
                printf("[ %s ] error in receive from STDIN. errno : %d, err: %s\n",__FUNCTION__,errno,strerror(errno));;
                break;
            }
		}
    }
	printf("Client io_thread is terminating.\n");
    return RETVAL(CLIENT_SUCCESS);
}

void get_client_list(void)
{
	msg_t client_list_req_struct={0};
	client_list_req_struct.msg_type = MSG_GET_CLIENT_LIST_TYPE;
	send_msg_to_server(client_list_req_struct);
}

const char *errTostr(client_err_type_t err)
{
	if(err >= CLIENT_MAX_ERR) return errStr[0];
	return errStr[err+1];
}

const char *msgTypeToStr(msg_type_t type)
{
	if(type >= MSG_TYPE_MAX) return msgTypeStr[0];
	return msgTypeStr[type+1];
}

client_err_type_t recv_with_timeout(int sock, void *buf, size_t len, int timeout_sec)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int retval = select(sock + 1, &readfds, NULL, NULL, &tv);

    if (retval == -1) 
	{
        printf("Error in select.\n");
        return CLIENT_READ_ERROR;
    }
    else if (retval == 0) 
	{
        printf("Timeout waiting for data\n");
        return CLIENT_READ_TIMEOUT;
    }
    else 
	{
        int ret = recv(sock, buf, len, 0);
		// printf("[ %s ] Received, ret : %d,buf->type : %d\n",__FUNCTION__,ret,((msg_t*)buf)->msg_type);
		return CLIENT_SUCCESS;
    }
}

void set_lib_params(lib_params_t* params)
{
	cb_parameters=params;
}

client_err_type_t chat_on(void)
{
	if(pthread_create(&io_thread_id,NULL,io_thread,NULL))
	{
		printf("Error in creating rx thread\n");
		return CLIENT_ERR_THREAD_CREATE;
	}
	void* io_thread_ret_val ;
	printf("joiing io_thread to main thread.\n");
	
	pthread_join(io_thread_id,&io_thread_ret_val);
	printf("io_thread joined to main thread. [ %s ]\n",errTostr(GETVAL(io_thread_ret_val)) );
	return GETVAL(io_thread_ret_val);
}

void connect_with_client(char *name)
{
	if(!name) 
	{
		printf("[ %s ] Null ptr found.\n",__FUNCTION__);
		return;
	}
	msg_t conn_req_msg={
		.msg_type = MSG_CONNECT_TO_CLIENT,
	};
	memcpy(&conn_req_msg.msg_data.buffer,name,MAX_MSG_LEN);

	send_msg_to_server(conn_req_msg);
	printf("Connection request to %s send.\n",name);
}

void handle_rx_msg_lib(int sock,msg_t rx_msg)
{
	switch(rx_msg.msg_type)
	{
		case MSG_CONNECTION_REQ_RX:
			conn_request_rx = true;
			break;
		
		case MSG_CLIENT_ACCEPT_CONNECTION_ACK:
			if(conn_request_rx)
			{
				printf("We have connn-request and some-one has requested our conn-request.\n");
			}
			conn_request_rx = false;
			*(cb_parameters->busy_in_chat)=true;
			break;

		case MSG_CLIENT_CHAT_READY:
			*(cb_parameters->busy_in_chat)=true;
			break;

		case MSG_CLIENT_NO_MORE_FREE:
		case MSG_CLIENT_TERMINATION:
		case MSG_CLIENT_DISCONNECTED:
			conn_request_rx = false;
			*(cb_parameters->busy_in_chat)=false;
			break;
	}
}
