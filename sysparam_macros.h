/*
 * sysparam_macros.h
 *
 *  Created on: 14 апр. 2021 г.
 *      Author: compl
 */

#ifndef SYSPARAM_MACROS_H_
#define SYSPARAM_MACROS_H_


#include "sysparam.h"
#include "macros.h"

#ifndef MEMORY_OK_MAGIC
    #define MEMORY_OK_MAGIC 1
#endif

#define SPTW_GETR(type, what, where, R) do{\
    if((err = CAT(sysparam_get_,type)(TOSTRING(what), &where)) < SYSPARAM_OK) {\
        LOGE("sysparam_get_"TOSTRING(type)" "TOSTRING(what)" failed (%d)", err);\
        R;\
    }\
}while(0)

#define SPTW_SETR(type, what, value, R) do{\
    if((err = CAT(sysparam_set_,type)(TOSTRING(what), value)) < SYSPARAM_OK) {\
        LOGE("sysparam_set_"TOSTRING(type)" "TOSTRING(what)" failed (%d)", err);\
        R;\
    }\
}while(0)

#define SPTW_GETNR(type, what, R) SPTW_GETR(type, what, what, R)
#define SPTW_SETNR(type, what, R) SPTW_SETR(type, what, what, R)


#endif /* SYSPARAM_MACROS_H_ */
