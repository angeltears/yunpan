/**
* Copyright: Copyright (c) 2019 angeltears-hyj, All right reserved.
* 
* @Functional description:上传文件
* @Author : angeltears-onter
* @Date : 19-2-23.
* @package : remote
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include "deal_mysql.h"
#include "fcgi_stdio.h"
#include "make_log.h" //日志头文件
#include "cfg.h"
#include "util_cgi.h" //cgi后台通用接口，trim_space(), memstr()

#define UPLOAD_LOG_MODULE "cgi"
#define UPLOAD_LOG_PROC   "upload"

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
    LOG(UPLOAD_LOG_MODULE,UPLOAD_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]\n", mysql_user, mysql_pwd, mysql_db);
}
/**
 * @brief  解析上传的post数据 保存到本地临时路径
 *         同时得到文件上传者、文件名称、文件大小
 *
 * @param len       (in)    post数据的长度
 * @param user      (out)   文件上传者
 * @param file_name (out)   文件的文件名
 * @param md5       (out)   文件的MD5码
 * @param p_size    (out)   文件大小
 *
 * @returns
 *          0 succ, -1 fail
 */
int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size)
{
    int ret = 0;
    char *file_buf = NULL;
    char *begin = NULL;
    char *p, *q, *k;

    char content_text[TEMP_BUF_MAX_LEN] = {0}; //文件头部信息
    char boundary[TEMP_BUF_MAX_LEN] = {0}; //分界线信息
    file_buf = (char *)malloc(len);
    if (file_buf == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "malloc error! file size is to big!!!!\n");
        return -1;
    }
    int ret2 = fread(file_buf, 1, len, stdin);
    if (ret2 == 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fread(file_buf, 1, len, stdin) err\n");
        ret = -1;
        goto END;
    }
    //开始处理前端发送过来post数据
    begin = file_buf;
    p = begin;
    /*
   ------WebKitFormBoundary88asdgewtgewx\r\n
   Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
   Content-Type: application/octet-stream\r\n
   \r\n
   真正的文件内容\r\n
   ------WebKitFormBoundary88asdgewtgewx
   */
    p = strstr(begin, "\r\n");
    if (p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"wrong no boundary!\n");
        ret = -1;
        goto END;
    }
    //拷贝分界线
    strncpy(boundary, begin, p-begin);
    boundary[p-begin] = '\0';   //字符串结束符
    p += 2;//\r\n
    if(p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"ERROR: get context text error, no filename?\n");
        ret = -1;
        goto END;
    }
    len -= p-begin;
    begin = p;
    //Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    p = strstr(begin, "\r\n");
    if(p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"ERROR: get context text error, no filename?\n");
        ret = -1;
        goto END;
    }
    strncpy(content_text, begin, p-begin);
    content_text[p-begin] = '\0';
    p += 2;//\r\n
    len -= (p-begin);

    //get user
    //Content-Disposition: form-data; user="mike"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //                                ↑
    q = begin;
    q = strstr(q,"user=");
    q += strlen("user=");
    q++;
    k = strchr(q, '"');
    strncpy(user, q, k-q);
    user[k-q] = '\0';
    //去掉一个字符串两边的空白字符
    trim_space(user);   //util_cgi.h

    //get filename
    begin = k;
    q = begin;
    q = strstr(begin, "filename=");
    q += strlen("filename=");
    q++;    //跳过第一个"
    k = strchr(q, '"');
    strncpy(filename, q, k-q);  //拷贝文件名
    filename[k-q] = '\0';

    trim_space(filename);   //util_cgi.h

    //get md5
    begin = k;
    q = begin;
    q = strstr(begin, "md5=");
    q += strlen("md5=");
    q++;    //跳过第一个"
    k = strchr(q, '"');
    strncpy(md5, q, k-q);   //拷贝文件名
    md5[k-q] = '\0';
    trim_space(md5);    //util_cgi.h

    //get size
    begin = k;
    q = begin;
    q = strstr(begin, "size=");

    //"; size=10240\r\n
    //        ↑
    q += strlen("size=");

    //"; size=10240\r\n
    //             ↑
    k = strstr(q, "\r\n");
    char tmp[256] = {0};
    strncpy(tmp, q, k-q);   //内容
    tmp[k-q] = '\0';

    *p_size = strtol(tmp, NULL, 10); //字符串转long

    begin = p;
    p = strstr(begin, "\r\n");
    p += 4;//\r\n\r\n
    len -= (p-begin);
    begin = p;
    p = memstr(begin, len, boundary);//util_cgi.h， 找文件结尾
    if (p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "memstr(begin, len, boundary) error\n");
        ret = -1;
        goto END;
    }
    else
    {
        p = p - 2;//\r\n
    }
    int fd = 0;
    fd = open(filename, O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"open %s error\n", filename);
        ret = -1;
        goto END;
    }
    ftruncate(fd, (p-begin));
    write(fd, begin, (p-begin));
    close(fd);

