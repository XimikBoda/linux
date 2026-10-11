#ifndef __MACH_UNCOMPRESS_H
#define __MACH_UNCOMPRESS_H


#define UART_THR (*(volatile unsigned char *)0xA0080000)
#define UART_LSR (*(volatile unsigned char *)0xA0080014)

static inline void putc(int c)
{
    while ((UART_LSR & 0x20) == 0);
    UART_THR = c;
}

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
}

#endif
