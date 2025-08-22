#include "server_mgmt.h"
#include "server_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

client_node_t* client_list=NULL;
uint8_t total_available_clients=0;

const char* queueErrStr[]={
    "UNDEFINED_QUEUE_ERR"
    "SERVER_QUEUE_SUCC",
    "ERR_NULL_PTR",
    "ERR_MALLOC_FAILED",
    "ERR_LIST_EMPTY",
    "ERR_SERVER_QUEUE_FULL",
    "ERR_INVALID_ID",
    "ERR_NODE_NOT_FOUND",
    "ERR_NO_CLIENT_ID_FREE",
    "ERR_NAME_NOT_SET",
    "ERR_QUEUE_MAX"
};

const char* chat_status_str[]={
    "INVALID_CHAT_STATUS",
    "CHAT_STATUS_FREE",
    "CHAT_STATUS_BUSY",
    "CHAT_STATUS_CLIENT_NOT_FOUND",
    "CHAT_STATUS_REQ_PENDING"
};

srv_queue_err_type_t add_client_node_to_queue(int* fd)
{
    if(total_available_clients>MAX_CLIENT) 
    {
        printf("Max client limit reached, cannot connect more client now \
                [ total_available_clients= %d ] !!!\n",total_available_clients);
        return ERR_SERVER_QUEUE_FULL;
    }

    client_node_t *new_node = malloc(sizeof(client_node_t));
    if (NULL == new_node)
    {
        printf("[ add_node ] malloc failed, err: %s, err_no: %d\n", strerror(errno), errno);
        return ERR_MALLOC_FAILED;
    }

    new_node->data.fd = *fd;
    new_node->next = NULL;
    new_node->data.to_fd = INVALID_FD;
    new_node->data.chat_status = CHAT_STATUS_FREE;
    new_node->data.thread_id = pthread_self();

    memset(new_node->data.name,'\0',MAX_CLIENT_NAME_LEN);
    sprintf(new_node->data.name,"temp_client_name_%d",new_node->data.fd);

    if (NULL == client_list) {
        client_list = new_node;
    } else {
        client_node_t *head = client_list;
        while (NULL != head->next)
            head = head->next;
        head->next = new_node;
    }

    total_available_clients++;
    printf("[ add_node ] Added client with fd: %d\n", new_node->data.fd);
    return SERVER_QUEUE_SUCC;
}

srv_queue_err_type_t remove_client_node_from_queue_by_fd(int fd)
{
    srv_queue_err_type_t ret = ERR_NODE_NOT_FOUND;
    if (NULL == client_list)
    {
        printf("[ remove_nonde ] queue is empty, cannot remove : %d\n", fd);
        return ERR_LIST_EMPTY;
    }
    else if(INVALID_FD == fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
    }
    else
    {
        if (client_list->data.fd == fd) 
        {
            client_node_t* temp = client_list;
            printf("closig fd : %d.\n",temp->data.fd);
            close(temp->data.fd);
            client_list = client_list->next;
            pthread_detach(temp->data.thread_id);
            free(temp);
            printf("[ remove_nonde ] removed client with fd: %d\n", fd);
            total_available_clients--;
            ret = SERVER_QUEUE_SUCC;
        }
        else
        {
            client_node_t* prev = client_list;
            client_node_t* curr = client_list->next;
        
            while (curr != NULL && curr->data.fd != fd) 
            {
                prev = curr;
                curr = curr->next;
            }
            if (curr == NULL) 
            {
                printf("[ remove_nonde ] client not found with fd : %d\n", fd);
                ret = ERR_NODE_NOT_FOUND;
            }
            else
            {
                prev->next = curr->next;
                printf("closig fd : %d.\n",curr->data.fd);
                close(curr->data.fd);
                pthread_detach(curr->data.thread_id);
                free(curr);
                total_available_clients--;
                printf("[ remove_nonde ] removed client with fd: %d\n", fd);
                ret = SERVER_QUEUE_SUCC; 
            }
        }
    }
    return ret;
}

const char * queueErrToStr(srv_queue_err_type_t err)
{
    if(err>=ERR_QUEUE_MAX) return queueErrStr[0];
    return queueErrStr[err+1];
}

