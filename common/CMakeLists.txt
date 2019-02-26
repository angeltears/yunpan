aux_source_directory(. DIR_COMMON_SRCS)

ADD_LIBRARY(common ${DIR_COMMON_SRCS})


target_link_libraries(common -lhiredis -lmysqlclient -lm -lfastcommon -lfcgi -lfdfsclient )