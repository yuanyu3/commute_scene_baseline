/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2019. All rights reserved.
 * Description: Public parameter definition.
 * Author: 杨伟君 y00295064
 * Create: 2019-05-14
 */
#ifndef __ALGCOMMONDEF_H__
#define __ALGCOMMONDEF_H__

#ifndef TRUE
#define TRUE                        1
#endif  // TRUE
#ifndef FALSE
#define FALSE                       0
#endif  // !FALSE

#ifndef SUCCESS
#define SUCCESS                     0
#endif
#ifndef FAIL
#define FAIL                        1
#endif

#ifndef ERR_POINTER
#define ERR_POINTER                 2  // 空指针
#endif
#ifndef ERR_LENGTH
#define ERR_LENGTH                  3  // 数据长度不对
#endif
#ifndef ERR_SAMPLE_TIME
#define ERR_SAMPLE_TIME             4  // 数据采样时间不对
#endif

#ifndef QUAT_NUM
#define QUAT_NUM                    4
#endif
#ifndef AXIS_NUM
#define AXIS_NUM                    3
#endif

// 参数定义
#ifndef X_AXIS
#define X_AXIS                      0
#endif
#ifndef Y_AXIS
#define Y_AXIS                      1
#endif
#ifndef Z_AXIS
#define Z_AXIS                      2
#endif

#ifndef PI
#define PI                          3.141592653589793
#endif
#ifndef PI_MULT_2
#define PI_MULT_2                   6.283185307179586
#endif
#ifndef PI_DIV_4
#define PI_DIV_4                    0.7853981634f
#endif
#ifndef PI_DIV_2
#define PI_DIV_2                    1.5707963268f
#endif
#ifndef PI_3DIV4
#define PI_3DIV4                    2.3561944902f
#endif
#ifndef COE_DEG2RAD
#define COE_DEG2RAD                 0.017453292519943
#endif
#ifndef COE_RAD2DEG
#define COE_RAD2DEG                 57.295779513f
#endif

#ifndef MINIMUM_UNIX_TIME
#define MINIMUM_UNIX_TIME           1420045261000  // in ms  2015.1.1
#endif
#ifndef MAXIMUM_UNIX_TIME
#define MAXIMUM_UNIX_TIME           2524582861000  // in ms  2050.1.1
#endif

// constant time
// 1.5s
#ifndef TIME_ONE_AND_HALF_SEC_IN_MS
#define TIME_ONE_AND_HALF_SEC_IN_MS 1500  // 1.5s
#endif
#ifndef TIME_THREE_SEC_IN_MS
#define TIME_THREE_SEC_IN_MS        3000
#endif
#ifndef TIME_FIVE_SEC_IN_MS
#define TIME_FIVE_SEC_IN_MS         5000
#endif
#ifndef TIME_THIRTY_SEC_IN_MS
#define TIME_THIRTY_SEC_IN_MS       30000
#endif

#endif
