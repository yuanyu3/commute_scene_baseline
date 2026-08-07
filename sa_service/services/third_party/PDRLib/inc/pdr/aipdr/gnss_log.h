/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2099. All Rights Reserved.
 * Descritpion: gnss_log.cpp 的头文件
 * Date: 2023-11-15
 */
#ifndef __GNSS_LOG_H__
#define __GNSS_LOG_H__

typedef unsigned char       uint8;
typedef unsigned short      uint16;
typedef unsigned int        uint32;
typedef unsigned int        uint32_t;
#ifndef WIN32
typedef unsigned int        DWORD;
#endif
typedef unsigned long long  uint64;

typedef char                int8;
typedef short               int16;
typedef int                 int32;
typedef long                LONG;
typedef unsigned long       ULONG;

typedef long long           int64;

typedef float               float32;
typedef double              float64;

typedef int                 BOOL;

typedef void*               THREAD_HANDLE;
typedef void*               MUTEX_HANDLE;
typedef void*               SEM_HANDLE;

typedef int                 FILE_DESCRIPTOR;


typedef struct
{
    uint32_t higeo_log_level;                     //0:commercial, 1:beta, 2:debug
    uint32_t higeo_log_size;                      //log每个文件大小,单位为MB
    uint32_t higeo_log_num_max_limit;             //log最大文件数量
} gnss_log_config_t;

typedef void (*gnss_init_logmgr_func)(void);
typedef void (*gnss_uninit_logmgr_func)(void);
typedef void (*gnss_set_logmgr_config_func)(gnss_log_config_t *lgconfig);
typedef void (*gnss_run_logmgr_func)(void);
typedef void (*gnss_log_message_func)(uint8 uc_type, const int8* ac_tag,
    const int8* ac_level, const int8* ac_file_num, int32 l_line_num, const int8* ac_log_info, ...);
typedef void (*gnss_open_log_file_func)(void);
typedef void (*gnss_close_log_file_func)(void);
typedef void (*gnss_set_log_time_func)(const int8* log_time);
typedef void (*gnss_set_log_path_func)(const int8* log_path);

typedef struct higeo_interface_logmgr
{
    gnss_init_logmgr_func        init_logmgr;
    gnss_uninit_logmgr_func      uninit_logmgr;
    gnss_set_logmgr_config_func  set_logmgr_config;
    gnss_run_logmgr_func         run_logmgr;
    gnss_log_message_func        output_log_message;
    gnss_open_log_file_func      open_log_file;
    gnss_close_log_file_func     close_log_file;
    gnss_set_log_time_func       set_log_time;          // HiMatrix使用,用来打桩log时戳
    gnss_set_log_path_func       set_log_path;          // HiMatrix使用,用来打桩log目录
} higeo_interface_logmgr_t;

#ifdef __cplusplus
extern "C"
{
#endif
void gnss_log_message(uint8 uc_type, const int8* ac_tag, const int8 *ac_level, const int8 *ac_file_name,
                      int32 l_line_num, const int8 *ac_log_info, ...);
uint8 higeo_get_fused_logmgr_interface(struct higeo_interface_logmgr* pst_fd_interface);
void gnss_set_log_path(const int8* log_path);
#ifdef __cplusplus
}
#endif

#endif /* end of gnss_log.h */