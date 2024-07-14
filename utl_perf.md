```C
static long utl_perfAllOnOff(int* drc_count_p, int onoff_code, int cnsId);
static void (*_routeVisionCB)() = NULL;
long utl_perfTuneOn(int perfMask, int* drc_count_p);
long utl_perfTuneOnShort(int perfMask, short* drc_count_p);
long utl_perfTuneOff(int perfMask);
static long utl_perfAllOnOff(int* drc_count_p, int onoff_code, int cnsId);
long utl_perfAllOn(int* drc_count_p);
long utl_perfAllOff(void);
long utl_perfAllSlow(int tune_off, int tune_brn_trace);
long utl_perfTuneOffByCnsId(int perfMask, int cnsId);
long utl_perfTuneOnByCnsId(int perMask, int cnsId, int* drc_count_p);
long utl_perfTransStop(int perfMask, int transMark, long status, int* drcCountP);
void utl_perfTuneSetRouteVisionCB(void* cb);
```
