#ifndef CLIENT_LIB_H
#define CLIENT_LIB_H

#include <stdbool.h>
#include <pthread.h>
#include "chat_app_common.h"
#include "user_iterectaions.h"

#define SERVER_PORT 12345
#define SERVER_IP   "127.0.0.1"

typedef enum
{
    CLIENT_SUCCESS=0,
    CONNECTION_FAILED,
    CLIENT_NOT_CONNECTED,
    CLIENT_MSG_SEND_ERR,
    CLIENT_ERR_THREAD_CREATE,
    CLIENT_READ_ERROR,
    CLIENT_READ_TIMEOUT,
    CLIENT_CB_PARAMS_NOT_SET,
    CLIENT_ERR_NULL_PTR,
    CLIENT_MAX_ERR
}client_err_type_t;

typedef enum{
    CHAT_SUCCESS=0,
    CHAT_CLIENT_BUSY,
    CHAT_CLIENT_
}chat_client_err_t;

typedef struct
{
    bool *client_shut_down_flag;
    bool *server_shut_down_flag;
    bool *conn_request_rx;
    bool *busy_in_chat;
    char* connected_client_name;
    client_err_type_t (*msg_handle_cb)(msg_t rx_msg);
}lib_params_t;


client_err_type_t connect_to_server(void);
void set_my_name(char *name_to_set);
const char *errTostr(client_err_type_t err);
const char *msgTypeToStr(msg_type_t type);
void get_client_list(void);
void set_lib_params(lib_params_t* params);
client_err_type_t chat_on(void);
void connect_with_client(char *name);
client_err_type_t send_msg_to_server(msg_t msg_to_send);

#endif