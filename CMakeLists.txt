cmake_minimum_required(VERSION 3.5.1)
project(remote)

set(CMAKE_CXX_STANDARD 11)
include_directories(common)
add_subdirectory(common)

add_executable(reg
        src_cgi/reg_cgi.c)
target_link_libraries(reg common)

add_executable(login
        src_cgi/login_cgi.c)
target_link_libraries(login common)

add_executable(myfiles
        src_cgi/myfiles_cgi.c)
target_link_libraries(myfiles common)

add_executable(upload
        src_cgi/upload_cgi.c)
target_link_libraries(upload common)

add_executable(md5
        src_cgi/md5_cgi.c)
target_link_libraries(md5 common)

add_executable(dealfile
        src_cgi/dealfile_cgi.c)
target_link_libraries(dealfile common)


add_executable(sharefiles
        src_cgi/sharefiles_cgi.c)
target_link_libraries(sharefiles common)


add_executable(dealsharefile
        src_cgi/dealsharefile_cgi.c)
target_link_libraries(dealsharefile common)
