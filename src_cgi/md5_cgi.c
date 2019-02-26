/**
* Copyright: Copyright (c) 2019 angeltears-hyj, All right reserved.
* 
* @Functional description:md5上传
* @Author : angeltears-onter
* @Date : 19-2-23.
* @package : remote
*/


#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "make_log.h" //日志头文件
#include "util_cgi.h"
#include "deal_mysql.h"
#include "cfg.h"
#include "cJSON.h"
#include <sys/time.h>
#define MD5_LOG_MODULE       "cgi"
#define MD5_LOG_PROC         "md5"


//mysql 数据库配置信息 用户名， 密码， 数据库名称
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};
static char mysql_ip[30] = {0};
static unsigned int mysqlport = 0;


void read_cfg()
{
    //读取mysql数据库配置信息
    get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
    get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
    get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);
    get_cfg_value(CFG_PATH, "mysql", "ip", mysql_ip);
    char mysql_port[6] = {0};
    get_cfg_value(CFG_PATH, "mysql", "port", mysql_port);
    mysqlport = atoi(mysql_port);
    LOG(MD5_LOG_MODULE,MD5_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]\n", mysql_user, mysql_pwd, mysql_db);
}

//解析json包
int get_md5_info(char *buf, char *user, char *token, char *md5, char *filename)
{
    int ret = 0;
    cJSON* root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }
    //user
    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if(NULL == child1)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);
    cJSON* child2 = cJSON_GetObjectItem(root, "token");
    if(NULL == child2)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token , child2->valuestring);
    cJSON* child3 = cJSON_GetObjectItem(root, "md5");
    if(NULL == child3)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(md5, child3->valuestring);
    cJSON* child4 = cJSON_GetObjectItem(root, "filename");
    if(NULL == child4)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(filename, child4->valuestring);
END:
    if(root != NULL)
    {
        cJSON_Delete(root);//删除json对象
        root = NULL;
    }
    return ret;
}
/**
 * @fuction: md5秒传
 * @return：　0　success
 *          -1  error
 *          -2  file exist
 *          -3  fail
 */
int deal_md5(char *user, char *md5, char *filename)
{
    int ret = 0;
    int ret2 = 0;
    MYSQL *conn = NULL;
    char tmp[512] = {0};
    char sql_cmd[SQL_MAX_LEN] = {0};
    char *out = NULL;

    conn = msql_conn(mysql_user,mysql_pwd, mysql_db, mysql_ip, mysqlport);
    if (NULL == conn)
    {
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }
    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //查看是否有这个文件，没有秒传失败
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp);

    //拥有这个文件
    if (ret2 == 0)
    {
        int count = atoi(tmp);
        //查看此用户是否已经拥有此文件，如果存在说明文件已经上传，无需再上传
        sprintf(sql_cmd, "select * from user_file_list where md5 = '%s' and user = '%s' and filename = '%s'", md5, user, filename);
        ret2 = process_result_one(conn, sql_cmd, NULL);
        if (ret2 == 2)
        {
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "%s[filename:%s, md5:%s]已存在\n", user, filename, md5);
            ret = -2;
            goto END;
        }

        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", ++count, md5);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

        //2、user_file_list, 用户文件列表插入一条数据
        //当前时间戳
        struct timeval tv;
        struct tm* ptm;
        char time_str[128];

        //使用函数gettimeofday()函数来得到时间。它的精度可以达到微妙
        gettimeofday(&tv, NULL);
        ptm = localtime(&tv.tv_sec);//把从1970-1-1零点零分到当前时间系统所偏移的秒数时间转换为本地时间
        //strftime() 函数根据区域设置格式化本地时间/日期，函数的功能将时间格式化，或者说格式化一个时间字符串
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

        /*
        -- 用户文件列表
        -- user	文件所属用户
        -- md5 文件md5
        -- createtime 文件创建时间
        -- filename 文件名字
        -- shared_status 共享状态, 0为没有共享， 1为共享
        -- pv 文件下载量，默认值为0，下载一次加1
        */
        sprintf(sql_cmd, "insert into user_file_list(user, md5, createtime, filename, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)"
                ,user, md5, time_str, filename, 0, 0);
        if(mysql_query(conn, sql_cmd) != 0)
        {
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }
        //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
        sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
        LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "cmd : %s\n", sql_cmd);
        count = 0;
        ret2 = process_result_one(conn, sql_cmd, tmp); //指向sql语句
        if(ret2 == 1) //没有记录
        {
            //数据库插入此记录
            sprintf(sql_cmd, "insert into user_file_count (user, count) values('%s', %d)", user, 1);
        }
        else if(ret2 == 0)
        {
            //更新用户文件数量count字段
            count = atoi(tmp);
            sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count+1, user);
        }
        if(mysql_query(conn, sql_cmd) != 0)
        {
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "%s 操作失败： %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

    }
    else if(1 == ret2)//没有结果，秒传失败
    {
        ret = -3;//0秒传成功，-1出错，-2此用户已拥有此文件， -3秒传失败
        goto END;
    }
END:
    /*
    秒传文件：
        文件已存在：{"code":"005"}
        秒传成功：  {"code":"006"}
        秒传失败：  {"code":"007"}

    */
    if (ret == 0)
    {
        out = return_status("006");
    }
    else if (ret == -2)
    {
        out = return_status("005");
    }
    else if (ret == -3)
    {
        out = return_status("007");
    }

    if(out != NULL)
    {
        printf(out);
        free(out);
    }

    if(conn != NULL)
    {
        mysql_close(conn); //断开数据库连接
    }
    return ret;
}
int main()
{
    read_cfg();
    while(FCGI_Accept() >= 0)
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n\r\n");
        if ( contentLength == NULL)
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength);
        }

        if (len <= 0)
        {
            printf("No data from standard input.<p>\n");
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4 * 1024] = {0};
            int ret = 0;

            ret = fread(buf, 1, len, stdin);
            if (ret == 0)
            {
                LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "buf = %s\n", buf);

            //解析json中信息
            /*
             * {
                user:xxxx,
                token: xxxx,
                md5:xxx,
                fileName: xxx
               }
             */
            char user[128] = {0};
            char md5[256] = {0};
            char token[256] = {0};
            char filename[128] = {0};

            ret = get_md5_info(buf, user, token, md5, filename);
            if(ret != 0)
            {
                LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "get_md5_info() err\n");
                continue;
            }
            LOG(MD5_LOG_MODULE, MD5_LOG_PROC, "user = %s, token = %s, md5 = %s, filename = %s\n", user, token, md5, filename);

            ret = verify_token(user, token);
            if (ret == 0)
            {
                deal_md5(user, md5, filename);
            }
            else
            {
                char *out = return_status("111");
                if (NULL != out)
                {
                    printf(out);
                    free(out);
                }
            }
        }
    }
    return 0;
}