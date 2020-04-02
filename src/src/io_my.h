#ifndef _ASM_IO_MY_H
#define _ASM_IO_MY_H

/*
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

#ifdef SLOW_IO_BY_JUMPING
#define __SLOW_DOWN_IO __asm__ __volatile__("jmp 1f\n1:\tjmp 1f\n1:")
#else
#define __SLOW_DOWN_IO __asm__ __volatile__("outb %al,$0x80")
#endif

#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif

/*
 * Talk about misusing macros..
 */

#define __MY_OUT1(s,x) \
extern inline void __my_out##s(unsigned x value, unsigned short port) {

#define __MY_OUT2(s,s1,s2) \
__asm__ __volatile__ ("out" #s " %" s1 "0,%" s2 "1"

#define __MY_OUT(s,s1,x) \
__MY_OUT1(s,x) __MY_OUT2(s,s1,"w") : : "a" (value), "d" (port)); } \
__MY_OUT1(s##c,x) __MY_OUT2(s,s1,"") : : "a" (value), "id" (port)); } \
__MY_OUT1(s##_p,x) __MY_OUT2(s,s1,"w") : : "a" (value), "d" (port)); SLOW_DOWN_IO; } \
__MY_OUT1(s##c_p,x) __MY_OUT2(s,s1,"") : : "a" (value), "id" (port)); SLOW_DOWN_IO; }

#define __MY_IN1(s) \
extern inline MY_RETURN_TYPE __my_in##s(unsigned short port) { MY_RETURN_TYPE _v;

#define __MY_IN2(s,s1,s2) \
__asm__ __volatile__ ("in" #s " %" s2 "1,%" s1 "0"

#define __MY_IN(s,s1,i...) \
__MY_IN1(s) __MY_IN2(s,s1,"w") : "=a" (_v) : "d" (port) ,##i ); return _v; } \
__MY_IN1(s##c) __MY_IN2(s,s1,"") : "=a" (_v) : "id" (port) ,##i ); return _v; } \
__MY_IN1(s##_p) __MY_IN2(s,s1,"w") : "=a" (_v) : "d" (port) ,##i ); SLOW_DOWN_IO; return _v; } \
__MY_IN1(s##c_p) __MY_IN2(s,s1,"") : "=a" (_v) : "id" (port) ,##i ); SLOW_DOWN_IO; return _v; }

#define __MY_OUTS(s) \
extern inline void my_outs##s(unsigned short port, const void * addr, unsigned long count) \
{ __asm__ __volatile__ ("cld ; rep ; outs" #s \
: "=S" (addr), "=c" (count) : "d" (port),"0" (addr),"1" (count)); }

#define MY_RETURN_TYPE unsigned char
/* __MY_IN(b,"b","0" (0)) */
__MY_IN(b,"")
#undef MY_RETURN_TYPE
#define MY_RETURN_TYPE unsigned short
/* __MY_IN(w,"w","0" (0)) */
__MY_IN(w,"")
#undef MY_RETURN_TYPE
#define MY_RETURN_TYPE unsigned int
__MY_IN(l,"")
#undef MY_RETURN_TYPE

__MY_OUT(b,"b",char)
__MY_OUT(w,"w",short)
__MY_OUT(l,,int)

__MY_OUTS(b)
__MY_OUTS(w)
__MY_OUTS(l)

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside a inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define my_outb(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_outbc((val),(port)) : \
	__my_outb((val),(port)))

#define my_inb(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_inbc(port) : \
	__my_inb(port))


#define my_outw(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_outwc((val),(port)) : \
	__my_outw((val),(port)))

#define my_inw(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_inwc(port) : \
	__my_inw(port))


#define my_outl(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_outlc((val),(port)) : \
	__my_outl((val),(port)))

#define my_inl(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__my_inlc(port) : \
	__my_inl(port))
#endif
