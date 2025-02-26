/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file bs2000/os.h
 * @brief This file in included in all Apache source code. It contains definitions
 * of facilities available on _this_ operating system (HAVE_* macros),
 * and prototypes of OS specific functions defined in os.c or os-inline.c
 *
 * @defgroup APACHE_OS_BS2000 bs2000
 * @ingroup  APACHE_OS
 * @{
 */

#ifndef APACHE_OS_BS2000_H
#define APACHE_OS_BS2000_H

#define PLATFORM "BS2000"

#include "../unix/os.h"

/* Other ap_os_ routines not used by this platform */

extern pid_t os_fork(const char *user);

#endif /* APACHE_OS_BS2000_H */
/** @} */