END:
    free(file_buf);
    return ret;
}

/**
 * @brief  将一个本地文件上传到 后台分布式文件系统中
 *
 * @param filename  (in) 本地文件的路径
 * @param fileid    (out)得到上传之后的文件ID路径
 *
 * @returns
 *      0 succ, -1 fail
 */

int upload_to_dstorage(char *filename, char *fileid)
{
    int ret = 0;
    pid_t pid;
    int fd[2];
    if (pipe(fd) < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"pip error\n");
        ret = -1;
        goto END;
    }
    pid = fork();
    if (pid < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"fork error\n");
        ret = -1;
        goto END;
    }

    if (pid == 0)
    {
        //关闭读端
        close(fd[0]);

        dup2(fd[1], STDOUT_FILENO);

        char fdfs_cli_conf_path[256] = {0};
        get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
        execlp("fdfs_upload_file", "fdfs_upload_file", fdfs_cli_conf_path, filename, NULL);
        //执行失败
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp fdfs_upload_file error\n");

        close(fd[1]);
    }
    else
    {
        //关闭写端
        close(fd[1]);
        //从管道中去读数据
        read(fd[0], fileid, TEMP_BUF_MAX_LEN);
        //去掉一个字符串两边的空白字符
        trim_space(fileid);
        if (strlen(fileid) == 0)
        {
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"[upload FAILED!]\n");
            ret = -1;
            goto END;
        }
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get [%s] succ!\n", fileid);
        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);
    }
END:
    return ret;
}
/**
 * @brief  封装文件存储在分布式系统中的 完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns
 *      0 succ, -1 fail
 */
int make_file_url(char *fileid, char *fdfs_file_url)
{
    int ret = 0;
    char *p = NULL;
    char *q = NULL;
    char *k = NULL;

    char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_host_name[HOST_NAME_LEN] = {0};

    pid_t pid;
    int fd[2];
    //无名管道的创建
    if (pipe(fd) < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "pip error\n");
        ret = -1;
        goto END;
    }
    //创建进程
    pid = fork();
    if (pid < 0)//进程创建失败
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"fork error\n");
        ret = -1;
        goto END;
    }

    if (pid == 0)
    {
        close(fd[0]);
        //将标准输出 重定向 写管道
        dup2(fd[1], STDOUT_FILENO); //dup2(fd[1], 1);
        //读取fdfs client 配置文件的路径
        char fdfs_cli_conf_path[256] = {0};
        get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
        execlp("fdfs_file_info", "fdfs_file_info", fdfs_cli_conf_path, fileid, NULL);

        //执行失败
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp fdfs_file_info error\n");

        close(fd[1]);
    }
    else
    {
        //关闭写端
        close(fd[1]);

        //从管道中去读数据
        read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);
        //LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get file_ip [%s] succ\n", fdfs_file_stat_buf);

        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);
        p = strstr(fdfs_file_stat_buf, "source ip address: ");
        q = p + strlen("source ip address: ");
        k = strstr(q, "\n");
        strncpy(fdfs_file_host_name, q, k-q);
        fdfs_file_host_name[k-q] = '\0';

        char storage_web_server_port[20] = {0};
        char storage_web_server_ip[30] = {0};
        get_cfg_value(CFG_PATH, "storage_web_server", "port", storage_web_server_port);
        strcat(fdfs_file_url, "http://");
//        strcat(fdfs_file_url, fdfs_file_host_name);
        get_cfg_value(CFG_PATH, "storage_web_server", "ip", storage_web_server_ip);
        strcat(fdfs_file_url,storage_web_server_ip);
        strcat(fdfs_file_url, ":");
        strcat(fdfs_file_url, storage_web_server_port);
        strcat(fdfs_file_url, "/");
        strcat(fdfs_file_url, fileid);

        //printf("[%s]\n", fdfs_file_url);
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "file url is: %s\n", fdfs_file_url);
    }
END:
    return ret;
}

