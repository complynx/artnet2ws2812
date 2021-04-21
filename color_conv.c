/*
 * color_conv.c
 *
 *  Created on: 19 апр. 2021 г.
 *      Author: compl
 */
#include "color_conv.h"
#include <stdlib.h>
#include <math.h>

#define max3(a, b, c) (((a)>(b))?(((c)>(a))?(c):(a)):((c)>(b))?(c):(b))
#define min3(a, b, c) (((a)<(b))?(((c)<(a))?(c):(a)):((c)<(b))?(c):(b))
#define clip(a, M, m) ((a)>(M)?(M):(a)<(m)?(m):(a))

void rgb2hsv(ws2812_pixel_t*in, color_HSV*out)
{
    if(in == NULL || out == NULL) return;
    uint8_t min, max, delta;

    min = min3(in->red,in->green,in->blue);
    max = max3(in->red,in->green,in->blue);

    out->v = ((float)max)/255.; // v
    delta = max - min;
    if (delta == 0) {
        out->s = 0;
        out->h = HUE_UNDEFINED;
        return;
    }
    if( max > 0 ) { // NOTE: if Max is == 0, this divide would cause a crash
        out->s = ((float)delta / (float)max); // s
    } else {
        // if max is 0, then r = g = b = 0
        out->s = 0;
        out->h = HUE_UNDEFINED;
        return;
    }
    if( in->red == max )
        out->h = ((float)( in->green - in->blue )) / (float)delta; // between yellow & magenta
    else
    if( in->green == max )
        out->h = 2.0 + ((float)( in->blue - in->red )) / (float)delta;  // between cyan & yellow
    else
        out->h = 4.0 + ((float)( in->red - in->green )) / (float)delta;  // between magenta & cyan

    out->h *= 60.0;                              // to degrees

    if( out->h < 0.0 )
        out->h += 360.0; // rollover
}

void hsv2rgb(color_HSV*in, ws2812_pixel_t*out)
{
    if(in == NULL || out == NULL) return;
    float      hh, p, q, t, ff;
    long        i;

    if(in->s <= 0.0) {       // < is wrong, yet for compiler
        out->red = in->v*(255); // grey
        out->green = in->v*(255);
        out->blue = in->v*(255);
        return;
    }

    while(in->h>=360) in->h -= 360;
    while(in->h<0) in->h += 360;
    hh = (float)in->h / 60.;            // sector 0 to 5
    i = (long)hh;
    ff = hh - i;
    p = in->v * (1.0 - in->s);
    q = in->v * (1.0 - (in->s * ff));
    t = in->v * (1.0 - (in->s * (1.0 - ff)));

    switch(i) {
    case 0:
        out->red = in->v*255;
        out->green = t*255;
        out->blue = p*255;
        break;
    case 1:
        out->red = q*255;
        out->green = in->v*255;
        out->blue = p*255;
        break;
    case 2:
        out->red = p*255;
        out->green = in->v*255;
        out->blue = t*255;
        break;
    case 3:
        out->red = p*255;
        out->green = q*255;
        out->blue = in->v*255;
        break;
    case 4:
        out->red = t*255;
        out->green = p*255;
        out->blue = in->v*255;
        break;
    case 5:
    default:
        out->red = in->v*255;
        out->green = p*255;
        out->blue = q*255;
        break;
    }
}

#ifndef __xtensa__
#include <string.h>
#include <stdio.h>

#define d1(a,b,t) (((a.t > b.t) ? (a.t - b.t) : (b.t - a.t))>0)

int main(int argc, char* argv) {
    uint32_t c = 0;
    color_HSV b;
    ws2812_pixel_t a,d;
    for(c=0; c<=0xffffff; ++c) {
        a.red = ((c>>16)&0xff);
        a.green = ((c>>8)&0xff);
        a.blue = ((c)&0xff);

        rgb2hsv(&a, &b);
        hsv2rgb(&b, &d);
        if(d1(a,d,red) || d1(a,d,green) || d1(a,d,blue)) {
            printf("a: %02x%02x%02x b: %d %d %d c: %02x%02x%02x\n",
                    a.red, a.green, a.blue,
                    b.h, b.s, b.v,
                    d.red, d.green, d.blue);
        }
    }
    return 0;
}

#endif


