/* Export the shipped overview renderer at native E1003 resolution. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "wind_renderer.h"
#include "wind_renderer_fixture.h"
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    wind_renderer_input_v2_t input;
    wind_renderer_dashboard_t rows[3];
    const char *names[]={"EDAM","WIJK AAN ZEE","SCHEVENINGEN"};
    if(wind_renderer_fixture_build(3,&input))return 2;
    for(int i=0;i<3;i++) {
        if(wind_renderer_input_v2_to_dashboard(&input,&rows[i]))return 2;
        rows[i].spot_name=names[i]; rows[i].swell_size=i==1?2:0;
        for(int d=0;d<5;d++) {
            for(int h=0;h<24;h++) {
                rows[i].swell_hourly[d][h]=180+i*40+d*25+(int)(90*sin(h*.27+d+i*.6));
                rows[i].secondary_swell_hourly[d][h]=0;
            }
            for(int j=0;j<5;j++) {
                wind_renderer_sample_t *w=&rows[i].days[d].samples[j];
                w->available=1; w->sustained_kt=16+i*2+(int)(8*sin(j*.55+d*.9+i*.4));
                w->gust_kt=w->sustained_kt+7;w->destination_degrees=40+d*20+j*5;
                rows[i].swell[d][j]=(wind_renderer_swell_sample_t){rows[i].swell_hourly[d][8+j*3],70+d*10+i*5,80+d*10};
            }
        }
    }
    uint8_t *pixels=malloc(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    wind_renderer_stats_t stats;
    if(wind_renderer_render_overview(rows,3,0,7,pixels,WIND_RENDERER_E1003_COMPOSITION_BYTES,&stats)||stats.clipped_primitives)return 3;
    int w,h;wind_renderer_display_dimensions(WIND_RENDERER_DISPLAY_E1003_GC16,&w,&h);
    uint8_t *line=malloc(w);FILE *out=fopen(argv[1],"wb");if(!out)return 4;
    fprintf(out,"P5\n%d %d\n255\n",w,h);
    for(int y=0;y<h;y++) {
        if(wind_renderer_project_display_row(WIND_RENDERER_DISPLAY_E1003_GC16,pixels,WIND_RENDERER_E1003_COMPOSITION_BYTES,y,line,w))return 5;
        for(int x=0;x<w;x++)line[x]*=17;
        if(fwrite(line,1,w,out)!=(size_t)w)return 6;
    }
    free(line);free(pixels);return fclose(out)!=0;
}
