```C

long dv_num_edges(F_POLYHEAD* head);
void dv_rmv_s_arcs(F_POLYHEAD* head, dbrep aper);
static XYTREE* dv_GetBoundary(shape_type* boundary_p);
long dv_genholes(shape_type* shape_p, dvInstData* p_instData, F_POLYHEAD** head, dbptr_type shp_net, int buffer_id, int callFromDRC, int doSmooth, F_POLYHEAD** newVoids, int callFromFast);
void dv_debug_chk_loop(double x1, double y1, double c1x, double c1y, double r1, double x2, double y2, int* put_printf);
void dv_debug_chk_fpoly_loop(F_POLY)
void dv_debug_chk_fexPpoly_loop(fexpLoop* loop);
void dv_debug_chk_fexPpoly(fexpPoly* poly);
void dv_debug_chk_fpoly(F_POLYHEAD* head);
int _dv_debug_on(int debugLevel);
void _dv_debug_off();
int dv_debug_getDebuggerLevel();
int dv_debug_next_sc();
long dv_debug_fpoly_drawshp_sc(F_POLYHEAD* head, int subclassIncrFlag, const char* infoString, int debugLevel);
fexpEdge* dvClonefExpEdge(fexpEdge* origEdge);
fexpLoop* dvClonefExpLoop(fexpLoop* origLoop);
fexpPoly* dvClonefExpPoly(fexpPoly* origPoly);
long dv_debug_fexp_drawshp_sc(fexpPoly* polyExp_p, int subclassIncrFlag, const char* infoString, int debugLevel);
double dv_ExpandAdjust(shape_type* shape_p, dba_dynfill_params* params_p);
dv_mergefpoly(shape_type* shape_p, dvInstData* p_instData, F_POLYHEAD** head, F_POLYHEAD* voids, int doClean, int doSmooth, int callFromDRC);
int dv_pin_x_cns_area(dbptr_type pin_ptr, shape_type_ptr shape_p);
static void _mark_x_pin_area_item(dbptr_type item_ptr, dbptr_type shp, db_state_flags pin_x_area_state_flag);
static void _mark_pins_x_area(int buffer_id, dbptr_type shp, db_state_flags pin_x_area_state_flag);
db_state_flags dv_checkout_x_pin_area(shape_type_ptr shape_p, short buffer_id);
static void _clear_x_pin_area(db_state_flags state_flag, void* item_buf);
static void _clear_voided_pin(db_state_flags state_flag, void* item_buf);
static long _dv_debug_fpoly_drawloop(dbptr_type parent, fexpLoop* loop);
static int _dv_debug_updateSubclass(const char* infoString, int subclassIncrFlag, int mergeLayers);
static int _dv_debug_debugLevelOk(int debugLevel, int reqDebugLevel);

```
