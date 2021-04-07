PROGRAM=artnet2ws2812
EXTRA_COMPONENTS = extras/i2s_dma extras/ws2812_i2s
EXTRA_CFLAGS = -DLOGGER_LEVEL=3

include ${RTOS_PATH}/common.mk
