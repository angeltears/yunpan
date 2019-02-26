/**
* Copyright: Copyright (c) 2019 angeltears-hyj, All right reserved.
* 
* @Functional description:登录验证cgi
* @Author : angeltears-onter
* @Date : 19-2-23.
* @package : remote
*/

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "make_log.h"
#include "util_cgi.h"
#include "deal_mysql.h"
#include "redis_op.h"
#include "cfg.h"
#include "cJSON.h"

#include "des.h"
#include "base64.h"
#include "md5.h"
#include <time.h>

#define LOGIN_LOG_MODULE "cgi"
#define LOGIN_LOG_PROC   "login"

int get_login_info(char *login_buf, char *user, char *pwd)
{
    int ret = 0;

    //解析json包
    cJSON *root = cJSON_Parse(login_buf);
    if (NULL == root)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "cJSON_Parse error");
        ret = -1;
        goto END;
    }
    //user
    cJSON * child1 = cJSON_GetObjectItem(root, "user");
    if (child1 == NULL)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "cJSON_GetObjectItem user error");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);
    //pwd
    cJSON* child2 = cJSON_GetObjectItem(root, "pwd");
    if (child2 == NULL)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "cJSON_GetObjectItem pwd error");
        ret = -1;
        goto END;
    }
    strcpy(pwd, child2->valuestring);
END:
    if (root != NULL)
    {
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

int check_user_pwd(const char * user, const char *pwd)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char mysql_user[256] = {0};
    char mysql_pwd[256] = {0};
    char mysql_db[256] = {0};
    char mysql_ip[16] = {0};
    char mysql_port_s[6] = {0};
    get_mysql_info(mysql_user, mysql_pwd, mysql_db, mysql_ip, mysql_port_s);
    unsigned int mysql_port = atoi(mysql_port_s);

    LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s\n", mysql_user, mysql_pwd, mysql_db);
    MYSQL *conn = msql_conn(mysql_user, mysql_pwd, mysql_db, mysql_ip, mysql_port);
    if (conn == NULL)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "msql_conn err\n");
        return -1;
    }

    //设置中文编码,主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //user->pwd
    sprintf(sql_cmd, "select password from user where name = \"%s\"", user);

    char tmp[PWD_LEN] = {0};

    process_result_one(conn, sql_cmd, tmp);
    if (strcmp(tmp, pwd) == 0)
    {
        ret = 0;
    }
    else
    {
        ret = -1;
    }

    mysql_close(conn);

    return ret;
}

int set_token(char *user, char *token)
{
    int ret = 0;
    redisContext *redis_conn = NULL;

    //redis　服务器ip 端口
    char redis_ip[16] = {0};
    char redis_port[6]={0};
    char redis_password[256] = {0};
    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    get_cfg_value(CFG_PATH, "redis", "password", redis_password);

    redis_conn = rop_connectdb(redis_ip,redis_port, redis_password);
    if (redis_conn == NULL)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "redis connected error\n");
        ret = -1;
        goto END;
    }

    //产生四位1000以内随机数据
    int rand_num[4] = {0};
    int i = 0;
    srand((unsigned int)time(NULL));
    for(i = 0; i < 4; ++i)
    {
        rand_num[i] = rand()%1000;//随机数
    }
    char tmp[1024] = {0};
    sprintf(tmp, "%s%d%d%d%d", user, rand_num[0], rand_num[1], rand_num[2], rand_num[3]);
    LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "tmp = %s\n", tmp);
    //加密
    char enc_tmp[1024*2] = {0};
    int enc_len = 0;
    ret = DesEnc((unsigned char *)tmp, strlen(tmp), (unsigned char *)enc_tmp, &enc_len);
    if(ret != 0)
    {
        LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "DesEnc error\n");
        ret = -1;
        goto END;
    }

    //to base64
    char base64[1024*3] = {0};
    base64_encode((const unsigned char *)enc_tmp, enc_len, base64); //base64编码
    LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "base64 = %s\n", base64);

    //to md5
    MD5_CTX md5;
    MD5Init(&md5);
    unsigned char decrypt[16];
    MD5Update(&md5, (unsigned char *)base64, strlen(base64) );
    MD5Final(&md5, decrypt);

    char str[100] = { 0 };
    for (i = 0; i < 16; i++)
    {
        sprintf(str, "%02x", decrypt[i]);
        strcat(token, str);
    }

    //redis保存　user:token键值对　有效时间24个小时
    ret = rop_setex_string(redis_conn, user, 86400, token);

    END:
    if(redis_conn != NULL)
    {
        rop_disconnect(redis_conn);
    }
    return ret;
}

void return_login_status(char *status_num, char *token)
{
    char *out = NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "code", status_num);
    cJSON_AddStringToObject(root, "token", token);
    out = cJSON_Print(root);
    cJSON_Delete(root);
    if(out != NULL)
    {
        printf(out); //给前端反馈信息
        free(out); //记得释放
    }
}

int main()
{
    while(FCGI_Accept() >= 0)
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        int len;
        char token[128] = {0};

        printf("Content-type: text/html\r\n\r\n");
        if (contentLength == NULL )
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength);
        }

        if (len <= 0) //没有登录信息
        {
            printf("No data from standard input.<p>\n");
            LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4*1024] = {0};
            int ret = 0;
            ret = fread(buf, 1, len, stdin);
            if(ret == 0)
            {
                LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "buf = %s\n", buf);

            // 获取登录用户的信息
            char user[512] = {0};
            char pwd[512] = {0};
            get_login_info(buf, user, pwd);
            LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "user = %s, pwd = %s\n", user, pwd);

            //登录判断.成功返回0,失败返回-1
            ret = check_user_pwd(user, pwd);
            if (ret == 0)
            {
                memset(token, 0, sizeof(token));
                ret = set_token(user, token);
                LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "user = %s, token = %s\n", user,token);
            }

            if(ret == 0)
            {
                //返回前端登陆情况， 000代表成功
                return_login_status("000", token);
            }
            else
            {
                //返回前端登陆情况， 001代表失败
                return_login_status("001", "fail");
            }
        }
    }

}