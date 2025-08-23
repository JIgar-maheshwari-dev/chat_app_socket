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
#include "logger.h"
#include "client_lib.h"
#include "chat_app_common.h"

#define RETVAL(x) ((void*)(intptr_t)(x))
#define GETVAL(ptr)   ((client_err_type_t)(intptr_t)(ptr))

client_err_type_t recv_with_timeout(int sock, void *buf, size_t len, int timeout_sec);
pthread_t io_thread_id;

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
    printf("*********************************************\n");
    printf("Owner             : %s \n",OWNER);
    printf("Cleint build date : %s \n",BUILD_DATE );
    printf("Cleint build time : %s \n",BUILD_TIME );
    printf("*********************************************\n\n");
}

void sig_int_handler(int sig)
{
	LOGI("Client termination signal received.");
	if(sig==SIGINT)
	{
		if(NULL==cb_parameters)
		{
			LOGI("cb_params are null.");
			return;
		}
		else
		{
			*(cb_parameters->client_shut_down_flag) = true;
			LOGI("Set client_shut_down_flag.");
		}
	}
}

client_err_type_t check_lib_params(void)
{
	LOGD("");
	if(!cb_parameters)
	{
		LOGE("Null callback params found.");
		return CLIENT_ERR_NULL_PTR;
	}
	else if( (!cb_parameters->client_shut_down_flag) || 
	         (!cb_parameters->server_shut_down_flag) || 
			 (!cb_parameters->msg_handle_cb) ||
			 (!cb_parameters->busy_in_chat)
			)
	{
		LOGE("Any of cb_param is null.");
		return CLIENT_ERR_NULL_PTR;
	}
	LOGI("cb_params set successfully.");
	return CLIENT_SUCCESS;
}

client_err_type_t connect_to_server(void)
{
	LOGD("");
	print_bin_info();
    struct sockaddr_in serv_addr;

	if(CLIENT_SUCCESS!=check_lib_params())
	{
		return CLIENT_CB_PARAMS_NOT_SET;
	}

    struct sigaction sa = {0};
    sa.sa_handler = sig_int_handler;   
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;          
    sigaction(SIGINT, &sa, NULL);

    sock = socket(AF_INET, SOCK_STREAM, 0);
	LOGI("Got socket.");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)))
	{
		LOGE("[ connect ] failed.");
		return CONNECTION_FAILED;
	}
    LOGI("Connected to server.");

	msg_t temp_msg={0};
	client_err_type_t err = recv_with_timeout(sock,&temp_msg,sizeof(temp_msg),5);

	if(CLIENT_SUCCESS != err )
	{
		LOGE("Connection verification failed.");
		return CONNECTION_FAILED;
	}
	else
	{
		if( (MSG_CONN_ESTABLISH_REQ==temp_msg.msg_type) && (0==strcmp(temp_msg.msg_data.buffer,SERVER_UNIQUEUE_ID) ))
		{
			LOGI("Connection verified successfully.");
			msg_t send_node={0};
			send_node.msg_type=MSG_CONN_ESTABLISH_ACK;
			client_err_type_t err = send_msg_to_server(send_node);
			if(err!=CLIENT_SUCCESS) return CONNECTION_FAILED;
		}
		else
		{
			LOGE("Server key mismatched.");
			return CONNECTION_FAILED;
		}
	}
	return CLIENT_SUCCESS;
}

void set_my_name(char *name_to_set)
{
	LOGD("");
	if(NULL==name_to_set)
	{
		LOGE("Null ptr found");
		return;
	}

	msg_t send_msg={0};
	send_msg.to_client_id=MSG_SERVER;
	strcpy(send_msg.msg_data.buffer,name_to_set);
	send_msg.msg_type = MSG_SET_NAME_REQ_TYPE;
	client_err_type_t ret = send_msg_to_server(send_msg);
	if(CLIENT_SUCCESS!=ret)
		LOGE("set-name msg send : failed.");
	else
		LOGD("set-name msg send : success,name : %s.",name_to_set);
}

client_err_type_t send_msg_to_server(msg_t msg_to_send)
{
	LOGD("");
	if(sock==INVALID_FD)
	{
		LOGE("cliet is not connected to server.");
		return CLIENT_NOT_CONNECTED;
	}

	int ret = send(sock,&msg_to_send,sizeof(msg_to_send),0);
	if( (sizeof(msg_to_send)!=ret) || (-1==ret))
	{
		LOGE("Error in sending msg to server.");
		return CLIENT_MSG_SEND_ERR;
	}
	LOGI("msg send to server successfuly.");
	return CLIENT_SUCCESS;
}

