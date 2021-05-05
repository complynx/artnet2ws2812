PROGRAM=artnet2ws2812
EXTRA_COMPONENTS = extras/i2s_dma extras/ws2812_i2s extras/dhcpserver
EXTRA_CFLAGS = -DI2S_COLOR_PROFILE_RGB=1

include ${RTOS_PATH}/common.mk
