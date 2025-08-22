#include <stdio.h>
#include "server_mgmt.h"
#include "logger.h"

int main()
{
    srv_err_type ret = init_srv();
    if(ret!=SERVER_SUCC) return -1;
    LOGI("[ server ] init done\n");

    ret = wait_for_client_conn_and_accept();
    LOGE("[ server ] terminating, reason : %d \n",ret);
    return 0;
}