srv_queue_err_type_t set_name_of_client_by_client_fd(int fd,char* name)
{
    srv_queue_err_type_t ret_val= ERR_NAME_NOT_SET;
    if(!client_list) 
    {
        printf("Client data list not init yet\n");
        ret_val= ERR_LIST_EMPTY;
    }
    else if(!name)
    {
        printf("[ %s ] Name cannot be a null ptr\n",__FUNCTION__);
        ret_val = ERR_NULL_PTR;
    }
    else if (INVALID_FD == fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
    }
    else
    {
        client_node_t* temp_node = client_list;

        while( (NULL != temp_node) && (fd != temp_node->data.fd) ) 
            temp_node=temp_node->next;
        
        if(NULL == temp_node) 
        {
            printf("[ %s ] No client found with fd : %d\n",__FUNCTION__,fd);
        }
        else
        {
            strncpy(temp_node->data.name,name,MAX_CLIENT_NAME_LEN-1);
            temp_node->data.name[MAX_CLIENT_NAME_LEN - 1] = '\0';
            ret_val = SERVER_QUEUE_SUCC;  
        }
    }
    return ret_val;
}

char* get_client_name_by_fd(int sock)
{
    if(!client_list) 
    {
        printf("Client data list not init yet\n");
        return UNDEF_NAME;
    }
    else if(INVALID_FD==sock)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
        return UNDEF_NAME;
    }

    client_node_t* temp_node = client_list;

    while( (NULL != temp_node) && (sock != temp_node->data.fd) ) 
        temp_node=temp_node->next;
    
    if(NULL == temp_node)
    {
        printf("[ %s ] No client found with fd : %d\n",__FUNCTION__,sock);
        return UNDEF_NAME;
    } 
    return temp_node->data.name;
}

int get_client_fd_by_name(char *name)
{
    if(!client_list) 
    {
        printf("Client data list not init yet\n");
        return INVALID_FD;
    }
    else if(!name)
    {
        printf("[ %s ] Name null ptr found.\n",__FUNCTION__);
        return INVALID_FD;
    }

    client_node_t* temp_node = client_list;

    while( (NULL != temp_node) && (0!=strcmp(name,temp_node->data.name)) ) 
        temp_node=temp_node->next;
    
    if(NULL == temp_node)
    {
        printf("[ %s ] No client found with name : %s.\n",__FUNCTION__,name);
        return INVALID_FD;
    } 
    return temp_node->data.fd;
}


srv_queue_err_type_t get_client_list(char *list)
{
    srv_queue_err_type_t ret_val = ERR_LIST_EMPTY;
    if(!client_list) 
    {
        printf("Client data list not init yet\n");
    }
    else if(!list)
    {
        printf("[ %s ] NULL Data ptr found.\n",__FUNCTION__);
        ret_val = ERR_NULL_PTR;
    }
    else
    {
        size_t list_len = 0;
        size_t list_capacity = MAX_MSG_LEN; 
        client_node_t* temp_node = client_list;
        ret_val = SERVER_QUEUE_SUCC;
        
        // printf("list_capacity : %ld\n",list_capacity);

        while( (NULL != temp_node) )
        {
            size_t name_len = strlen(temp_node->data.name);
            // printf("name_len : %ld\n",name_len);
            // printf("list_len : %ld\n",list_len);

            if (list_len + name_len + 1 >= list_capacity) {
                printf("name_len : %ld\n",name_len);
                printf("list_len : %ld\n",list_len);
                printf("List buffer too small, stopping append.\n");
                break;
            }
        
            strncat(list, temp_node->data.name, list_capacity - list_len - 1);
            list_len = strlen(list);
        
            if (list_len + 1 < list_capacity) {
                strncat(list, " ", list_capacity - list_len - 1);
                list_len++;
            }
        
            temp_node = temp_node->next;   
        }
              
    }
    return ret_val;
}

name_find_type_t check_client_with_same_name_exist_or_not(char* name)
{
    if(!name) return NAME_FIND_ERR;
    if(!client_list) return NAME_FIND_ERR;

    client_node_t* temp = client_list;
    while( (temp) && (strcmp(name,temp->data.name)) )
        temp = temp->next;

    if(!temp)
        return NAME_NOT_EXIST;
    else
        return NAME_EXISTS;
}

void free_all_client_nodes(void)
{
    client_node_t* temp;
    while(client_list)
    {
        temp = client_list->next;
        free(client_list);
        client_list = temp;
    }
    printf("Freed-up all nodes memory.\n");
}

