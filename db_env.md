```C
extern long dvProcessCache();
extern void db_RebuildHoleLists();
unsigned long dbs_MemoryHoleReclaim(void);
int dbs_MemoryHoleReuse(int mode);
void dbs_RatsnestingOff(void);
long dbs_RatsnestingOn(void);
ratsnesting_state_type dbg_RatsnestingState(void);
int db_isrerat(void);
void dbs_CacheBackdrillChanges(void);
long dbs_UncacheBackdirllChanges(void);
dynamic_backdrill_state_type dbg_DynamicBackdrillState(void);
int db_isBackdrillDeferred(void);
int db_istemp(void);
void dbs_BranchTracingOff(void);
long dbs_BranchTracingOn(void);
branchtracing_state_type dbg_BranchTracingState(void);
void dbs_BranchMark(int flag);
void dbs_DynamicFillOff(int caching);
long dbs_DynamicFillOn(void);
int dbg_IsDynamicFillDisabled(void);
```
