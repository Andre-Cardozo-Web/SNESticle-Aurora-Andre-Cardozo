/* AURORA_SGB_MGBA_LINK_PROBE_V0_3_20260904: compile/link only. */
#include <string.h>
#include <mgba/core/cpu.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/serialize.h>
#include <mgba/internal/gb/video.h>
#include <mgba/internal/gb/renderers/software.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/vfs.h>
#include "aurora-hooks.h"
static unsigned char rom[0x8000], keys;
static struct GBSerializedState state;
static mColor pixels[160*144];
static unsigned char j(void *c,int p14,int p15,int w){(void)c;(void)p14;(void)p15;(void)w;return 0x0f;}
static void l(void *c,int y,const unsigned short *r){(void)c;(void)y;(void)r;}
int main(void){
 struct GB gb; struct SM83Core cpu; struct GBVideoSoftwareRenderer renderer;
 struct mCPUComponent *components[CPU_COMPONENT_MAX]; struct VFile *vf,*sv;
 memset(&gb,0,sizeof(gb)); memset(&cpu,0,sizeof(cpu)); memset(&renderer,0,sizeof(renderer)); memset(components,0,sizeof(components));
 GBCreate(&gb); SM83SetComponents(&cpu,&gb.d,CPU_COMPONENT_MAX,components); SM83Init(&cpu);
 GBVideoSoftwareRendererCreate(&renderer); renderer.outputBuffer=pixels; renderer.outputBufferStride=160; GBVideoAssociateRenderer(&gb.video,&renderer.d);
 gb.keySource=&keys; vf=VFileMemChunk(rom,sizeof(rom)); if(vf) (void)GBLoadROM(&gb,vf); gb.model=GB_MODEL_SGB; SM83Reset(&cpu);
 sv=VFileMemChunk(0,0); if(sv) (void)GBLoadSave(&gb,sv); mGBAAuroraSetHooks(0,j,l,0); GBSerialize(&gb,&state); (void)GBDeserialize(&gb,&state); SM83Run(&cpu); mGBAAuroraSetHooks(0,0,0,0);
 SM83Deinit(&cpu); GBDestroy(&gb); return sizeof(state)==0x11800 ? 0:1;
}

/* AURORA_SGB_GBRESET_TRACE_RECOVERY_V0_6_4_1_20260905 */
void AuroraSgbBootTrace(const char *pText)
{
    (void)pText;
}