void join_all_client_threads(void)
{
    client_node_t* temp=client_list;
    while(temp)
    {
        printf("Joiinng %lu to main.\n",temp->data.thread_id);
        pthread_join(temp->data.thread_id,NULL);
        temp = temp->next;
    }
    printf("All threads are joined.\n");
}

client_chat_status_t get_client_chatting_status_by_name(char* name)
{
    client_chat_status_t ret = CHAT_STATUS_CLIENT_NOT_FOUND;

    if(!name) return ret;

    client_node_t* temp = client_list;
    while(temp && (strcmp(name, temp->data.name))) 
        temp = temp->next;

    if(temp) 
    {
        printf("[ %s ] Client chat status of %s is : %s\n",__FUNCTION__,temp->data.name, chat_status_to_str(temp->data.chat_status));
        ret = temp->data.chat_status;
    }
    else
    {
        printf("No client found with name : %s\n",name);
    }
    return ret;
}

client_chat_status_t get_client_chatting_status_by_fd(int fd)
{
    client_chat_status_t ret = CHAT_STATUS_CLIENT_NOT_FOUND;

    if(INVALID_FD==fd) return ret;

    client_node_t* temp = client_list;
    while(temp && (fd!=temp->data.fd)) 
        temp = temp->next;

    if(temp) 
    {
        printf("[ %s ] Client chat status of %s is : %s\n",__FUNCTION__,temp->data.name, chat_status_to_str(temp->data.chat_status));
        ret = temp->data.chat_status;
    }
    else
    {
        printf("No client found with fd : %d\n",fd);
    }
    return ret;
}


chat_err_t set_client_chatting_status_by_fd(int fd,client_chat_status_t chat_status)
{
    chat_err_t ret = ERR_CHAT_CLIENT_NOT_FOUND;

    if(chat_status >= CHAT_STATUS_MAX) 
    {
        ret = ERR_CHAT_INVALID_CHAT_STATUS;
    }
    else if(INVALID_FD == fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
    }
    else
    {
        client_node_t* temp = client_list;
        while(temp && (!(fd == temp->data.fd))) 
            temp = temp->next;
    
        if(temp) 
        {
            printf("Setting chat status as : %s of with name : %s.\n",chat_status_to_str(chat_status),temp->data.name);
            temp->data.chat_status = chat_status;
            ret = CHAT_SUCCESS;
        }
        else
        {
            printf("No client found with id : %d\n",fd);
        }
    }
    return ret;
}

chat_err_t set_to_fd_by_fd(int my_fd,int to_fd)
{
    // if((INVALID_FD==my_fd)||(INVALID_FD==to_fd))
    // {
    //     printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
    //     return ERR_CHAT_CLIENT_NOT_FOUND;
    // }
    client_node_t* temp = client_list;
    while(temp && (my_fd != temp->data.fd))
        temp = temp->next;

    if(temp) 
    {
        // printf("Setting to_fd of : %s(%d) to : %d.\n",temp->data.name,temp->data.fd,to_fd);
        temp->data.to_fd = to_fd;
        printf("set : to fd of %d is %d.\n",temp->data.fd,temp->data.to_fd);

        return CHAT_SUCCESS;
    }
    else
    {
        printf("No client found with fd : %d\n",my_fd);
        return ERR_CHAT_CLIENT_NOT_FOUND;
    }
}

int get_conn_fd_by_fd(int my_fd)
{
    printf("[ %s ] Entered.\n",__FUNCTION__);
    if(INVALID_FD==my_fd)
    {
        printf("[ %s ] Invalid fd found.\n",__FUNCTION__);
        return INVALID_FD;
    }

    client_node_t* temp = client_list;
    while(temp && (!(my_fd == temp->data.fd))) 
        temp = temp->next;

    if(temp) 
    {
        printf("Found client with fd : %d, name : %s. conn_fd : %d\n",temp->data.fd,temp->data.name,temp->data.to_fd);
        return temp->data.to_fd;
    }
    else
    {
        printf("No client found with fd : %d\n",my_fd);
        return INVALID_FD;
    }
}


const char *chat_status_to_str(client_chat_status_t c )
{
    if(c >= CHAT_STATUS_MAX) return chat_status_str[0];
    return chat_status_str[c+1];
}