void* io_thread(void* arg)
{
	LOGD("");
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
                LOGD("Poll interrupted by signal.");
                break;
            }
            LOGD("Error in poll.");
            break;
        }
		else if(ret==0)
		{
			// LOGD("Poll timeout.");
			continue;
		}

        if (fds[0].revents & POLLIN)
        {
            int bytes = recv(sock, &rx_msg, sizeof(rx_msg), 0);
            if (bytes > 0)
            {
				LOGD("%d bytes received from server.",bytes);
				if( (NULL!=cb_parameters) && (NULL!=cb_parameters->msg_handle_cb)) 
				{
					cb_parameters->msg_handle_cb(rx_msg);
					handle_rx_msg_lib(sock,rx_msg);
				}
			}
            else if (bytes == 0)
            {
                LOGI("Server shut-down detected.");
                LOGI("Setting client/server shutdow flag.");
				*cb_parameters->server_shut_down_flag = true;
				*cb_parameters->client_shut_down_flag = true;
                break;
            }
            else
            {
                LOGE("error in receive from server.");;
                break;
            }
        }
		if(fds[1].revents & POLLIN)
		{
			char buffer[MAX_MSG_LEN];
			int bytes = read(STDIN_FILENO, buffer, sizeof(buffer)-1);
            if (bytes > 0)
            {
				// LOGD("%d bytes received from stdin.",bytes);
				if((1==bytes) && (buffer[0]=='\n'))
				{
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
				LOGD("0 bytes received in poll of stdin.");
				continue;
            }
            else
            {
                LOGE("error in receive from STDIN.");
                break;
            }
		}
    }
	LOGI("Client io_thread is terminating.");
    return RETVAL(CLIENT_SUCCESS);
}

void get_client_list(void)
{
	LOGD("");
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
	LOGD("");
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int retval = select(sock + 1, &readfds, NULL, NULL, &tv);

    if (retval == -1) 
	{
        LOGE("Error in select.");
        return CLIENT_READ_ERROR;
    }
    else if (retval == 0) 
	{
        LOGI("Timeout waiting for data.");
        return CLIENT_READ_TIMEOUT;
    }
    else 
	{
        int ret = recv(sock, buf, len, 0);
		return CLIENT_SUCCESS;
    }
}

void set_lib_params(lib_params_t* params)
{
	if(!params)
		LOGE("NUll lib params found.");
	else
	{
		cb_parameters=params;
		LOGI("Lib params set successfully.");
	}
}

client_err_type_t chat_on(void)
{
	if(pthread_create(&io_thread_id,NULL,io_thread,NULL))
	{
		LOGE("Error in creating rx thread.");
		return CLIENT_ERR_THREAD_CREATE;
	}
	void* io_thread_ret_val ;
	LOGI("joiing io_thread to main thread.");
	
	pthread_join(io_thread_id,&io_thread_ret_val);
	LOGD("io_thread joined to main thread. [ %s ].",errTostr(GETVAL(io_thread_ret_val)) );
	return GETVAL(io_thread_ret_val);
}

void connect_with_client(char *name)
{
	if(!name) 
	{
		LOGE("Null ptr found.");
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
			LOGI("Setting conn_request_rx to true.");
			conn_request_rx = true;
			break;
		
		case MSG_CLIENT_ACCEPT_CONNECTION_ACK:
			if(conn_request_rx)
			{
				LOGI("We have connn-request and some-one has accepted our conn-request.");
			}
			LOGI("Setting conn_request_rx to false.");
			conn_request_rx = false;
			LOGI("Setting busy_in_chat to true.");
			*(cb_parameters->busy_in_chat)=true;
			break;

		case MSG_CLIENT_CHAT_READY:
			LOGI("Setting busy_in_chat to true.");
			*(cb_parameters->busy_in_chat)=true;
			break;

		case MSG_CLIENT_NO_MORE_FREE:
		case MSG_CLIENT_TERMINATION:
		case MSG_CLIENT_DISCONNECTED:
			LOGI("Setting conn_request_rx to false.");
			conn_request_rx = false;
			LOGI("Setting busy_in_chat to false.");
			*(cb_parameters->busy_in_chat)=false;
			break;
	}
}
