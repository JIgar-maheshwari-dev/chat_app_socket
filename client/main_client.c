#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include "logger.h"
#include "client_lib.h"

client_err_type_t msg_handle_cb(msg_t rx_msg);

bool server_shut_down_flag = false;
bool client_shut_down_flag = false;
bool busy_in_chat          = false;
char connected_client_name[MAX_CLIENT_NAME_LEN]=UNDEF_NAME;

lib_params_t params_send_to_lib={
	.client_shut_down_flag = &client_shut_down_flag,
	.server_shut_down_flag = &server_shut_down_flag,
	.busy_in_chat          = &busy_in_chat,
	.msg_handle_cb         = msg_handle_cb,
	.connected_client_name = connected_client_name
};

int main(int argc,char** argv)
{
	LOGD("main started.");
	set_lib_params(&params_send_to_lib);

	client_err_type_t ret = connect_to_server();
	if(ret!= CLIENT_SUCCESS)
	{
		printf("Connect to server failed.\n");
		LOGE("Error : %s .",errTostr(ret));
		return -1;
	}

	if(argc==2)
	{
		set_my_name(argv[1]);
	}

	show_help();

	ret = chat_on();
	printf("Closing the client.\n");
}

client_err_type_t msg_handle_cb(msg_t rx_msg)
{
	switch (rx_msg.msg_type)
	{
		case MSG_SET_NAME_ACK_TYPE:
			printf("Name set successfully\n");
			break;
	
		case MSG_SET_NAME_NACK_TYPE:
			printf("Name not set.\n");
			break;

		case MSG_GET_CLIENT_LIST_TYPE:
			printf("Available clients on server : %s\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CONNECTION_REQ_RX:
			printf("[ %s ] Wants to connect with you.\n",rx_msg.msg_data.buffer);
			printf("Enter \"yes\" or \"no\" to accept or decline connection request.\n");
			break;

		case MSG_CLIENT_STATUS_REQ_PENDING:
			printf("Cannot connect with [ %s ], processing other connection request.\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CLIENT_ACCEPT_CONNECTION_ACK:
			printf("[ %s ] accepted your connection request.\n",rx_msg.msg_data.buffer);
			printf("You can chat now.\n");
			break;

		case MSG_CLIENT_NO_MORE_FREE:
			printf("Cannot connect to [ %s ], no more available to chat.\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CLIENT_TERMINATION:
			printf("Client terminated : %s.\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CLIENT_DISCONNECTED:
			printf("Client disconnected : [ %s ].\n",rx_msg.msg_data.buffer);
			break;
		
		case MSG_CLIENT_CHAT_READY:
			printf("Ready to chat with : [ %s ].\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CLIENT_DECLINE_CONNECTION_ACK:
			printf("[ %s ] Declined your connection request.\n",rx_msg.msg_data.buffer);
			break;
		
		case MSG_CLIENT_FREE:
			printf("Requested client is free to chat, Waitig for request approval.\n");
			break;

		case MSG_CLIENT_BUSY:
			printf("[ %s ] is now busy in chat with someone else, cannot connect to you.\n",rx_msg.msg_data.buffer);
			break;

		case MSG_ATTEMPT_TO_CONNECT_TO_SELF:
			printf("Cannot connect to our-self.\n");
			break;

		case MSG_CLIENT_NOT_EXIST:
			printf("No client with name : %s.\n",rx_msg.msg_data.buffer);
			break;

		case MSG_CLIENT_RX_TYPE:
			printf("[ %s ] : [ %s ]\n",connected_client_name, rx_msg.msg_data.buffer);
			break;

		default:
			printf("msg rx , msg_type : %s, msg_data : %s\n",msgTypeToStr(rx_msg.msg_type),rx_msg.msg_data.buffer);
			break;
	}
	return CLIENT_SUCCESS;
}
