#ifndef SERVER_QUEUE_H
#define SERVER_QUEUE_H

#include "chat_app_common.h"

#define MAX_QUEUE_LEN        20
#define MAIN_MSG_QUEUE_INDEX -1
#define MAX_CLIENT_ID 128

typedef enum{
    SERVER_QUEUE_SUCC=0,
    ERR_NULL_PTR,
    ERR_MALLOC_FAILED,
    ERR_LIST_EMPTY,
    ERR_SERVER_QUEUE_FULL,
    ERR_INVALID_ID,
    ERR_NODE_NOT_FOUND,
    ERR_NO_CLIENT_ID_FREE,
    ERR_NAME_NOT_SET,
    ERR_QUEUE_MAX,
}srv_queue_err_type_t;

typedef enum{
    CHAT_STATUS_FREE,
    CHAT_STATUS_BUSY,
    CHAT_STATUS_CLIENT_NOT_FOUND,
    CHAT_STATUS_REQ_PENDING,
    CHAT_STATUS_MAX
}client_chat_status_t;

typedef enum{
    CHAT_SUCCESS=0,
    ERR_CHAT_CLIENT_NOT_FOUND,
    ERR_CHAT_INVALID_CHAT_STATUS,
    ERR_CHAT_NULL_PTR
}chat_err_t;

typedef enum{
    NAME_EXISTS=0,
    NAME_NOT_EXIST,
    NAME_FIND_ERR
}name_find_type_t;

srv_queue_err_type_t add_client_node_to_queue(int* fd);
srv_queue_err_type_t remove_client_node_from_queue_by_fd(int fd);

const char * queueErrToStr(srv_queue_err_type_t err);
// char* get_client_name_by_client_fd(uint8_t c_id);

char* get_client_name_by_fd(int sock);
int get_client_fd_by_name(char *name);

srv_queue_err_type_t set_name_of_client_by_client_fd(int fd,char* name);

srv_queue_err_type_t get_client_list(char *list);
name_find_type_t check_client_with_same_name_exist_or_not(char* name);
void free_all_client_nodes(void);
void join_all_client_threads(void);

chat_err_t set_client_chatting_status_by_fd(int fd,client_chat_status_t chat_status);
client_chat_status_t get_client_chatting_status_by_name(char* name);
client_chat_status_t get_client_chatting_status_by_fd(int fd);
int get_conn_fd_by_fd(int my_fd);

const char *chat_status_to_str(client_chat_status_t c );
chat_err_t set_to_fd_by_fd(int my_fd,int to_fd);

#endif