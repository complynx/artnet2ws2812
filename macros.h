/*
 * macros.h
 *
 *  Created on: 14 апр. 2021 г.
 *      Author: compl
 */

#ifndef MACROS_H_
#define MACROS_H_

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CAT2(x,y) x##y
#define CAT(x,y) CAT2(x,y)

#endif /* MACROS_H_ */
