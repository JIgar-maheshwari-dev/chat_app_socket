#ifndef SRV_MGMT_H
#define SRV_MGMT_H
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include "server_queue.h"

#define MAX_LISTEN           20
#define SERVER_PORT          12345
#define MAX_CLIENT           20
#define MAX_CLIENT_NAME_LEN  64

#define MAX_RECV_BUFFER_LEN  2048

#define LOCK_CLIENT_DATA_MUTEX() do {       \
    LOGD("Locking client_data_mutex");      \
    pthread_mutex_lock(&client_data_mutex); \
} while(0)

#define UNLOCK_CLIENT_DATA_MUTEX() do {       \
    LOGD("Unlockig client_data_mutex");      \
    pthread_mutex_unlock(&client_data_mutex); \
} while(0)


typedef enum
{
    SERVER_SUCC=0,
    ERR_LIB_INIT,
    ERR_THREAD_CREATE,
    SERVER_TERMINATE_DETECTED,
    ERR_CONN_EST,
    ERR_MSG_SEND,
    ERR_CLIENT_CLOSED_CONN,
    ERR_RECV,
    ERR_ADD_NODE_FAILED,
    ERR_READ_TIMEOUT
}srv_err_type;

typedef struct 
{
    client_chat_status_t chat_status;
    int fd;
    int to_fd;
    pthread_t thread_id;
    char name[MAX_CLIENT_NAME_LEN];
}client_data_t;

typedef struct client_node_t {
    client_data_t data;
    struct client_node_t* next;
} client_node_t;

srv_err_type init_srv(void);
srv_err_type wait_for_client_conn_and_accept(void);

#endif