int store_fileinfo_to_mysql(char *user, char *filename, char *md5, long size, char *fileid, char *fdfs_file_url)
{
    int ret = 0;
    MYSQL *conn = NULL; //数据库连接句柄

    time_t now;
    char create_time[TIME_STRING_LEN];
    char suffix[SUFFIX_LEN];
    char sql_cmd[SQL_MAX_LEN] = {0};

    conn = msql_conn(mysql_user, mysql_pwd, mysql_db, mysql_ip, mysqlport);
    if (conn == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "msql_conn connect err\n");
        ret = -1;
        goto END;
    }
    /*
       -- 文件信息表
       -- md5 文件md5
       -- file_id 文件id
       -- url 文件url
       -- size 文件大小, 以字节为单位
       -- type 文件类型： png, zip, mp4……
       -- count 文件引用计数， 默认为1， 每增加一个用户拥有此文件，此计数器+1
       */
    //设置数据库编码
    mysql_query(conn, "set names utf8");
    //得到文件后缀字符串 如果非法文件后缀,返回"null"
    get_file_suffix(filename, suffix); //mp4, jpg, png
    sprintf(sql_cmd, "insert into file_info (md5, file_id, url, size, type, count) values ('%s', '%s', '%s', '%ld', '%s', %d)",
            md5, fileid, fdfs_file_url, size, suffix, 1);
    if (mysql_query(conn, sql_cmd) != 0) //执行sql语句
    {
        //print_error(conn, "插入失败");
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 插入失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 文件信息插入成功\n", sql_cmd);
    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN-1, "%Y-%m-%d %H:%M:%S", localtime(&now));
    /*
   -- 用户文件列表
   -- user 文件所属用户
   -- md5 文件md5
   -- createtime 文件创建时间
   -- filename 文件名字
   -- shared_status 共享状态, 0为没有共享， 1为共享
   -- pv 文件下载量，默认值为0，下载一次加1
   */
    sprintf(sql_cmd, "insert into user_file_list(user, md5, createtime, filename, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
            user, md5, create_time, filename, 0, 0);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }
    //查询用户文件数量
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    int ret2 = 0;
    char tmp[512] = {0};
    int count = 0;
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 == 1) //没有记录
    {
        //插入记录
        sprintf(sql_cmd, " insert into user_file_count (user, count) values('%s', %d)", user, 1);
    }
    else if(ret2 == 0)
    {
        //更新用户文件数量count字段
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count+1, user);
    }
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }
END:
    if (conn != NULL)
    {
        mysql_close(conn); //断开数据库连接
    }

    return ret;
}

int main()
{
    char filename[FILE_NAME_LEN] = {0};
    char user[USER_NAME_LEN] = {0};
    char md5[MD5_LEN] = {0};
    long size = 0;
    char fileid[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_url[FILE_URL_LEN] = {0};

    //读取数据库配置信息
    read_cfg();

    while(FCGI_Accept() >= 0)
    {
        char* contentLength = getenv("CONTENT_LENGTH");
        long len;
        int ret = 0;
        printf("Content-type: text/html\r\n\r\n");
        if (contentLength != NULL)
        {
            len = strtol(contentLength, NULL, 10); //字符串转long， 或者atol
        }
        else
        {
            len = 0;
        }

        if (len <= 0)
        {
            printf("No data from standard input\n");
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "len = 0, No data from standard input\n");
            ret = -1;
        }
        else
        {
            if (recv_save_file(len, user, filename, md5, &size) < 0)
            {
                ret = -1;
                goto END;
            }
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s成功上传[%s, 大小：%ld, md5码：%s]到本地\n", user, filename, size, md5);

            if (upload_to_dstorage(filename, fileid) < 0)
            {
                ret = -1;
                goto END;
            }

            unlink(filename);
            if (make_file_url(fileid, fdfs_file_url) < 0)
            {
                ret = -1;
                goto END;
            }
            if (store_fileinfo_to_mysql(user, filename, md5, size, fileid, fdfs_file_url) < 0)
            {
                ret = -1;
                goto END;
            }
END:
            memset(filename, 0, FILE_NAME_LEN);
            memset(user, 0, USER_NAME_LEN);
            memset(md5, 0, MD5_LEN);
            memset(fileid, 0, TEMP_BUF_MAX_LEN);
            memset(fdfs_file_url, 0, FILE_URL_LEN);
            char *out = NULL;
            /*
           上传文件：
           成功：{"code":"008"}
           失败：{"code":"009"}
            */
            if (ret == 0)
            {
                out = return_status("008");
            }
            else
            {
                out = return_status("009");
            }
            if (out != NULL)
            {
                printf(out);
                free(out);
            }
        }
    }
    return 0;
}