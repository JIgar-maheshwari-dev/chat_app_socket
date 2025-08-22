#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "user_iterectaions.h"

typedef enum{
    CMD_TYPE_GET_LIST=0,
    CMD_TYPE_CONNECT,
    CMD_TYPE_DISCONNECT,
    CMD_TYPE_SET_NAME,
    CMD_TYPE_PRINT_HELP,
    CMD_TYPE_CLEAR_SCREEN,
    CMD_TYPE_MAX_CMD
}cmd_type_t;

#define GET_LIST_CMD      "get_list"
#define CONNECT_CMD       "connect"
#define SET_NAME_CMD      "set_name"
#define PRINT_HELP_CMD    "help"
#define CLEAR_SCREEN_CMD  "clear"

#define REQ_ACCEPT_STR   "yes"
#define REQ_DECLINE_STR  "no"

const char* cmd_list[]={
    GET_LIST_CMD,
    CONNECT_CMD,
    DISCONNECT_CMD,
    SET_NAME_CMD,
    PRINT_HELP_CMD,
    CLEAR_SCREEN_CMD
};

extern bool conn_request_rx;
extern lib_params_t* cb_parameters;
cmd_type_t get_cmd_id_by_name(char *cmd_name);
void  handle_send_msg_to_client(char *send_msg_str);

void show_help(void)
{
    printf("cmd : [ %s] : To get list off all connected client to the server (including you).\n",GET_LIST_CMD);
    printf("cmd : [ %s] : To set your name as you wish.\n",SET_NAME_CMD);
    printf("cmd : [ %s client_name ]: will connect you to the client named \"client_name\".\n",CONNECT_CMD);
    printf("cmd : [ %s ] : To disconnect from the client you previously connected.\n",DISCONNECT_CMD);
    printf("cmd : [ %s ] : To print the help and usage of all commands.\n",PRINT_HELP_CMD);
    printf("cmd : [ %s ] : To clear the screen.\n",CLEAR_SCREEN_CMD);
}

void process_send_msg(char *send_msg_buffer)
{
    if(!send_msg_buffer) 
    {
        printf("[ %s ] Null ptr found.\n",__FUNCTION__);
        return;
    }

    if(*(cb_parameters->busy_in_chat))
    {
        handle_send_msg_to_client(send_msg_buffer);
        return;
    }

    // printf("Processed %s msg.\n",send_msg_buffer);

    msg_t conn_response_msg={0};

    if(conn_request_rx && (0==strcmp(send_msg_buffer,REQ_ACCEPT_STR)))
    {
        conn_request_rx = false;
        conn_response_msg.msg_type=MSG_CLIENT_ACCEPT_CONNECTION;
        send_msg_to_server(conn_response_msg);
        printf("sending : Connection request accept response.\n");
        return;
    }
    else if(conn_request_rx && (0==strcmp(send_msg_buffer,REQ_DECLINE_STR)))
    {
        conn_request_rx = false;
        conn_response_msg.msg_type=MSG_CLIENT_DECLINE_CONNECTION;
        send_msg_to_server(conn_response_msg);
        printf("sending : Connection request decline response.\n");
        return;
    }
    else if( conn_request_rx )
    {
        printf("Please enter \"yes\" or \"no\" to accept or decline connection request.\n");
        return;
    }

    cmd_type_t cmd = get_cmd_id_by_name(send_msg_buffer);
    if(cmd >= CMD_TYPE_MAX_CMD)
    {
        printf("[ %s ] is not recognised as valid command.\n",send_msg_buffer);
        return;
    }
    switch(cmd)
    {
        case CMD_TYPE_GET_LIST:
            get_client_list();
            break;  

        case  CMD_TYPE_CONNECT:
        {
            char *cmd_str = strtok(send_msg_buffer," ");
            char *name    = strtok(NULL," ");

            if(name) 
            {
                printf("Setting name to : %s .\n",name);
                connect_with_client(name);
            }
            else 
            {
                printf("No client name provided to connect.\n");
            }
        }
        break;

        case CMD_TYPE_DISCONNECT:
            break;

        case CMD_TYPE_SET_NAME:
        {
            char *cmd_str = strtok(send_msg_buffer," ");
            char *name    = strtok(NULL," ");

            if(name) 
            {
                printf("Setting name to : %s .\n",name);
                set_my_name(name);
            }
            else 
            {
                printf("No name provided\n");
            }
        }
        break;

        case CMD_TYPE_PRINT_HELP:
            show_help();
            break;
        
        case CMD_TYPE_CLEAR_SCREEN:
        {
            system("clear");
            show_help();
        }
        break;
    }
}

cmd_type_t get_cmd_id_by_name(char *cmd_name)
{
    if(!cmd_name) return CMD_TYPE_MAX_CMD;

    cmd_type_t cmd = CMD_TYPE_MAX_CMD;
    int i=0;
    for(i=0;i<CMD_TYPE_MAX_CMD;i++)
    {
        if( 0 == strcmp(cmd_name,cmd_list[i]) )
        {
            cmd=i;
            break;
        }
        if( CMD_TYPE_SET_NAME==i || CMD_TYPE_CONNECT==i )
        {
            if( 0 == strncmp(cmd_name,cmd_list[i],strlen(cmd_list[i])) )
            {
                cmd=i;
                break;
            }    
        }
    }
    return cmd;
}

void  handle_send_msg_to_client(char *send_msg_str)
{
    if(!send_msg_str)
    {
        printf("[ %s ] Null ptr found.\n",__FUNCTION__);
        return;
    }

    msg_t send_msg={0};
    send_msg.msg_type=MSG_CLIENT_TX_TYPE;
    strcpy(send_msg.msg_data.buffer,send_msg_str);
    send_msg_to_server(send_msg);

    if(0==strcmp(send_msg_str,DISCONNECT_CMD))
    {
        *(cb_parameters->busy_in_chat) = false;
    }

    printf("msg send, chat communication.\n");
}
