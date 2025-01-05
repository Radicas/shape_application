static long _dvUpdateShapes(dvCacheHeaderType *cacheRoot_p, dvInstData *p_instData)
{
    // 检查boundaryList_p是否有受影响的shape，如果没有，直接返回成功。
    // 如果没有传入实例数据p_instData，则创建一个新的实例数据。
    // 关闭显示和一些性能约束检查以提高性能。
    // 遍历受影响的shape，根据多用户模式或单用户模式进行处理，并更新每个shape。
    // 根据错误码进行相应处理，如忽略某些特定错误或标记子类为仅Out-Of-Date。
    // 恢复性能约束检查和显示设置。
    // 如果创建了新的实例数据，则释放它，否则恢复原来的参数。
    // 返回最终的错误码。

    dvBoundaryListType *boundaryList_p = cacheRoot_p->boundaryList_p; // 从缓存中拿到所有轮廓列表
    int SetOODOnly[MAX_ETCH_SUBCLASS];                                // 设置仅Out-Of-Date
    int i;
    long error = SUCCESS;      // 错误码，初始为成功
    int oldDisplay = TRUE;     // 旧显示状态
    int localInst = FALSE;     // 本地实例标志
    void *p_saveParams = NULL; // 保存参数的指针
    int subclass;

    // 如果没有受影响的shape，直接返回
    if (boundaryList_p->boundaryCount == 0)
        return (SUCCESS);

    if (!p_instData)
    {
        localInst = TRUE;
        p_instData = static_cast<dvInstData *>(SYMalloc(sizeof(dvInstData))); // 分配内存
        BZERO(p_instData, sizeof(dvInstData));                                // 初始化
        dv_init_all_scan_hash(p_instData);                                    // 初始化扫描哈希
    }
    else
    {
        p_saveParams = p_instData->params_p; // 保存实例数据的参数
        p_instData->params_p = NULL;         // 将参数置空
    }

    // 调试 - 输出每个受影响shape的孔洞/段数据
    oldDisplay = utl_dispena(FALSE); // 关闭显示
    if (SYGetEnv("dv_debug"))
        _dvDumpImpactedShapeData(cacheRoot_p); // 输出shape数据

    // 为了性能暂时关闭选择的约束检查
    utl_perfTuneOff(PERF_MASK); // 关闭性能约束

    // 遍历受影响的shape并按需更新
    for (int i = 0; i < dbg_class_udef_count(ETCH); i++)
    {
        SetOODOnly[i] = FALSE; // 初始化Out-Of-Date标志
    }
    for (i = 0; i < boundaryList_p->boundaryCount; i++)
    {
        subclass = ELEMENT_SUBCLASS(boundaryList_p->boundaryDataList_p[i]->boundary_p); // 获取子类
        if (SetOODOnly[subclass])
            dbs_ShapeDynFill_OOD(boundaryList_p->boundaryDataList_p[i]->boundary_p); // 处理过期的shape
        else
        {
            if (db_isMultiUser())
            {
                /*
                    多用户模式 - 对于单用户或生成原始更改的多用户客户端，
                    以下函数应始终返回false
                 */
                dvBoundaryDataType *boundaryData_p = boundaryList_p->boundaryDataList_p[i];
                if (!dv_isFrozenShape(boundaryData_p->boundary_p) &&
                    db_mu_noShapeInput(boundaryData_p->boundary_p))
                {
                    continue;
                }
                error = _dvUpdateImpactedShape(cacheRoot_p, boundaryList_p->boundaryDataList_p[i], p_instData);
                // 多用户模式下，仅标记av进程的开始，以便收集此后的任何cline更改。
                _mark_av_process(boundaryData_p);
            }
            else
            {
                // 更新受影响的shape
                error = _dvUpdateImpactedShape(cacheRoot_p, boundaryList_p->boundaryDataList_p[i], p_instData);
            }
            _dvFreeDummyObjects(); // 释放虚拟对象
        }
        if (error != SUCCESS)
        {
            if (dba_dyn_shape_mode_extended(NULL, NULL) && (error == DV_ERROR_shapeVoidedAway || error == DV_ERROR_noShapeLeft))
            {
                error = SUCCESS; // 忽略某些错误
            }
            else if (error != DV_ERROR_shapeVoidedAway)
            {
                dbs_ShapeDynFill_OOD(boundaryList_p->boundaryDataList_p[i]->boundary_p);
                SetOODOnly[subclass] = TRUE; // 标记子类为仅Out-Of-Date
            }
        }
    }
    // 重新开启约束检查
    utl_perfTuneOn(PERF_MASK, NULL);
    // 完成后重新启用显示
    utl_dispena(oldDisplay);
    if (oldDisplay)
    {
        utl_dispRedraw(); // 重绘显示
    }
    if (localInst)
    {
        dv_free_all_scan_hash(p_instData); // 释放扫描哈希
        SYFree(p_instData);                // 释放实例数据
        p_instData = NULL;                 // 将指针置空
    }
    else
    {
        p_instData->params_p = static_cast<dba_dynfill_params *>(p_saveParams); // 恢复参数
    }
    return (error); // 返回错误码
}
static long _dvUpdateImpactedShape(dvCacheHeaderType *cacheRoot_p, dvBoundaryDataType *boundaryData_p, dvInstData *p_instData)
{
    /* routine to update the voiding state of an impacted shape */
    int retVal;

    if (dba_dyn_shape_mode_fast() || dbg_design_flavor() == DESIGN_FLAVOR_ORCAD_X || dv_isFrozenShape(boundaryData_p->boundary_p))
    {
        retVal = _dvUpdateImpactedShapeFast(cacheRoot_p, boundaryData_p, p_instData);
    }
    else
    {
        retVal = _dvUpdateImpactedShapeSmooth(cacheRoot_p, boundaryData_p, p_instData);
    }
    return retVal;
}
static long _dvUpdateImpactedShapeSmooth(dvCacheHeaderType *cacheRoot_p, dvBoundaryDataType *boundaryData_p, dvInstData *p_instData)
{
    /* routine to update the voiding state of an impacted shape in smooth mode */
    // 用于在平滑模式下更新受影响shape的空洞状态的例程

    box_type damageExtents;                   // 定义损坏范围的盒子类型变量
    dba_dynfill_params *restore_p = NULL;     // 恢复参数的指针初始化为 NULL
    dba_transaction_mark_type start_mark = 0; // 事务开始标记
    dba_transaction_mark_type etch_mark = 0;  // 事务结束标记
    dbptr_type branch_p;                      // 数据库指针类型变量，用于存储分支指针
    dbptr_type shapeNet_p;                    // 数据库指针类型变量，用于存储shape网指针
    dvEtchShapeDataType *etchShapeData_p;     // 存储蚀刻shape数据的指针
    F_POLYHEAD *fpolyHead_p = NULL;           // 存储多边形头部的指针
    int i;                                    // 循环计数器
    int extMode;                              // 外部模式变量
    int localParams = FALSE;                  // 本地参数标志
    int localDRCWindow = FALSE;               // 本地DRC窗口标志
    int doHealPass = FALSE;                   // 是否执行修复标志
    long error = SUCCESS;                     // 错误码，初始值为成功
    shape_type *boundary_p;                   // shape类型的指针，用于存储边界
    shape_type *etchShape_p;                  // shape类型的指针，用于存储蚀刻shape
    shape_type *regenShape_p;                 // shape类型的指针，用于存储再生shape
    short objectBufferId = 0;                 // 对象缓冲区ID
    short drc_count = 0;                      // DRC计数
    dbptr_type boundary_group = NULL;         // 数据库指针类型变量，用于存储边界组
    box_type extents;                         // 定义扩展范围的盒子类型变量

    // 清除之前shape的适当实例参数数据集
    dv_empty_all_scan_hash(p_instData);
    if (p_instData->thermalBufferID)
        bufrset(p_instData->thermalBufferID);

    // 确定是否需要重新空洞整个shape，而不是修复单个子蚀刻shape
    boundary_p = boundaryData_p->boundary_p;
    boundary_group = dba_dynamic_shape_get_group(boundary_p);
    // 发送受影响的shape到服务器，收集自动空洞的连接信息
    db_ctmp_impct_shp(boundary_p, sizeof(shape_type), FALSE);
    if (_dvShouldWeRevoidTheEntireShape(boundaryData_p))
    {
        if (!dv_isLocalPatchRepair() ||
            boundaryData_p->revoidBoundary ||
            boundaryData_p->etchShapeCount == 0 ||
            dbg_shape_dyn_mask(boundaryData_p->boundary_p, DYNAMIC_FILL_OOD) ||
            SHAPE_IS_HATCHED(boundaryData_p->boundary_p))
        {
            error = _dvRevoidEntireShape(boundary_p);
            return error;
        }
        else
        {
            // 如果可能，不要完全重新空洞。只需从边界修复shape即可
            doHealPass = TRUE;
        }
    }
    // ++rsl_inccnt; 增加计数器
    start_mark = dba_transaction_start();        // 开始事务
    dba_dyn_shape_mode_extended(&extMode, NULL); // 获取扩展模式

    /*
        初始化用于包含可空洞对象指针列表的缓冲区。
        这是由自动空洞用于为每个蚀刻shape中的每个对象生成孔洞
    */
    dv_bufinit(sizeof(dbptr_type), DV_BUFSIZE_ETCH, DV_BUFSIZE_ETCH, &objectBufferId);
    if (!p_instData->drcWindowBufferID)
    {
        localDRCWindow = TRUE;
        dv_bufinit(sizeof(box_type), DV_BUFSIZE_ETCH, DV_BUFSIZE_ETCH, &p_instData->drcWindowBufferID);
    }

    av_init_nopool(); // 初始化池

    // 设置并获取目标shape参数集指针。我们实际上只在这里获取一次参数并存储指针，以供下游按需检索
    restore_p = p_instData->params_p;
    if (!p_instData->params_p)
    {
        p_instData->params_p = _dvSetShapeParameters(boundary_p);
        localParams = TRUE;
    }

    // 设置网格填充状态
    p_instData->gridFillOff = (boundaryData_p->gridMode == DV_FILL_PERFECT && boundaryData_p->simpleUpdate)
                                  ? TRUE
                                  : FALSE;

    if (boundaryData_p->gridMode == DV_FILL_FAST ||
        (boundaryData_p->gridMode == DV_FILL_PERFECT && !boundaryData_p->simpleUpdate))
    {
        // 处理网格填充模式
        fpolyHead_p = utl_f_shp2poly(boundary_p);
        if (extMode == DV_FILL_PERFECT && !p_instData->thermalBufferID)
        {
            bufinit(sizeof(dbptr_type), DV_BUFSIZE_ETCH, DV_BUFSIZE_ETCH, &p_instData->thermalBufferID);
            dv_init_scan_set(&p_instData->p_thermalsSet);
        }

        // 获取shape的网络
        shapeNet_p = dba_dynamic_shape_get_net(boundary_p);
        if (extMode == DV_FILL_PERFECT && boundaryData_p->figureCount)
        {
            // 处理shape再生
            for (i = 0; i < boundaryData_p->figureCount; i++)
            {
                int newShapesBufferID = 0;
                // 重置每个shape的对象缓冲区
                bufrset(objectBufferId);
                ext_alltypes(boundaryData_p->figureList_p[i], &extents);
                bufpend(p_instData->drcWindowBufferID, &extents);

                if (SUCCESS == dba_dynamic_shape_regenerate_region(boundary_group, boundary_p, &extents, shapeNet_p, fpolyHead_p, &newShapesBufferID) && newShapesBufferID)
                {
                    // 处理再生shape
                    F_POLYHEAD *regenPoly_p;
                    box_type regenExtents;
                    int j = 0, count = 0, shapeCount = bufcount(newShapesBufferID);

                    for (j = 1; j <= shapeCount; j++)
                    {
                        bufget(newShapesBufferID, j, &regenShape_p);
                        ext_alltypes(regenShape_p, &regenExtents);
                        error = dvFindVoidableObjectsInWindow(&regenExtents, NULL, p_instData, regenShape_p, objectBufferId, FALSE);
                        dv_empty_scan_set(&p_instData->p_scanSet);
                        count = bufcount(objectBufferId);

                        // 注册修复损坏区域的范围框
                        db_ctmp_obs_win(regenShape_p, &regenExtents, TDB_UPDATE_OBS_WIN);

                        // 为shape轮廓和受影响的空洞构建多边形结构
                        regenPoly_p = utl_f_shp2poly(regenShape_p);
                        if (regenPoly_p == NULL)
                            continue;

                        // 删除shape
                        dv_deleteShape(regenShape_p, NULL);
                        dbs_shape_dyn_mask(regenShape_p, (DYNAMIC_FILL_OOD), FALSE);
                        dbs_shape_dyn_mask(regenShape_p, (SHP_GRID_FILL_DATA), TRUE);

                        error = dv_autovoid_patch(regenShape_p, shapeNet_p, &regenPoly_p, p_instData, objectBufferId, &regenExtents, TRUE);
                    }
                    dv_buffree(newShapesBufferID);
                }
            }
        }
        else
        {
            // 处理蚀刻shape计数
            for (i = 0; i < boundaryData_p->etchShapeCount; i++)
            {
                int newShapesBufferID = 0;
                dbptr_type p_figure = NULL;
                box_type etchExtents;
                db2point location;

                // 再生需要更新的任何shape区域
                etchShapeData_p = boundaryData_p->etchShapeDataList_p[i];
                etchShape_p = etchShapeData_p->etchShape_p;

                if (DELETE_MASK(etchShape_p))
                    continue; // 可能已被_dvShapeFragmentInVoid删除

                if (dbg_BlurStates(etchShape_p))
                    continue;

                // 重置每个蚀刻shape的对象缓冲区
                bufrset(objectBufferId);

                // 如果区域已经再生，则不再重复
                ext_alltypes(etchShape_p, &etchExtents);
                MIDPOINT(location, etchExtents.min, etchExtents.max);
                p_figure = dba_dynamic_shape_get_region_marker(boundary_group, location);
                if (p_figure)
                {
                    if (dv_is_scan_set_item(p_figure, p_instData->p_figureSet))
                        continue;
                }
                if (etchShapeData_p->doNotRevoid)
                {
                    error = dvFindVoidableObjectsInWindow(&etchExtents, NULL, p_instData, etchShape_p, objectBufferId, TRUE);
                    error = dv_autovoid_patch(etchShape_p, shapeNet_p, &fpolyHead_p, p_instData, objectBufferId, &etchExtents, TRUE);
                    dbs_shape_dyn_mask(etchShape_p, (DYNAMIC_FILL_OOD), FALSE);
                }
                else
                {
                    bufpend(p_instData->drcWindowBufferID, &etchExtents);
                    // 如果再生区域成功
                    if (SUCCESS == dba_dynamic_shape_regenerate_region(boundary_group, etchShape_p, &etchExtents, shapeNet_p, fpolyHead_p, &newShapesBufferID) && newShapesBufferID)
                    {
                        // 处理再生shape
                        F_POLYHEAD *regenPoly_p;
                        box_type regenExtents;
                        int j = 0, count = 0, shapeCount = bufcount(newShapesBufferID);

                        for (j = 1; j <= shapeCount; j++)
                        {
                            bufget(newShapesBufferID, j, &regenShape_p);
                            ext_alltypes(regenShape_p, &regenExtents);
                            error = dvFindVoidableObjectsInWindow(&regenExtents, NULL, p_instData, regenShape_p, objectBufferId, FALSE);
                            dv_empty_scan_set(&p_instData->p_scanSet);
                            count = bufcount(objectBufferId);

                            // 注册修复损坏区域的范围框
                            db_ctmp_obs_win(regenShape_p, &regenExtents, TDB_UPDATE_OBS_WIN);

                            // 为shape轮廓和受影响的空洞构建多边形结构
                            regenPoly_p = utl_f_shp2poly(regenShape_p);
                            if (regenPoly_p == NULL)
                                continue;

                            // 删除shape
                            dv_deleteShape(regenShape_p, NULL);
                            dbs_shape_dyn_mask(regenShape_p, (DYNAMIC_FILL_OOD), FALSE);
                            dbs_shape_dyn_mask(regenShape_p, (SHP_GRID_FILL_DATA), TRUE);

                            error = dv_autovoid_patch(regenShape_p, shapeNet_p, &regenPoly_p, p_instData, objectBufferId, &regenExtents, TRUE);
                        }
                        dv_buffree(newShapesBufferID);
                    }
                }
            }
        }
    }
    else
    {
        // 处理非网格填充模式的子蚀刻shape
        if (boundaryData_p->etchShapeCount)
        {
            for (i = 0; i < boundaryData_p->etchShapeCount; i++)
            {
                etchShapeData_p = boundaryData_p->etchShapeDataList_p[i];
                etchShape_p = etchShapeData_p->etchShape_p;

                if (DELETE_MASK(etchShape_p))
                    continue; // 可能已被_dvShapeFragmentInVoid删除

                if (dbg_BlurStates(etchShape_p))
                    continue;

                // 重置每个蚀刻shape的对象缓冲区
                bufrset(objectBufferId);
                ext_alltypes(etchShape_p, &etchExtents);
                error = dvFindVoidableObjectsInWindow(&etchExtents, NULL, p_instData, etchShape_p, objectBufferId, TRUE);
                error = dv_autovoid_patch(etchShape_p, shapeNet_p, &fpolyHead_p, p_instData, objectBufferId, &etchExtents, FALSE);
            }
        }
        else
        {
            // 在没有蚀刻shape时处理边界
            bufrset(objectBufferId);
            ext_alltypes(boundary_p, &etchExtents);
            error = dvFindVoidableObjectsInWindow(&etchExtents, NULL, p_instData, boundary_p, objectBufferId, TRUE);
            error = dv_autovoid_patch(boundary_p, shapeNet_p, &fpolyHead_p, p_instData, objectBufferId, &etchExtents, FALSE);
        }
    }

    // 确定是否需要执行修复通道
    if (doHealPass)
        error = _dvHealDamagedBoundary(boundary_p);

    // 重新设置shape轮廓以反映其新内容
    dbs_shape_dyn_mask(boundary_p, (DYNAMIC_FILL_OOD), FALSE);

    // 检查shape的热缓冲区并清除缓存
    if (p_instData->thermalBufferID)
        dv_empty_scan_set(&p_instData->p_thermalsSet);

    // 释放所有池
    av_free_nopool();

    // 处理事务结束标记
    etch_mark = dba_transaction_end();
    dba_transaction_check(start_mark, etch_mark);

    // 检查是否有本地参数
    if (localParams)
    {
        p_instData->params_p = restore_p;
    }

    // 检查是否有本地DRC窗口
    if (localDRCWindow)
    {
        dv_buffree(p_instData->drcWindowBufferID);
        p_instData->drcWindowBufferID = NULL;
    }

    return error;
}
static long _dvRevoidEntireShape(shape_type *shape_p)
{
    /* 例程：重新填充整个shape */
    shape_type *boundary_p;
    switch (ELEMT_CLASS(shape_p))
    {
    case ETCH:
        // 获取shape的边界
        boundary_p = SHAPE_PTR(dba_dynamic_shape_get_boundary(shape_p));
        break;
    case BOUNDARY:
        // shape本身就是边界
        boundary_p = shape_p;
        break;
    default:
        // 如果shape类型不符合预期，返回错误码
        return -1;
    }

    // 处理我们正在进行的原型实验
    // 适用于需要重新填充整个shape和更新shape命令的交互
    if (SYGetEnv("dv_proto_copy"))
    {
        // 将初始Allegroshape写入JSON文件
        if (SYGetEnv("dv_proto_writeshape"))
        {
            dv_writeAllegroShapeToJson(shape_p);
        }
        // 复制整个shape作为F_POLYGON，以便我们可以绕过初始孔洞生成，并在修复和平滑之前放回去
        dv_protoShapeAdd(shape_p, protoNewShape::protoCopy);
    }
    else if (SYGetEnv("dv_proto_diamond") || SYGetEnv("dv_proto_nvidia"))
    {
        // diamond是多线程实验，生成初始孔洞/平面
        // 然后在修复和平滑之前替换进去
        // nvidia使用diamond预处理，但使用nvidia GPU进行处理
        protoNewShapes protoMode = protoNewShapes::protoDiamond;
        if (SYGetEnv("dv_proto_nvidia"))
        {
            protoMode = protoNewShapes::protoNvidiaGpu;
        }
        // 设置线程数
        dv_protoSetNumThreads();
        // 添加shape到原型处理中
        dv_protoShapeAdd(shape_p, protoMode);
        // 进行diamond处理
        dv_protoDiamondProcess();

        // 目的：nvidia GPUshape原型
        if (dv_protoGetShapeMode() == protoNewShapes::protoNvidiaGpu)
        {
            // 默认写出
            if (SYGetEnv("dv_nvidia_in"))
            {
                // 从nvidia读取输入
                dv_readJson();
            }
            else
            {
                // 将输出发送到nvidia
                dv_writeJson();
            }
        }
    }
    // 更新shape的低级蚀刻
    long error = dba_dynamic_shape_update_etch_low(boundary_p, FULL_FILL, NULL);

    // 如果原型开启，关闭原型处理
    dv_protoShapeFinish();

    // 返回错误码
    return error;
}
long dba_dynamic_shape_update_etch(dba_object_handle object_handle, dyn_fill_control_type action, box_type *win)
{
    // 定义错误码，初始化为成功
    long error = SUCCESS;
    int updateOnlyOneShape = FALSE;
    // multiuser开关值，用于获取和设置自动填充状态
    int oldAutovoidValue = FALSE;
    if (object_handle == NULL)
        return SUCCESS;

    // 如果动态填充已禁用并且shape为OOD（超出日期），则返回成功
    if (dbg_IsDynamicFillDisabled() && dbg_ShapeDynFill_OOD(object_handle))
        return SUCCESS;

    // 检查是否在处理模糊的shape，如果是则返回成功
    if (dbg_BlurStates(object_handle))
        return SUCCESS;

    // 根据action值进行不同处理
    if (action == (OOD_WYSIWYG | SHAPE_VOIDS))
    {
        action = OOD_WYSIWYG;
        updateOnlyOneShape = TRUE;
        // 如果是边界且没有指定窗口，则更新shape标志
        if (ELEMENT_CLASS(object_handle) == BOUNDARY && !win)
        {
            db_ctmp_flags(object_handle, sizeof(shape_type), 0, TDB_FLG_UPDATE);
        }
    }
    else if (action == (OOD_GRIDDED | SHAPE_VOIDS))
    {
        action = OOD_GRIDDED;
        updateOnlyOneShape = TRUE;
        if (ELEMENT_CLASS(object_handle) == BOUNDARY && !win)
        {
            db_ctmp_flags(object_handle, sizeof(shape_type), 0, TDB_FLG_UPDATE);
        }
    }
    else if (action == (OOD_ONLY | SHAPE_VOIDS))
    {
        action = OOD_ONLY;
        updateOnlyOneShape = TRUE;
        if (ELEMENT_CLASS(object_handle) == BOUNDARY && !win)
        {
            db_ctmp_flags(object_handle, sizeof(shape_type), 0, TDB_FLG_UPDATE);
        }
        dbs_shape_dyn_mask(object_handle, (DYNAMIC_FILL_OOD), TRUE);
    }

    // 如果action有效
    if (action && (action & ~NO_ACTION))
    {
        // 如果是多用户环境，处理shape连接
        if (db_isMultiUser())
        {
            dba_dynamic_shape_item_process(object_handle, _store_shape_connections, NULL);
        }
        // 如果是边界类对象，清除临时标志
        if (ELEMENT_CLASS(object_handle) == BOUNDARY)
        {
            db_ctmp(object_handle, sizeof(shape_type), FALSE);
        }
        // 关闭性能调优
        utl_perfTuneOff(PERF_MASK);
        // 多用户环境下，切换自动填充状态
        if (db_isMultiUser())
        {
            oldAutovoidValue = dbs_autovoid_active(TRUE);
        }
        // 如果指定了窗口
        if (win)
        {
            int call_dv_window = TRUE;
            int allegro_class;
            int subclass = ELEMENT_SUBCLASS(object_handle);
            // 确定allegro类
            if (dbg_ShapeIsDynamic(object_handle) == DYNAMIC_ETCH_FILL)
                allegro_class = ETCH;
            else
                allegro_class = CAVITY;

            // 如果填充被禁用且没有缓存，清除shape并标记OOD
            if (dbg_IsDynamicFillDisabled() && !dbcom_->dyn_fill_cache_ptr)
            {
                error = _cre_clean_shape(object_handle, NULL);
                dbs_shape_dyn_mask(object_handle, (DYNAMIC_FILL_OOD), TRUE);
                if (allegro_class == CAVITY)
                    call_dv_window = FALSE; // 没有其他受影响的shape
            }
            else if ((action & FULL_FILL) && !(action & OOD_GRIDDED) &&
                     !dba_dyn_shape_mode_fast() && !dv_isFrozenShape((shape_type_ptr)object_handle))
            {
                // 在快速模式下，我们不想删除所有现有的孔洞
                error = _cre_clean_shape(object_handle, NULL);
                dbs_shape_dyn_mask(object_handle, (DYNAMIC_FILL_OOD), TRUE);
            }
            if (call_dv_window)
            {
                // 更新窗口中的孔洞
                dv_void_window(win, allegro_class, subclass, subclass,
                               NET_PTR(dba_dynamic_shape_get_net(object_handle)), DV_UPDATE_VOID,
                               object_handle);
            }
        }
        else
        {
            int oldDisplay = utl_dispena(FALSE);

            // 在实验性原型复制模式中，我们不想在这里清除shape
            if (!SYGetEnv("dv_proto_copy"))
            {
                error = _cre_clean_shape(object_handle, NULL);
            }
            if (!error)
            {
                // 更新shape中的孔洞
                if (updateOnlyOneShape)
                    dv_void_object(object_handle, DV_UPDATE_ONE_SHAPE_VOID);
                else
                    dv_void_object(object_handle, DV_UPDATE_VOID);
            }
            utl_dispena(oldDisplay);
        }
        // 恢复多用户环境下的自动填充状态
        if (db_isMultiUser())
        {
            dbs_autovoid_active(oldAutovoidValue);
        }
        // 重置所有运行时映射
        dv_reset_all_runtime_maps(DV_MAPS_ALL);
        // 打开性能调优
        utl_perfTuneOn(PERF_MASK, NULL);
    }
    return (SUCCESS);
}
long dv_void_object(dbptr_type object_p, int voidingMode)
{
    return (dv_void_objectLow(object_p, NULL, voidingMode));
}
long dv_void_objectLow(dbptr_type object_p, net_type *net_ptr, int voidingMode)
{
    dvInstData instData;
    dvObjectDataType *objectData_p = NULL;
    long error = SUCCESS;
    short impactedShapeBufferId = 0;
    int mask;
    int impacted_fill = 0;
    bool needToFreeParams = false;

    // Application ask to skip calls
    if (_DisableVoidObjectCalls)
        return (SUCCESS);

    // Specific to IC Packaging tools. Checks for need to update
    // cavity outlines because we've defined a keepin/outline shape
    if ((voidingMode != DV_DELETE_VOID && object_p) &&
        (ELEMENT_CLASS(object_p) == ROUTE_KEEPIN ||
         (ELEMENT_CLASS(object_p) == BOARD_GEOMETRY &&
          (ELEMENT_SUBCLASS(object_p) == BGEO_DESIGN_OUTLINE ||
           ELEMENT_SUBCLASS(object_p) == BGEO_OUTLINE) &&
          !dbcom_->etch_keepin_root && db_isicp())))
    {
        dba_dynamic_cavity_update_outline(object_p);
    }
    /*
        ensure that the given object is voidable. if not - check for a given
        constraint-area which is not voidable but requires special handling.
        if none of this is true, DO NOT PROCEED FURTHER. Absolutely kills
        performance. If you are adding many, many non-conductor elements on
        a layer. for drawing only.
    */
    if (object_p && !utl_isvoidable(object_p))
    {
        if (!utl_isvoidableCavity(object_p))
        {
            return (SUCCESS);
        }
        else
            impacted_fill = DYNAMIC_CAVITY_FILL;
    }
    else
    {
        if (voidingMode == DV_UPDATE_ONE_SHAPE_VOID)
        {
            voidingMode = DV_UPDATE_VOID;
            impacted_fill = FALSE;
        }
        else
        {
            impacted_fill = DYNAMIC_ETCH_FILL;
        }
    }
    /*
        ensure this code is not re-entered from outside while already processing
        an object(as a result of downstream processing of the object)
     */
    if (_dvIsNestedCall() == TRUE)
    {
        if (!_gridRegenerrateActive)
        {
#ifdef OSASSERT
            printf("__ void-object re-entry attempt -- \n");
#endif
        }
        return (SUCCESS);
    }
    else
    {
        _voidObjectProcessingActive = TRUE;
    }

    BZERO(&instData, sizeof(dvInstData));
    // scan hashes are always used
    dv_init_all_scan_hash(&instData);
    mask = ELEMENT_MASK(object_p);

    /*
        handle constraint-areas here. while not being voidable
        objects, their extents are used for both finding voidable
        objects and shape hits.
    */

    // route_keepin needs to mark
    if (mask == SHAPE)
    {
        switch (ELEMENT_CLASS(object_p))
        {
        case CONS_REGION:
        {
            box_type extents;
            int sc_low;
            int sc_high;

            ext_shp(SHAPE_PTR(object_p), &extents);
            _voidObjectProcessingActive = FALSE;
            sc_low = sc_high = ELEMENT_SUBCLASS(object_p);
            error = dv_void_window_low(&extents, CONS_REGION, sc_low, sc_high, NULL, DV_UPDATE_VOID, &instData);
        }
            goto DONE;
        }
    }

    instData.params_p = dv_recall_dyn_fill_params();
    if (!instData.params_p)
    {
        // get the global shape parameter and override if object is a shape
        instData.params_p = dba_dynamic_fill_params_get(NULL);
        needToFreeParams = true;
    }

    /*
        create and fill an object data structure instance with info about
        this object
    */
    objectData_p = _dvCreateTargetObjectData(object_p, net_ptr, voidingMode, impacted_fill, 0, &instData);
    if (objectData_p == NULL)
    {
        error = SUCCESS;
        goto DONE;
    }

    /*
        if batch or shape parameter global deferral is on - walk thru
        frect root looking for BOUNDARY-class shapes impacted by clearanced
        target object (impact means that clearanced target either is internal
        to the boundary or intersects the boundary outline).
        if there is no active deferral - continue on to updating the affected
        shapes.
     */
    error = _dvVoidIfCacheIsActive(objectData_p, voidingMode, &instData, impactedShapesBufferId);
DONE:
    dv_free_all_scan_hash(&instData);
    if (needToFreeParams && instData.params_p)
        dba_dynamic_fill_params_free(instData.params_p);
    instData.params_p = NULL;
    _voidObjectProcessingActive = FALSE;
    return (error);
}

static long _dvVoidIfCacheIsActive(dvObjectDataType *objectData_p,
                                   int voidingMode, dvInstData *p_instData,
                                   short impactedShapesBufferId)
{
    /* routine to void if caching inactive */
}

long dba_dynamic_shape_update_etch_low(dba_object_handle object_handle, dyn_fill_control_type action, box_type *win)
{
    int curFillMode;              // 当前填充模式
    int extFillMode;              // 扩展填充模式
    int oldAutovoidValue = FALSE; // 旧的自动填充值

    // 如果对象句柄为空，返回成功
    if (object_handle == NULL)
        return SUCCESS;

    // 如果动态填充被禁用并且形状为 OOD（过时），返回成功
    if (dbg_IsDynamicFillDisabled() && dbg_ShapeDynFill_OOD(object_handle))
        return SUCCESS;

    /*
        低级数据库工具可能会禁用实际更新并将形状保持在当前状态。
        这只应在动态填充被禁用时使用，以防止创建动态形状的干净副本，只需将形状标记为 OOD（过时）。
    */
    if (dba_disable_dyn_update_etch())
    {
        dbs_shape_dyn_mask_for_serial_or_parallel(object_handle, (DYNAMIC_FILL_OOD), TRUE);
        return SUCCESS;
    }

    // 如果形状在模糊处理中，返回成功
    if (db_BlurStates(object_handle))
        return SUCCESS;

    if (db_isMultiUser())
    {
        if (dbg_ShapeIsDynamic(object_handle) == DYNAMIC_ETCH_FILL)
        {
            db_ctmp(object_handle, sizeof(shape_type), FALSE); // 在多用户环境下处理动态蚀刻填充的形状
        }
        /*
            多用户环境下通知 Allegro 自动填充过程已经开始的函数。
        */
        oldAutovoidValue = dbs_autovoid_active(TRUE);
    }

    if (action && (action & ~NO_ACTION))
    {
        // 获取当前和扩展填充模式
        dba_dyn_shape_mode_extended_for_serial_or_parallel(&extFillMode, &curFillMode);
        if (action & OOD_WYSIWYG)
        {
            if (curFillMode != DV_FILL_WYSIWYG)
            {
                dbs_global_dynamic_fill_only_for_serial(DV_FILL_WYSIWYG); // 设置全局动态填充模式为 WYSIWYG
                if (extFillMode != DV_FILL_PERFECT)
                    dba_dynamic_shape_clear_grid_regions_for_serial_or_parallel(object_handle); // 清除网格区域
            }
        }
        else
        {
            curFillMode = 0; // 设置当前填充模式为 0
        }
        utl_perfTuneOff_only_for_serial(PERF_MASK); // 关闭性能调优

        /*
            现在在所有情况下都创建一个新的轮廓。这允许 `blr_dynamic_shape` 重新链接连接的 CLINEs，
            以防止它们在形状被填充时断开连接且不重新连接到形状。
        */

        /*
            仅多用户环境，查看 CCR1965156。如果我们得到一个具有添加或修改状态的形状，
            我们在服务器端的变换中收集它，并希望在变换函数本身中为它创建一个动态蚀刻形状，
            而不是在这里这样做。这样，当一个形状被填充在一个更大的形状中时，
            新的 `dv_scan` 代码可以拾取外部形状。
        */
        if (db_isMultiUser() && db_mu_transform_active())
        {
            if (db_mu_should_create_clean_shape((dbptr_type)object_handle))
            {
                _cre_clean_shape_for_serial_or_parallel(object_handle, win); // 创建干净的形状
            }
        }
        else
        {
            _cre_clean_shape_for_serial_or_parallel(object_handle, win); // 创建干净的形状
        }
        _do_dyn_fill(object_handle, extFillMode);        // 执行动态填充
        utl_perfTuneOn_only_for_serial(PERF_MASK, NULL); // 打开性能调优
        if (curFillMode)
            dbs_global_dynamic_fill_only_for_serial(curFillMode); // 恢复全局动态填充模式
    }
    if (db_isMultiUser())
    {
        dbs_autovoid_active(oldAutovoidValue); // 恢复旧的自动填充值
    }
    return (SUCCESS); // 返回成功
}
long _do_dyn_fill(dbptr_type dyn_shape_ptr, int fillMode)
{
    // 定义并初始化错误码、缓冲区等变量。
    // 获取动态shape组并初始化缓冲区。
    // 初始化实例数据和扫描哈希表。
    // 根据填充模式获取网格或蚀刻shape。
    // 如果填充模式为完美或快速，初始化网格再生设置。
    // 遍历缓冲区中的shape：
    // 开始事务处理。
    // 根据shape类型进行不同的孔洞处理。
    // 根据错误码进行相应的处理和事务提交或回滚。
    // 清理资源并返回错误码。

    // 定义错误码，初始化为成功
    long error = SUCCESS;
    short shp_buf = 0;
    int i, cnt;
    int didPolyUnfill;
    int Prev_keep_net = 0;
    int succeeded = 0;
    dvInstData instData;
    dbptr_type dyn_grp;
    dbptr_type etch_shape_ptr;
    dba_transaction_mark_type start_mark;

    // 获取动态shape的组
    dyn_grp = _dyn_get_group_from_boundary(dyn_shape_ptr);
    // 初始化缓冲区
    bufinit(sizeof(dbptr_type), 50, 50, &shp_buf);

    // 初始化实例数据
    BZERO(&instData, sizeof(dvInstData));
    dv_init_all_scan_hash(&instData);

    /*
        在进行孔洞处理之前，我们必须获取shape列表。
        在此过程中，将有更多shape被添加到组中。
    */
    if (fillMode == DV_FILL_PERFECT && dba_dyn_shape_is_griddable(dyn_shape_ptr))
        dba_group_item_process(dyn_grp, _get_grid_pieces, (void *)&shp_buf);
    else
        dba_group_item_process(dyn_grp, _get_etch_shapes, (void *)&shp_buf);
    cnt = bufcount(shp_buf);

    // 仅用于串行模式的取消操作
    IMCancelOn_only_for_serial();

    // 如果填充模式为完美或快速
    if (fillMode == DV_FILL_PERFECT || fillMode == DV_FILL_FAST)
    {
        // 设置为仅用于串行模式的网格再生
        dv_grid_regenerating_only_for_serial(TRUE); // 串行或并行
        if (fillMode == DV_FILL_PERFECT)
        {
            // 初始化缓冲区和扫描集合
            bufinit(sizeof(dbptr_type), 1023, 1023, &instData.thermalBufferID);
            dv_init_scan_set(&instData.p_thermalsSet);
        }
    }

    // 遍历缓冲区中的shape
    for (i = 1; i <= cnt; i++)
    {
        bufget(shp_buf, i, &etch_shape_ptr);
        if (!dbg_IsDynamicFillDisabled())
        {
            // 开始事务
            start_mark = dba_transaction_start_only_for_serial();
            if (!dbg_ShapeIsActive(etch_shape_ptr))
            {
                didPolyUnfill = 1;
                dba_poly_unfill_for_serial_or_parallel(etch_shape_ptr);
            }
            else
                didPolyUnfill = 0;

            // 根据shape类型进行不同处理
            if (dbg_ShapeIsDynamic(dyn_shape_ptr) == DYNAMIC_CAVITY_FILL)
                error = dv_autovoid_cavity(SHAPE_PTR(etch_shape_ptr));
            else
            {
                Prev_keep_net = aud_cln_net_keep_net_only_for_serial(TRUE);

                /*
                    在调用动态shape更新之前，DRC缓冲区必须是干净的，
                    因为该工具依赖于DRC来确定需要避开的对象。
                */
                if (bufcount(DRC_BMS_INDEX))
                {
                    utl_add_drc_bufrset();
                }

                if (fillMode == DV_FILL_PERFECT)
                {
                    error = dv_autovoid_instance(SHAPE_PTR(etch_shape_ptr), &instData);
                }
                else
                {
                    error = dv_autovoid(SHAPE_PTR(etch_shape_ptr));
                }

                /*
                    shape孔洞后没有剩余的蚀刻片并不是错误。
                    但我们不再需要蚀刻shape，因此可以删除它。
                */
                if (fillMode == DV_FILL_FAST || fillMode == DV_FILL_PERFECT)
                {
                    if (error == DV_ERROR_noShapesLeft || error == DV_ERROR_shapeVoidedAway)
                    {
                        db_dshp_obs(SHAPE_PTR(etch_shape_ptr));
                        db_dshp_item_for_serial_or_parallel(SHAPE_PTR(etch_shape_ptr));
                    }
                    error = SUCCESS;
                }
                else
                {
                    if (error == DV_ERROR_noShapesLeft && cnt > 1)
                    {
                        db_dshp_obs(SHAPE_PTR(etch_shape_ptr));
                        db_dshp_item_for_serial_or_parallel(SHAPE_PTR(etch_shape_ptr));
                        error = SUCCESS;
                    }
                }

                aud_cln_net_keep_net_only_for_serial(Prev_keep_net);
            }
            if (UTL_IS_CANCEL)
            {
                // 如果操作被取消，回滚事务并禁用动态填充
                mh_printf_msg(SYSMSG_INFOSTRING, "Autovoid cancel. Disabling dynamic fill.");
                dba_transaction_rollback_only_for_serial(start_mark);
                if (!didPolyUnfill)
                    error = dba_poly_fill_for_serial_or_parallel(etch_shape_ptr, FALSE, NULL);
                if (dbg_design_flavor() == DESIGN_FLAVOR_ORCAD_X)
                {
                    dba_dynamic_shape_ood_all(FULL_FILL);
                }
                else
                {
                    dbs_global_dynamic_fill_only_for_serial(DV_FILL_DISABLED);
                }
            }
            else
                dba_transaction_commit_only_for_serial(start_mark);

            /*
                在发生错误的情况下，设置active_shape_ptr为NULL，
                因为dba_poly_unfill会设置它
            */
            if (didPolyUnfill)
                dbcom_->active_shape_ptr = NULL;

            if (error == -1 && cnt > 1)
            {
                if (blr_delete_autogen_shape_for_serial_or_parallel(dyn_shape_ptr, etch_shape_ptr) == SUCCESS)
                    error = SUCCESS;
                else if (!UTL_IS_CANCEL)
                {
                    ASSERT(printf(" Dynamic Shape has no ETCH left\n"));
                }
            }
            else if (error == SUCCESS)
                succeeded++;

            // 不将由于自动孔洞取消而设置的错误视为错误
            if (error && !UTL_IS_CANCEL)
            {
                ASSERT(printf(" Dynamic Shape has no ETCH left\n"));
                goto DONE;
            }
        }
        else
        {
            if (dbg_ShapeIsActive(etch_shape_ptr))
            {
                error = dba_poly_fill_for_serial_or_parallel(etch_shape_ptr, FALSE, NULL);
                if (error)
                    goto DONE;
                else
                    succeeded++;
            }
        }
    }
    // 清理资源
DONE:
    bufdestroy(shp_buf);
    return error;
}
long dv_autovoid(shape_type *shape_p)
{
    return dv_autovoid_instance(shape_p, NULL);
}
long dv_autovoid_instance(shape_type *shape_p, dvInstData *p_instData)
{
    // 定义和初始化各种变量和标记。
    // 如果是在AllegroX设计风格下进行操作，显式地将填充模式设置为WYSIWYG（所见即所得，平滑模式）。
    // 查询环境变量，确定是否使用额外的DRC命中设置。
    // 初始化并行或串行孔洞处理环境。
    // 关闭界面显示。
    // 如果需要，打开debug日志记录。
    // 开始性能调试计时器，记录孔洞处理的性能。
    // 设置急角覆盖工具的使用状态。
    // 调用孔洞处理的前期准备函数，包括数据库事务处理、扫描初始化等。
    // 如果孔洞处理已经完成，则直接跳转到完成标签（DONE）。
    // 如果前期处理成功，继续进行后续的孔洞生成处理。
    // 如果不是原型拷贝模式，释放F_POLYHEAD结构和相关内存。
    // 释放本地DRC哈希和扫描集合。
    // 如果发生错误或者操作被取消，设置标志以在后期处理中完成。
    // 调用孔洞处理的后期处理函数，处理生成的孔洞和相关资源。
    // 如果是AllegroX设计风格，恢复原先的全局动态填充模式。
    // 释放并行或串行孔洞处理环境。
    // 恢复界面显示，并重绘界面。
    // 如果debug日志是打开的，关闭日志。
    // 结束性能调试计时器，输出孔洞处理的性能信息。
    // 返回最终的错误码。

    // 定义各种变量和标记
    dba_transaction_mark_type start_mark = 0; // 数据库事务标记
    dbptr_type shapeNet_p = NULL;             // shape网络指针
    int scanInitializedLocally = FALSE;       // 是否本地初始化扫描
    int localThermals = FALSE;                // 是否本地热封
    int xtraDrcHit = 0;                       // DRC相关命中
    int prev_pad_suppress = FALSE;            // 前置pad抑制
    long error = SUCCESS;                     // 错误码
    short buffer_id = 0;                      // 缓冲区ID
    short newShapeBufferId = 0;               // 新shape缓冲区ID
    int finished = FALSE;                     // 是否完成
    int done_only_in_post = FALSE;            // 仅在后期完成
    int oldDisplay = TRUE;                    // 旧显示状态
    unsigned long timer = 0;                  // 计时器

    F_POLYHEAD *fhead = NULL;
    F_POLYHEAD *fkeepin = NULL;
    int shape_cnt = 0;                                             // shape计数
    int localDRCHash = 0;                                          // 本地DRC哈希
    dvInstData *p_locinstData = p_instData;                        // 本地数据实例指针
    unsigned long genholes_timer = 0;                              // 生成孔洞计时器
    int isAllegroX = dbg_design_flavor() == DESIGN_FLAVOR_ORCAD_X; // 是否是AllegroX设计风格

    int reset_mode; // 重置模式

    /*
        在进行全局填充（full pour）操作时，当使用AllegroX软件时，显式地将填充模式设置为WYSIWYG（所见即所得，平滑模式）。
        这将确保生成的shape处于可以直接用于艺术品的状态。
    */
    if (isAllegroX)
    {
        reset_mode = dbg_global_dynamic_fill();
        dbs_global_dynamic_fill(DV_FILL_WYSIWYG);
    }

    // 查询变量，判断是否需要使用并行
    xtraDrcHit = dv_SYEnvIsSet_for_serial_or_parallel("dv_SameNetDrc");

    // 初始化串行或并行环境
    av_init_for_serial_or_parallel();

    // 关闭显示
    oldDisplay = utl_dispena(FALSE);

    // 如果请求了debug日志记录，打开日志
    _dv_debug = dv_SYEnvIsSet_for_serial_or_parallel("dv_debug");
    if (_dv_debug)
        dv_openlog();

    // 开始性能调试计时
    dv_performanceDebugUpdate(DV_START_TIMER | DV_PRINT_TIMER, "Performing complete autovoid", shape_p, &timer);

    // 设置急角覆盖工具使用状态
    dv_setAcuteAngleCoverInUse();

    // 调用实际的孔洞处理函数，进行孔洞处理的前期准备
    error = dv_autovoid_instance_head(shape_p, &p_locinstData, xtraDrcHit, &scanInitializedLocally,
                                      p_locinstData && p_locinstData->params_p ? p_locinstData->params_p->global_ignore_pad_suppress : FALSE,
                                      &prev_pad_suppress,
                                      &localThermals, &start_mark,
                                      &newShapeBufferId, &shapeNet_p, &finished,
                                      &fhead, &fkeepin, &shape_cnt, &localDRCHash, &genholes_timer);

    // 如果孔洞处理完成，则直接跳转到DONE标签
    if (finished)
        goto DONE;

    // 如果前期处理成功，继续进行后续的孔洞生成处理
    if (error == SUCCESS)
        error = dv_autovoid_post_genholes(shape_p, p_locinstData,
                                          shapeNet_p,
                                          xtraDrcHit,
                                          newShapeBufferId,
                                          fkeepin,
                                          &shape_cnt, &fhead, &genholes_timer);

    // 如果不是原型拷贝模式，释放F_POLYHEAD结构和相关内存
    if (dv_protoGetShapeMode() != protoNewShapes::protoCopy)
    {
        if (fhead != NULL)
            f_killpolyList(fhead);
        fhead = NULL; // 清空内存，避免内存泄漏
    }

    if (fkeepin != NULL)
        f_killPolyList(fkeepin);

    if (localDRCHash)
        dv_free_scan_set(&p_locinstData->p_drcSet);

    // 如果发生错误或者操作被取消，设置done_only_in_post为TRUE
    if ((error != SUCCESS) || UTL_IS_CANCEL)
    {
        done_only_in_post = TRUE;
    }

    // 调用孔洞处理的后期处理函数
    (void)dv_autovoid_instance_post(shape_p, &p_locinstData,
                                    done_only_in_post, shapeNet_p, newShapeBufferId,
                                    scanInitializedLocally, localThermals, start_mark,
                                    p_locinstData && p_locinstData->params_p ? p_locinstData->params_p->global_ignore_pad_suppress : FALSE,
                                    prev_pad_suppress);

DONE:
    // 如果是AllegroX设计风格，恢复全局动态填充模式
    if (isAllegroX)
    {
        dbs_global_dynamic_fill(reset_mode);
    }

    // 释放串行或并行环境资源
    av_free_for_serial_or_parallel();

    // 恢复显示状态，并且重绘界面
    utl_dispena(oldDisplay);
    if (oldDisplay)
        utl_dispRedraw();

    // 如果debug日志是打开的，关闭日志
    if (_dv_debug)
        dv_closelog();

    // 结束性能调试计时器
    dv_performanceDebugUpdate(DV_END_TIMER | DV_PRINT_TIMER, "Autovoid complete", shape_p, &timer);

    // 返回错误码
    return error;
}
long dv_autovoid_instance_head(
    shape_type *shape_p,                     // 形状指针
    dvInstData **pp_instaData,               // 动态实例数据指针的指针
    int xtraDrcHit,                          // 额外的 DRC 命中数
    int *p_scanInitializedLocally,           // 本地扫描初始化标志的指针
    int ignore_pad_suppress,                 // 忽略焊盘抑制标志
    int *p_prev_pad_suppress,                // 先前焊盘抑制标志的指针
    int *p_localThermals,                    // 本地热量标志的指针
    dba_transaction_mark_type *p_start_mark, // 事务标记指针
    short *p_newShapeBufferId,               // 新形状缓冲区 ID 的指针
    dbptr_type *p_shapeNet_p,                // 形状网络指针
    int *p_finished,                         // 完成标志的指针
    F_POLYHEAD **p_fhead,                    // 多边形头指针
    F_POLYHEAD **p_fkeepin,                  // 保持多边形头指针
    int *p_shape_cnt,                        // 形状计数的指针
    int *p_localDRCHash,                     // 本地 DRC 哈希的指针
    unsigned long *p_genholes_timer          // 生成孔的计时器指针
)
{
    dvInstData *p_instData = NULL;
    dba_object_handle boundary_p = NULL;      // 对象处理器，边界
    dba_object_handle group_p = NULL;         // 对象处理器，组
    dba_transaction_mark_type start_mark = 0; // 事务标记
    dbptr_type br_ptr;                        // 元对象指针
    dbptr_type shapeNet_p = NULL;             // 网路指针
    int scanInitializedLocally = FALSE;
    int localThermals = FALSE;
    long error = SUCCESS;
    long loc_error = SUCCESS;
    short buffer_id = 0;
    short newShapeBufferId = 0;
    dbrep x_hatch_border_width = 0; // x 盖板宽度
    int extMode;
    int baseMode;

    // 自动挖孔低层数据
    F_POLYHEAD *fhead = NULL;
    F_POLYHEAD *fkeepin = NULL;
    int shape_cnt = 0;
    int localDRCHash = 0;

    // 如果不在所见即所得模式，设置所见即所得为过时
    dba_dyn_shape_mode_extended_for_serial_or_parallel(&extMode, &baseMode);

    if (baseMode != DV_FILL_WYSIWYG)
        boundary_p = dba_dynamic_shape_get_boundary(shape_p); // 获取 shape 的边界

    // 初始化指针参数
    if (p_scanInitializedLocally)
        *p_scanInitializedLocally = scanInitializedLocally;

    if (p_localThermals)
        *p_localThermals = localThermals;

    if (p_start_mark)
        *p_start_mark = start_mark;

    if (p_newShapeBufferId)
        *p_newShapeBufferId = newShapeBufferId;

    if (p_shapeNet_p)
        *p_shapeNet_p = shapeNet_p;

    if (p_finished)
        *p_finished = FALSE;

    if (p_fhead)
        *p_fhead = NULL;

    if (p_fkeepin)
        *p_fkeepin = NULL;

    if (p_shape_cnt)
        *p_shape_cnt = 0;

    if (p_localDRCHash)
        *p_localDRCHash = 0;

    // 确保给定 shape 可以避让
    if ((shape_p->shape_fill == SHAPE_UNFILLED) ||
        (!utl_ok2void(ELEMENT_CLASS(shape_p), ELEMENT_SUBCLASS(shape_p))))
    {
        if (p_finished)
            *p_finished = TRUE;
        return (-1);
    }

    if (DELETE_MASK(shape_p))
    {
        if (p_finished)
            *p_finished = TRUE;
        return (SUCCESS);
    }

    // 处理实例数据
    if (pp_instData)
    {
        p_instData = *pp_instData;
        if (p_instData == NULL)
        {
            scanInitializedLocally = TRUE;
            p_instData = static_cast<dvInstData *>(SYMalloc(sizeof(dvInstData)));
            BZERO(p_instData, sizeof(dvInstData));
            dv_init_all_scan_hash(p_instData); // 初始化扫描哈希

            *pp_instData = p_instData;

            if (p_scanInitializedLocally)
                *p_scanInitializedLocally = scanInitializedLocally;
        }
    }

    if (SYEnvIsSet("dv_drc_iterations"))
        dv_SYGetEnvIntMinMax_for_serial_or_parallel("dv_drc_iterations", &MAXITERATIONS, 3, 15, 1);

    // 获取父动态 shape 组
    loc_error = dba_object_group_owner_process(shape_p,
                                               (long int (*)())dv_find_dynamic_shape, (void *)&group_p);

    // 在进行多线程网格化时，需要删除以下块
    if (baseMode != DV_FILL_WYSIWYG)
    {
        dbs_shape_dyn_mask_for_serial_or_parallel(boundary_p, DYNAMIC_FILL_OOD_WYSIWYG, TRUE);

        // 如果在网格粗糙模式下，确保这不是第一次从平滑到网格的 shape 更新
        if (extMode == DV_FILL_FAST)
        {
            if (dba_dyn_shape_init_fracture_for_serial_or_parallel(boundary_p))
            {
                error = dv_autovoid(SHAPE_PTR(boundary_p));
                if (p_finished)
                    *p_finished = TRUE;
                return error;
            }
        }
    }

    // 为给定 shape 获取填充参数
    p_instData->params_p = dba_dynamic_fill_params_get(group_p);

    // 忽略焊盘抑制
    if ((p_instData->params_p->global_ignore_pad_suppress || ignore_pad_suppress) && *p_prev_pad_suppress)
        *p_prev_pad_suppress = utl_suppress_pad_disable_prev_only_for_serial(TRUE);

    // 为 poly_2_shape 工具设置边界宽度
    x_hatch_border_width = (SHAPE_IS_HATCHED(shape_p)) ? p_instData->params_p->x_hatch_border_width : 0;
    xhatch_val_set(shape_p, x_hatch_border_width);

    // 获取Shape网络
    SHAPE_OWNER(FRECTANGELE_PTR(shape_p), br_ptr);
    if (br_ptr != NULL)
    {
        BRANCH_OWNER(br_ptr, shapeNet_p);
        if (p_shapeNet_p)
        {
            *p_shapeNet_p = shapeNet_p;
        }
    }
    if (shapeNet_p != NULL)
        DV_FPRINTF(_dvDebugLogFP, "Shape net: %s\n", NET_PTR(shapeNet_p)->net_name);

    // 初始化静态变量
    num_blurred_voids_map_set(shape_p, 0);

    if (p_instData->params->global_ignore_pad_suppress || ignore_pad_suppress)
        ignore_pad_suppress = utl_suppress_pad_disable_prev_only_for_serial(TRUE);

    // 开始事务
    start_mark = dba_transaction_start_only_for_serial();
    if (p_start_mark)
        *p_start_mark = start_mark;

    if (dv_protoGetShapeMode() != protoNewShapes::protoOff)
    {
        if (!p_instData->p_thermalsSet)
        {
            dv_init_scan_set(&p_instData->p_thermalsSet);
            localThermals = TRUE;
            if (p_localThermals)
                *p_localThermals = localThermals;
        }

        // 初始化缓冲区存储避让后的 shape
        bufinit(sizeof(dbptr_type), 50, 50, &newShapeBufferId);
        if (p_newShapeBufferId)
        {
            *p_newShapeBufferId = newShapeBufferId;
        }
        dv_deleteShape_for_serial_or_parallel(shape_p);

        fhead = dv_protoGetShapePoly(shape_p, p_instData);
        shape_cnt = dv_minAreaFilter(&fhead, p_instData->params_p->min_area, dvDebugLogFP, FALSE);
    }
    else
    {
        // 初始化缓冲区用于持有被挖空的对象
        bufinit(sizeof(dbptr_type), 100, 100, &buffer_id);

        // 调用 find 收集所有在 shape 下的元素
        dv_fillbuf(shape_p, p_instData, 0.0, buffer_id); // SERIALLY_OR_PARALLELY: 处理哈希表的标记
        // 检查是否需要在外部搜索高优先级的边界形状
        if (dv_isSurroundingBoundaryCheck_for_serial_or_parallel() || extMode == DV_FILL_FAST || extMode == DV_FILL_PERFECT)
        {
            dv_findAdnAddNonBlurHigherPriorityShapes(shape_p, buffer_id);
        }

        // 移除缓冲区中优先级低于被挖空形状的动态形状
        loc_error = dv_removeLowerPriorityShapes(shape_p, buffer_id); // SERIALLY_OR_PARALLELY: 可能 OK
        // 清除引脚的状态标志
        if (!p_instData->p_thermalsSet)
        {
            dv_init_scan_set(&p_instData->p_thermalsSet);
            localThermals = TRUE;
            if (p_localThermals)
                *p_localThermals = localThermals;
        }

        // 初始化缓冲区存储新避让的形状
        bufinit(sizeof(dbptr_type), 50, 50, &newShapeBufferId);
        if (p_newShapeBufferId)
            *p_newShapeBufferId = newShapeBufferId;

        error = dv_autovoid_genholes(shape_p, p_instData, buffer_id, shapeNet_p, newShapeBufferId, xtraDrcHit, &shape_cnt, &localDRCHash, &fhead, &fkeepin, p_genholes_timer);
    }

    if (p_fhead)
        *p_fhead = fhead;
    if (p_fkeepin)
        *p_fkeepin = fkeepin;
    if (p_shape_cnt)
        *p_shape_cnt = shape_cnt;
    if (localDRCHash)
        *p_localDRCHash = localDRCHash;

    buffree(buffer_id);
    return (error);
}

/*
    This sees if the shape being added is clipped by the route ki.
    If clipped it creates a new outline restricted to the ki.
    KI is shrunk by 1 db unit to avoid DRCs.
    Note this is only called for dynamic shapes
*/
F_POLYHEAD *dv_orrki(shape_type *shp, F_POLYHEAD *psh, int removeOutside, int *p_isClipped)
{
    F_POLYHEAD *pshp, *prki, *prkit, *pres, *permHole;
    shape_type *rki = SHAPE_PTR(dbcom_->etch_keepin_root);
    shape_type rki_shp;
    line_segment_type segs[4];
    f_db2point dumxy1, dumxy2;
    double contract;
    double dist = MAX_FLOAT_VALUE;
    long status;

    if (p_isClipped)
        *p_isClipped = FALSE;

    if (!rki || !shp)
        return psh;
}

long dv_autovoid_genholes(shape_type *shape_p, dvInstData *p_instData,
                          int buffer_id,
                          dbptr_type shapeNet_p,
                          int newShapeBufferId,
                          int xtraDrcHit,
                          int *shape_cnt,
                          int *p_localDRCHash,
                          F_POLYHEAD **p_fhead, F_POLYHEAD **p_fkeepin,
                          unsigned long *p_timer)
{
    long error = SUCCESS;
    F_POLYHEAD *fhead = NULL;
    F_POLYHEAD *consRegHead = NULL;
    F_POLYHEAD *tempHead = NULL;
    F_POLYHEAD *fkeepin = NULL;
    int callFromDRC = FALSE;
    int doWYSIWYG;
    dba_object_handle boundary_p = NULL;
    int extMode;
    int gridTempPiece = FALSE;
    int isRKIClipped = FALSE;
    short buf_id = 0;
    int buf_count = 0;
    dbptr_type shape_ptr;
    int intersect_count = 0;
    db2point shapePt;
    int i = 0;

    // 初始化返回值
    *shape_cnt = 0;

    if (!p_instData->p_drcSet)
    {
        // 初始化DRC扫描设置
        dv_init_scan_set(&p_instData->p_drcSet);
        if (p_localDRCHash)
            *p_localDRCHash = TRUE;
    }

    // 读取整个要挖孔的形状，并将其转换为多边形结构
    fhead = dv_readshp(shape_p, FALSE, NULL);
    if ((fhead == NULL) || (fhead->APoint == NULL) || DELETE_MASK(shape_p))
    {
        error = -1;
        goto DONE;
    }

    if (dbg_shape_dyn_mask(shape_p, SHP_GRID_FILL_DATA))
        gridTempPiece = TRUE;
    dba_dyn_shape_mode_extended(&extMode, NULL);

    if (!gridTempPiece)
        dv_performanceDebugUpdate(DV_START_TIMER, NULL, NULL, p_timer);

    /*
        路由保持修剪，如果网格再生处于活动状态，修剪已经完成。
        不要浪费时间重复执行。
    */
    if (!dv_grid_regenerating(-1))
    {
        fhead = dv_orrki(shape_p, fhead, FALSE, &isRKIClipped);
        /*
            需要保留修剪后的多边形的未修改副本，以便稍后验证修剪/平滑是否超出范围。
            但如果没有剪裁到它，不要浪费时间和内存。
        */
        if (fhead && isRKIClipped)
        {
            F_POLYHEAD *p_walker;
            fkeepin = fpoly_CopyPolyList(fhead);
            // 但不需要孔。
            for (p_walker = fkeepin; p_walker; p_walker = p_walker->next)
            {
                if (p_walker->NextHole)
                {
                    f_killPolyList(p_walker->NextHole);
                    p_walker->NextHole = NULL;
                }
            }
        }
    }
    if (SYGetEnv("dv_cons_region_precise"))
    {
        buf_id = bufinit0(sizeof(dbptr_type), 100, 500);

        // 在缓冲区中填充所有约束区域形状
        dba_db_process(DBA_DBPROC_OBS, (APFL)qfindGetConsRegions, &buf_id);
        // 遍历缓冲区中的所有项目
        buf_count = bufcount(buf_id);
        p_instData->params_p->single_cons_region = TRUE;
        for (int i = 1; i <= buf_count; i++)
        {
            bufget(buf_id, i, (dbptr_type)&shape_ptr);
            if (ELEMENT_SUBCLASS(shape_ptr) == ELEMENT_SUBCLASS(shape_p))
            {
                consRegHead = dv_readshp(SHAPE_PTR(shape_ptr), FALSE, NULL);
                // 检查形状是否完全在约束区域内
                tempHead = f_DoLogicalOperation(fhead, LANDNOT, consRegHead);
                if (!tempHead)
                    continue;
                else
                {
                    // 检查形状是否与约束区域相交
                    tempHead = f_DoLogicalOperation(fhead, LAND, consRegHead);
                    if (tempHead)
                    {
                        p_instData->params_p->single_cons_region = FALSE;
                        break;
                    }
                }
            }
        }
        if (p_instData->params_p->single_cons_region)
        {
            // 将形状段顶点的第一个点设置为形状上的一个点
            shapePt.db2x = 0;
            shapePt.db2y = 0;
            if (shape_p->first_outline_segment)
                shapePt = LINE_SEG_PTR(shape_p->first_outline_segment)->vertex1;
            p_instData->params_p->point_on_shape.db2x = (double)shapePt.db2x;
            p_instData->params_p->point_on_shape.db2y = (double)shapePt.db2y;
        }
        buffree(buf_id);
    }
    // 现在删除轮廓段 -- 需要保留以进行 dv_orrki 测试
    error = dv_deleteShape_for_serial_or_parallel(shape_p);
    if (error)
        goto DONE;

    // 从形状的参数设置平滑标志
    doWYSIWYG = (p_instData->params_p->global_dynamic_fill == DV_FILL_WYSIWYG) ? TRUE : FALSE;

    if (extMode != DV_FILL_PERFECT && extMode != DV_FILL_FAST)
    {
        boundary_p = dba_dynamic_shape_get_boundary(shape_p);
        dba_dynamic_shape_clear_grid_regions_for_serial_or_parallel(boundary_p);
    }

    if (fhead)
    {
        int doClean = (doWYSIWYG && !gridTempPiece) ? TRUE : FALSE;
        // DVI_NEW_FAST
        // 如果我们处于新 FAST 模式，覆盖标志
        if (dba_dyn_shape_mode_fast())
        {
            doClean = TRUE;
        }
        // 创建孔洞并将其与形状结构合并
        error = dv_genholes(shape_p, p_instData, &fhead, shapeNet_p, buffer_id,
                            callFromDRC, doClean, NULL, FALSE);
        if ((error < 0) || UTL_IS_CANCEL)
            goto DONE;

        // 过滤小形状
        if (extMode != DV_FILL_PERFECT)
        {
            *shape_cnt = dv_minAreaFilter(&fhead, p_instData->params_p->min_area, _dvDebugLogFP, FALSE);
        }
        else
            *shape_cnt = 1;
    }
DONE:
    if (p_fkeepin)
        *p_fkeepin = fkeepin;

    if (p_fhead)
        *p_fhead = fhead;

    return error;
}

long dv_get_all_voids_in_serial_or_parallel(
    long buffer_id,
    const FXYTREE *tree;
    shape_type * shape_p;
    const XYTREE *boundaryTree, // xytree of shape boundary
    dvInstData *p_instData,
    const F_POLYHEAD *head,
    box_type_ptr poly_ext_ptr,
    dbptr_type shp_net,
    double expandAdjustment,
    int callFromDRC,
    short groupViaBuf_id,
    F_POLYHEAD **voids,
    int error_handling_scheme,
    int callFromFast)
{

    long error = SUCCESS;
    clock_t c_start = clock();
    dvSeriaExecutrix &dve = dvSerialExecutrix::getInstance();
    static int forceSerial = TRUE;

    if (forceSerial || dve.getParallelShapesMode() == false)
    {
        error = dv_get_all_voids_in_serial(buffer_id, tree, shape_p, boundaryTree,
                                           p_instData, head, poly_ext_ptr, shp_net, expandAdjustment, callFromDRC,
                                           groupViaBuf_id, voids, error_handling_scheme, callFromFast)
    }
    else
    {
        error = dv_get_all_voids_in_parallel(buffer_id, tree, shape_p, boundaryTree,
                                             p_instData, head, poly_ext_ptr, shp_net, expandAdjustment, callFromDRC,
                                             groupViaBuf_id, voids, error_handling_scheme, callFromFast)
    }

    clock_t c_end = clock();
    double delta = (c_end - c_start) * 1.0;
    double delta_secs = delta / CLOCKS_PER_SEC;

    return error;
}

static long dv_get_all_voids_in_serial(
    long buffer_id,
    const FXYTREE *shpTree,
    shape_type *shape_p,
    const XYTREE *boundaryTree,
    dvInstData *p_instData,
    const F_POLYHEAD *head,
    box_type_ptr poly_ext_ptr,
    dbptr_type shp_net,
    double expandAdjustment,
    int callFromDRC,
    short groupViaBuf_id,
    F_POLYHEAD **retVoids,
    int error_handling_scheme,
    int callFromFast)
{
    int cnt = bufcount(buffer_id);
    long error = SUCCESS;
    int shp_changed = FALSE;
    int nVoids = 0, nNewVoids = 0, nPrevVoids = 0;

    if (cnt == 0)
        return error;

    std::vector<F_POLYHEAD *> allVoids;
    for (int item = 1; item <= cnt; item++)
    {
        dbptr_type elem_ptr;
        int error = SUCCESS;
        bufget(buffer_id, item, &elem_ptr);
        F_POLYHEAD *extractedVoids = NULL;

        error = dv_makehole(shpTree, elem_ptr, shape_p, boundaryTree, p_instData,
                            head, poly_ext_ptr, shp_net, expandAdjustment,
                            callFromDRC, groupViaBuf_id, FALSE, FALSE, callFromFast, FALSE, &extractedVoids);
        if (extractedVoids)
            allVoids.push_back(extractedVoids);

        if (error_handling_scheme == 0)
        { // first set of calls to dv_makehole from dv_genholes
            if ((error < 0) || UTL_IS_CANCEL)
                return error;
            else if (error > 0)
                assert(FALSE);
        }
        else
        {
            if (error < 0)
                return error;
            else if (error > 0)
                shp_changed = TRUE; // keep going, according to the original code(thus error overwrite)
        }
    }
    for (auto x : allVoids)
    {
        F_POLYHEAD *next;
        for (auto lp = x; lp != NULL; lp = next)
        {
            next = lp->next;
            lp->next = NULL;
            /*
                Could possibly do additional checking here, it might same time in polybool later
                e.g. cline voiding checks if (fpoly_Loop2Shp(shpTree, head, lp)!=PT_OUTSIDE)
                can't do quite the same here because sometimes lp could be a shape that encompasses
                head, which returns PT_OUTSIDE, ye needs voided
                Should try this when there is time with an extra point in loop check
                where point is any point from head and loop is lp
            */
            *retVoids = fpoly_mergeloop(*retVoids, lp);
        }
    }
    if (shp_changed)
    {
        /*
            signal the caller that shape has changed.
            Trying to keep the structure of the non parallelized code
        */
        error = 1;
    }
    return error;
}

long dv_PinPattern_for_serial_or_parallel(
    FXYTREE *tree,
    dvInstData *p_instData,
    dbptr_type shp_net,
    dbptr_type shp_ptr,
    F_POLYHEAD *shp,
    F_POLYHEAD **voids,
    int buffer_if,
    long items_in_buf,
    double expandAdjustment,
    double SmoothExpand,
    int callFromDRC)
{
    dvSerialExecutrix &dve = dvSerialExcutrix::getInstance();
    long retVal = 0;
    if (dve.getParallelShapesMode())
    {
        currentMutex_t::scoped_lock lock(serialMutex);
        // serialMutex.lock();
        // allegroLock();
        retVal = dv_PinPattern(tree, p_instData, shp_net, shp_ptr, shp, voids, buffer_id,
                               items_in_buf, expandAdjustment, SmoothExpand, callFromDRC);
        // allegroUnlock();
        // serialMutex.unlock();
    }
    else
    {
        retVal = dv_PinPattern(tree, p_instData, shp_net, shp_ptr, shp, voids, buffer_id,
                               items_in_buf, expandAdjustment, SmoothExpand, callFromDRC);
    }
    return retVal;
}

void dv_rmv_s_arcs(F_POLYHEAD *head, dbrep aper)
{
    F_POLYHEAD *h;
    F_POLYELEM *p;
    double aper_rad;

    /*
        Look for s arcs...ie one is CW and other is CCW then if one is
        exactly equal to min aperture size and other is much bigger, then
        flattern the min aper one.
     */
    aper_rad = aper / 2;
    for (h = head; h != NULL; h = h->next)
    {
        if ((p = h->Apoint) == NULL)
            continue;
        do
        {
            if ((p->radius != 0.0) && ABS(p->radius) <= aper_rad)
            {
                if (ABS(p->Forwards->radius) > aper)
                {
                    if (fpoly_PolyFArcIsCCW(p) ^ fpoly_PolyFArcIsCCW(p->Forwards))
                        p->radius = 0.0;
                }
                if (ABS(p->Backwards->radius) > aper)
                {
                    if (fpoly_PolyFArcIsCCW(p) ^ fpoly_PolyFArcIsCCW(p->Backwards))
                        p->radius = 0.0;
                }
            }
            p = p->Forwards;
        } while (p != h->APoint);
    }
}

long dv_genholes(shape_type *shape_p, dvInstData *p_instData,
                 F_POLYHEAD **head, dbptr_type shp_net, int buffer_id, int callFromDRC, int doSmooth, F_POLYHEAD **newVoids, int callFromFast)
{
    F_POLYHEAD *voids = NULL;
    F_POLYHEAD *h;
    F_POLYHEAD *tmpHead;
    FXYTREE *tree;
    long error = SUCCESS;
    dbptr_type elem_ptr;
    long item;
    int cnt;
    long num_edges;
    double expandAdjustment;
    double SmoothExpand;
    int shp_changed = FALSE;
    int merged = FALSE;
    int cl;
    shape_type *boundary_p = NULL;
    box_type poly_ext;
    box_type_ptr poly_ext_ptr = NULL;
    short groupViaBuf_id = 0;
    short shpInShp_bufId = 0;
    short serialOrParallelShp_bufId = 0;
    db_state_flags pin_x_area_state_flag = 0;
    int localPinVoid = FALSE;
    dba_dynfill_params *params_p = p_instData->params_p;
    XYTREE *boundaryTree = NULL;
    bufinit(sizeof(dbptr_type), DV_BUFSIZE_ETCH, DV_BUFSIZE_ETCH, &serialOrParallelShp_bufId);

    // Determine how many elements are in buffer
    // 确定缓冲区中的元素数量
    cnt = bufcount(buffer_id);
    if (cnt < 1)
        goto DONE1;
    cl = ELEMENT_CLASS(shape_p);

    if (!callFromDRC && cl == ETCH)
    {
        // Preprocess pins that cross constraint areas
        // 预处理跨越约束区域的引脚
        pin_x_area_state_flag = dv_checkout_x_pin_area_for_serial_or_parallel(shape_p, buffer_id);
    }

    // Create an FXY tree
    // 创建一个FXY树
    tree = make_fxytree(0.0);

    // Get expansion adjustment value
    // 获取扩展调整值
    expandAdjustment = dv_ExpandAdjust(shape_p, params_p);

    // Initialize padstack problem error message storage
    // 初始化padstack问题错误消息存储
    if (cl == ETCH)
    {
        dv_padreport_init();
    }

    /*
        Read shp structure into tree structure for inside/outside checking.
        Note that this is only used to determine if a given object is inside
        or outside of the shape. This can sometimes gives incorrect results
        if the object is already inside a void. This is a particular problem
        if voiding a crosshatch shape with "Snap Voids to Hatch Grid" turned
        on since many voids will likely have overlapping segments, which
        confuses things further. To simplify, just put the shape outline(s)
        in the tree(ignore the voids).

        将形状结构读取到树结构中，以便进行内外部检查。
        请注意，这仅用于确定给定对象是位于形状内部还是外部。如果对象已经位于空洞内，有时会给出错误结果。
        这在对交叉阴影形状进行空洞处理时尤其成问题，因为“对齐空洞到网格”打开时，许多空洞可能会有重叠段，这会进一步混淆结果。
        为了简化，只将形状轮廓放入树中（忽略空洞）。
    */
    fpoly_PToHead(*head);
    for (h = *head; h != NULL; h = h->next)
    {
        // Save the voids and remove them from the shape
        // 保存空洞并从形状中移除它们
        tmpHead = h->NextHole;
        h->NextHole = NULL;

        // Register the shape outline in the tree
        // 在树中注册形状轮廓
        fpoly_loadtree(h, tree);

        // Put the voids back on the shape
        // 将空洞放回形状中
        h->NextHole = tmpHead;
    }
    rebalance_fxytree(&tree);

    /*
        Make first pass to process other shapes. They will modify the
        polygon boundary, so we need to re-create the tree after processing
        all shapes, since every element is checked against the tree.

        初次处理其他形状。它们将修改多边形边界，因此在处理所有形状后需要重新创建树，因为每个元素都要与树进行检查。
    */
    shp_changed = FALSE;

    // Get extents of shape being voided
    // 获取正在进行空洞处理的形状的范围
    if (head && *head)
    {
        poly_ext_ptr = &poly_ext;
        fpoly_getextents_box(*head, poly_ext_ptr);
        if ((*head)->next != NULL)
        {
            box_type next_ext;
            box_type_ptr next_ext_ptr;
            /*
                Normally there is just a single poly input. However, for rki
                clipping Logical operations on the shape against the rki could
                have fractured the original shape poly into sub polys. So get
                extent of all parts.

                通常只有一个多边形输入。然而，对于rki剪辑，形状上的逻辑操作可能会将原始形状多边形分割成子多边形。因此获取所有部分的范围。
            */
            next_ext_ptr = &next_ext;
            for (h = (*head)->next; h != NULL; h = h->next)
            {
                fpoly_getextents_box(h, next_ext_ptr);
                ext_union(poly_ext_ptr, next_ext_ptr, poly_ext_ptr);
            }
        }
    }
    boundary_p = SHAPE_PTR(dba_dynamic_shape_get_boundary(shape_p));

    // Get boundary tree
    // 获取边界树
    boundaryTree = dv_GetBoundaryTree(boundary_p);

    for (item = 1; item <= cnt; item++)
    {
        bufget(buffer_id, item, &elem_ptr);

        // Screen out non-shapes
        // 筛选非形状元素
        if (ELEMENT_MASK(elem_ptr) != SHAPE)
            continue;

        // Don't process shape under edit or its parent boundary.
        // 不处理正在编辑的形状或其父边界
        if ((elem_ptr == shape_p) || (elem_ptr == boundary_p))
            continue;
        bufpend(serialOrParallelShp_bufId, &elem_ptr);
    }
    error = dv_get_all_voids_in_serial_or_parallel(serialOrParallelShp_bufId, tree,
                                                   shape_p, boundaryTree, p_instData, head, poly_ext_ptr, shp_net,
                                                   expandAdjustment, callFromDRC, 0, &voids, 0, callFromFast);

    if ((error < 0) || UTL_IS_CANCEL)
        goto DONE;
    else if (error > 0)
        ASSERT(FALSE);

    /*
        Process pins here if inline-pins are requested in parameters.
        note a value of greater than zero for the inline parameter indicates
        inlining is active and the value supplied is the maximum pin
        spacing allowable for pins to be inlined.

        如果参数中请求了内联引脚，这里处理引脚。
        注意：内联参数大于零表示内联是活动的，提供的值是引脚可以内联的最大间距。
    */
    if (cl == ETCH)
    {
        if (params_p->inline_pin_voids > 0)
        {
            error = dv_group_for_serial_or_parallel(tree, p_instData, shp_net, shape_p,
                                                    *head, &voids, buffer_id, cnt, callFromDRC);
        }
        else
        {
            if (!p_instData->p_pinVoidSet)
            {
                dv_init_scan_set(&p_instData->p_pinVoidSet);
                localPinVoid = TRUE;
            }

            // Add in adjustments that would also cause a pattern merge
            // 加入会导致模式合并的调整
            if (doSmooth)
                dv_GetSmoothParam(params_p, TRUE, &SmoothExpand);
            else
                SmoothExpand = 0.0;

            SmoothExpand += SmoothExpand; // both elements bump up by smooth
            // 两个元素都被平滑调整
            error = dv_PinPattern_for_serial_or_parallel(tree, p_instData, shp_net, shape_p, *head,
                                                         &voids, buffer_id, cnt, expandAdjustment, SmoothExpand, callFromDRC);
        }
    }
    /*
        Create voids for every element in buffer (except shapes which were done above).
        为缓冲区中的每个元素创建空洞（除上述处理过的形状外）。
    */

    bufrset(serialOrParallelShp_bufId);
    for (item = 1; item <= cnt; item++)
    {
        // Get next item in the buffer
        // 获取缓冲区中的下一个元素
        bufget(buffer_id, item, &elem_ptr);

        // We've already processed shapes in loop above
        // 在上面的循环中我们已经处理了形状
        if (ELEMENT_MASK(elem_ptr) == SHAPE)
            continue;

        // We've voided pin patterns above
        // 我们已经在上面对引脚模式进行了空洞处理
        if (ELEMENT_MASK(elem_ptr) == VAR_PIN &&
            dv_is_scan_set_item(elem_ptr, p_instData->p_pinVoidSet))
            continue;

        // We only want vias and thermal reliefs here
        // 我们这里只需要过孔和热减容
        bufpend(serialOrParallelShp_bufId, &elem_ptr);

        if (groupViaBuf_id == 0 && ELEMENT_MASK(elem_ptr) == VIA)
            bufinit(sizeof(dbptr_type), DV_BUFSIZE_ETCH, DV_BUFSIZE_ETCH, &groupViaBuf_id);
    }
    error = dv_get_all_voids_in_serial_or_parallel(serialOrParallelShp_bufId, tree,
                                                   shape_p, boundaryTree, p_instData, head, poly_ext_ptr, shp_net,
                                                   expandAdjustment, callFromDRC, groupViaBuf_id, &voids, 1, callFromFast);

    if (groupViaBuf_id != 0)
        buffree(groupViaBuf_id);

    if (error < 0)
        goto DONE;
    else if (error > 0)
    {
        error = SUCCESS;
        shp_changed = TRUE;
    }

    num_edges = dv_num_edges(voids);

    if ((num_edges == 0) && (!shp_changed))
    {
        /*
            shapes did not change. but could still be multiple polys in the
            list if clipped to boundary or have been smoothed. Merge them all.
            Check if need to smooth and merge if asked to do so.

            形状没有改变，但如果剪辑到边界或已被平滑，列表中仍可能有多个多边形。合并它们。
            检查是否需要平滑并合并（如果要求这样做）。
        */
        if (head != NULL && *head != NULL && doSmooth)
        {
            merged = TRUE;
            *head = dv_dosomething(*head, params_p, TRUE, 1, dba_dynamic_shape_get_boundary(shape_p));
        }
        goto DONE;
    }

    // Add in original edges
    // 加入原始边缘
    num_edges += dv_num_edges(*head);

    if (num_edges > 0)
    {
        if (voids != NULL)
        {
            fpoly_CleanUp(head);
            fpoly_CleanUp(&voids);

            // This removes internal arcs and does polygon smoothing
            // 这会移除内部弧并进行多边形平滑处理
            if (params_p->smooth_min_gap)
                dv_rmv_s_arcs(*head, params_p->smooth_min_gap);

            merged = TRUE;

            if (newVoids)
                *newVoids = fpoly_CopyPolyList(voids);
            error = dv_mergefpoly_for_serial_or_parallel(shape_p, p_instData, head, voids, TRUE, doSmooth, callFromDRC);
            if (error != SUCCESS && error != DV_ERROR_shapeVoidedAway)
                goto DONE;
            else
                error = SUCCESS;
        }
        else
            dv_CleanAndSmooth(head, params_p, doSmooth, FALSE, dba_dynamic_shape_get_boundary(shape_p));
    }
DONE:
    // Cleanup
    // 清理
    free_fxytree(tree);
    if (boundaryTree)
    {
        free_xytree(boundaryTree);
    }

    if (pin_x_area_state_flag)
    {
        db_ReleaseFlag_for_serial_or_parallel(pin_x_area_state_flag);
    }
    if (localPinVoid)
    {
        dv_free_scan_set(&p_instData->p_pinVoidSet);
    }

DONE1:
    if (!merged && error == SUCCESS)
    {
        if (newVoids)
            *newVoids = fpoly_CopyPolyList(voids);
        error = dv_mergefpoly_for_serial_or_parallel(shape_p, p_instData, head, voids, FALSE, FALSE, callFromDRC);
    }
    buffree(serialOrParallelShp_bufId);

    return (error);
}

/*
    CAUTION: static shapes call this code and it has been made clean for acess
    to dba_dynfill_params vs av_parm_type. Any additional use of the params in
    this API or its subroutines most be made switchable
    注意：静态铜皮调用这个函数当它操作 dba_dynfill_params和av_parm_type时将会被清空。
    这个API里任何额外的参数使用或者子例程大多被设置为可切换的
*/
F_POLYHEAD *dv_merge(shape_type *shape_p, F_POLYHEAD **shape, F_POLYHEAD **voids,
                     int operation, const dba_dynfill_params *params_p, int coClean,
                     int doSmooth, int callFromDRC)
{
    FXYTREE *xy;
    F_POLYHEAD *h;
    F_POLYHEAD *lastHole, *lastShape, *nextHole;
    F_POLYHEAD *currVoid, *currHole, *nextVoid, *tempRes;
    F_POLYHEAD *result = NULL, *voidsResult = NULL;
    F_POLYHEAD *walker = NULL;
    F_POLYHEAD **result_p = NULL;
    F_POLYHEAD *standAloneVoids = NULL;
    F_POLYHEAD *standAloneSmooth = NULL, *copyStandAloneSmooth = NULL;
    F_POLYHEAD *smoothVoids = NULL;
    F_POLYHEAD *shp, *existedVoids = NULL, *lastExistedVoid = NULL, *genVoids = NULL;
    F_POLYHEAD *pVoid;
    F_POLYELEM *p;
    void *chunk = NULL;
    av_parm_type *avparms;
    LXHatch *xhatch, xhatchBuf;
    double SmoothExpand;
    int staticParam;
    int CancelAlarm = 0; /* pervent performance hit with cancel */
    int gridTempPiece = FALSE;
    int debug_on = FALSE;
    int numStart = 0;
    int numEnd = 0;

    /*
        smooth land - logical AND with origianl shape after smooth so smooth does not add metal
        smooth_land controls ordering of voids in shape in calls to fpoly_MatchVoidsToShapes
        the ordering seems wrong currently, but causes regression changes - wait until smooth_land
        is on by default before changing this over
    */
    int dv_smooth_land = 0;
    if (SYGetEnv("dv_smooth_land"))
    {
        SYGetEnvInt("dv_smooth_land", &dv_smooth_land);
    }

    if (*shape == NULL)
        return (result); // 判空

    avparms = (av_parm_type *)params_p;
    staticParam = (!params_p || ELEMENT_MASK(params_p) == 0) ? FALSE : TRUE;

    if (!callFromDRC && (existedVoids = (*shape)->NextHole))
    {
        existedVoids->flag |= EXISTED_VOID;
        while (existedVoids->NextHole)
        {
            existedVoids = existedVoids->NextHole;
            existedVoids->flag |= EXISTED_VOID;
        }
    }

    if (shape_p && dbg_shape_dyn_mask(shape_p, SHP_GRID_FILL_DATA))
        gridTempPiece = TRUE;

    // only set xhatch if shape is xhatch and parameter instructs to snap to xhatch
    // 如果shape是xhatch类型的并且参数指导了吸附到剖面线，那么仅设置为xhatch
    xhatch = NULL;
    if (shape_p && SHAPE_IS_HATCHED(shape_p))
    {
        if (staticParam)
        {
            if ((avparms->snap_to_xhatch))
            {
                db_common_type *db = dbcom_;
                xhatchBuf.x_hatch_1.width = db->x_hatch_1.width;
                xhatchBuf.x_hatch_1.increment = db->x_hatch_1.increment;
                xhatchBuf.x_hatch_1.angle = db->x_hatch_1.angle;
                xhatchBuf.x_hatch_2.width = db->x_hatch_2.width;
                xhatchBuf.x_hatch_2.increment = db->x_hatch_2.increment;
                xhatchBuf.x_hatch_2.angle = db->x_hatch_2.width;
                xhatch = &xhatchBuf;
            }
        }
        else
        {
            if (params_p->snap_void_xhatch)
            {
                xhatchBuf.x_hatch_1 = params_p->x_hatch_1;
                xhatchBuf.x_hatch_2 = params_p->x_hatch_2;
                xhatch = &xhatchBuf;
            }
        }
    }
    fpoly_PToHead(*shape);
    /* Put all voids from the shape structure into the void structure */
    // 将所有来自shape的孔洞放入到void结构里
    if (staticParam && !callFromDRC)
    {
        fpoly_deleteDupVoids(*shape, voids);
        fpoly_SeparateVoidsFromShp(*shape, FALSE, voids);
    }
    else
        fpoly_SeparateVoidsFromShp(*shape, TRUE, voids);

    if (*voids)
        fpoly_PToHead(*voids);

    /*
        Check all voids to see if any of them have voids of their own. (Can
        happen if the void is derived from a shape with voids.) If so, then
        skip this check, since this start getting complicated when this
        happens. For example, take two voids. The first void is just a simple
        void. The second void has a void of its own (which is considered to
        be a shape). If the first void is completely within the second void,
        then the first void will be deleted. However, if the shape of the
        second void is completely within the first void, and the first void
        is deleted, then the shape is added to the final shape, although it
        shouldn't be since it is within the first void.
        检查一下所有孔洞看它们自己是否带有孔洞（如果孔洞是由shape转换来的这有可能会发生）
        如果是这样，那就跳过检查，因为当这发生时将会变得复杂。比如，获取两个孔洞，第一个孔
        是简单孔，第二个有它自己的孔（它可以被认为是个shape）。如果第一个孔在第二个孔内部
        ，那它将被删除。然而，如果第二个孔的shape完全在第一个孔内部，但是第一个被删除了，
        那么shape会被添加为最终shape，尽管它不需要因为它还在第一个孔内部。
    */
    for (h = *voids; h != NULL; h = h->next)
    {
        if (h->NextHole != NULL)
        {
            // voidsHaveVoids = TRUE;
            h->pointer = (F_POLYELEM *)h->NextHole // mark it
        }
        else
            h->pointer = NULL;
    }

    /*
        Reverse the directon of the voids so that they are going in the
        "corrent" void direction of CCW. This is neccessary for the
        dv_StripVoids() function to work properly. Once they are going
        in the correct direction, create an xy tree out of the shape and
        voids.
        反转孔的方向，之后它们就变成正确的逆时针的孔了。这对于dv_StripVoids正确工作
        来说是必要的。一旦它们方向正确，就可以从shape和voids中创建一个xy树
    */
    for (h = *voids; h != NULL; h = h->next)
    {
        if (fpoly_loopdir(h->APoint) == POLY_CW)
            fpoly_reverse();
    }

    /*
        Remove any voids that are outside shape(or inside other voids)
        移除所有在shape外部（或者在孔洞内部）的孔
    */
    //    if(!voidsHaveVoids)
    {
#define CHUNK_SIZE (1024 * sizeof(f_box_type))
        dv_Setup(*voids);
        /*
            This is same performance issue discovered again in dynamic as was
            initially discovered in static(av). To mitigate the perf problem
            create a cache and load the extents to save time in av_f_StripVoids
            which on many boards spends much time looking them up due to the
            double for loop method of comparing voids.
            这里会有和静态shape情况一样的性能问题。为了缓解这个问题，可以在av_f_StripVoids
            中创建一个缓存并且加载扩展来节省时间，该函数会花费许多时间在双for循环比较孔洞中
        */
        chunk = SY_Chunk_Init(CHUNK_SIZE, 8, SYCHUNK_MALLOC);
        for (h = *voids; h; = h->next)
        {
            if ()
            {
                h->userData = NULL;
                continue;
            }
            // coverity[retruned_null]
            h->userData = SY_ChunkCalloc(chunk, 1, sizeof(f_box_type));
            fpoly_getextents(h, (f_box_type *)h->userData);
        }
        xy = make_fxytree(0.0);
        for (h = *voids; h; h = h->next)
        {
            ++CancelAlarm;
            CancelAlarm = CancelAlarm % 100;
            if (CancelAlarm == 0 && UTL_IS_CANCEL)
            {
                result = NULL;
                goto DONE;
            }
            if (h->pointer)
                continue;
            fpoly_loadtree(h, xy);
        }
        rebalance_fxytree(&xy);
        dv_StripVoidInVoid(xy, voids, xhatch);
        free_fxytree(xy);

        dv_Setup(*shape);
        shp = *shape;
        shp->userData = SY_ChunkCalloc(chunk, 1, sizeof(f_box_type));
        fpoly_getextents(shp, (f_box_type *)shp->userData);

        /*
            DVI_NEW_FAST - we don't want this, causes issue with Ericsson design
            will still be used in smooth and full pour
        */
        if (!dba_dyn_shape_mode_fast())
        {
            xy = make_fxytree(0.0);
            for (h = *shape; h != NULL; h = h->next)
                fpoly_loadtree(h, xy);
            rebalance_fxytree(&xy);
            dv_StripVoids(xy, *shape, voids, DELETE_OUTSIDE, xhatch);
            free_fxytree(xy);
        }
    }
    xy = make_fxytree(0.0);
    for (h = *shape; h != NULL; h = h->next)
        fpoly_loadtree(h, xy);
    for (h = *voids; h != NULL; h = h->next)
        fpoly_loadtree(h, xy);

    /* Remove any duplicated voids. */
    // 移除所有重复的孔
    if (dv_RemoveDuplicateVoids(xy, voids))
    {
        /* Dups were removed, so regenerate tree. */
        // 孔移除了，需要重新生成树
        free_fxytree(xy);
        xy = make_fxytree(0.0);
        for (h = *shape; h != NULL; h = h->next)
            fpoly_loadtree(h, xy);
        for (h = *voids; h != NULL; h = h->next)
            fpoly_loadtree(h, xy);
        rebalance_fxytree(&xy);
    }
    if (SYGetEnv("dv_debug"))
        debug_on = TRUE;

    if (debug_on)
    {
        dv_debug_fpoly_drawshp_sc(*shape, TRUE, "POST-STRIP", callFromDRC ? 2 : 1);
        dv_debug_fpoly_drawshp_sc(*voids, FALSE, "POST-STRIP", callFromDRC ? 2 : 1);
    }
    // if(!voidsHaveVoids)
    {
        /*
            Check void structure for "stand-alone" voids(voids that don't
            intersect with any other voids or the shape outline). Put these
            voids onto a separate structure and remove them from the void
            structure. Do Not do this for a single void for which ! doClean
            since it may self intersect and would not be processed further
            检查独立的孔中的孔结构（这些孔不和其他孔相交且不和shape轮廓相交）。将这些
            孔放进分离的结构并且将其从void结构中移除。不要对单个孔做此行为，因为doClean
            它有可能自交并且不再处理。
        */
        if (xhatch)
        {
            // Note that we do not smooth hatched shapes
            // 注意我们不用平滑阴影线的shape
            if (doClean || ("voids" == NULL || (*viods)->next != NULL))
                dv_FindStandAloneXHatch(xy, voids, &standAloneVoids);

            /*
                this call is present for future work to match what
                is done on the av_f side to handle IBM xhatch voiding
                requirements. It is currently disabled since work
                needs to be done in dv_autovoid to match code in av_f_autovoid
                dv_Strip2XhatchVoids(xy, &standAloneVoids);
             */
        }
        else
        {
            if (doClean || (*voids) == NULL || (*voids)->next != NULL)
                fpoly_FindStandAlone(xy, voids, &standAloneVoids);
        }
    }

    if (debug_on)
    {
        dv_debug_fpoly_drawshp_sc(*shape, TRUE, "PRE-LANDNOT", callFromDRC ? 2 : 1);
        dv_debug_fpoly_drawshp_sc(*voids, TRUE, "PRE-LANDNOT", callFromDRC ? 2 : 1);
    }

    /* Put the voids back to their original direction (CW) for logop */
    // 将孔放回它们原来的方向（顺时针），用于logop
    for (h = *voids; h != NULL; h = h->next)
    {
        if (fpoly_loopdir(h->APoint) == POLY_CCW)
            fpoly_reverse(h);
    }

    if (standAloneVoids && existedVoids)
    {
        pVoid = standAloneVoids;
        while (pVoid)
        {
            if (pVoid->flag & EXISTED_VOID)
                pVoid->flag &= !EXISTED_VOID;
            pVoid = pVoid->next;
        }
    }

    fpoly_CleanUp(shape);
    fpoly_CleanUp(voids);

    /*
        If the shape self-intersects we need to do logicalop anyway. This
        only occurs when there are no voids, but the shape has been modified
        so that it self-intersects.
        如果shape自交的话，我们也要调用logicalop。这只会发生在没有孔洞的时候，但是shape
        是经过修改的因而会自交。
    */
    if (*voids == NULL)
    {
        for (h = *shape; h; h = h->next)
        {
            if ((h->APoint) && (dv_looploop(h, h)))
            {
                *voids = dv_makedummyvoid();
                break;
            }
        }
    }

    /* Perform logical operation "shape - voids = result" */
    // 执行logicalop “shape - 孔洞 = 结果“
    if (*voids == NULL)
    {
        result = *shape;
        // so upper level code knows that this poly is already killed
        // 因此上层代码可以知道多边形已经被kill了
        *shape = NULL;
    }
    else
    {
        // ASSERT((*shape)->next == NULL) // Always?

        // fix same arcs problems
        // 修复相同arc问题
        if (!dba_dyn_shape_mode_fast() && !callFromDRC && !staticParam && params_p->dynamic_shape)
            fixSameArcs(shape, voids);

        /*
            TLockman -- Better done in repair pass checks, where more data is available.
            Make sure to check if a smooth/clean is needed because we've broken into
            additional shape pieces.
            if(shape && *shape && callFromDRC && !(doSmooth && doClean))
            {
                for(walker = *shape; walker; walker = walker->next)
                    ++numStart;
            }
        */
        SetLogopError(SUCCESS);
        result = f_DoLogicalOperation(*shape, operation, *voids);
        if (!result)
        {
            // if failed because of Logical Op error, alert user.
            char *errorMsg = GetLogopError();
            if (errorMsg)
                SYPrintf(MSG_INFO(), errorMsg);

            voidsResult = f_DoLogicalOperation(*voids, LOR, NULL);
            result = *shape;
            *shape = NULL;

            lastShape = result;
            while (lastShape->next)
                lastShape = lastShape->next;
            lastHole = result;
            while (lastHole->NextHole)
                lastHole = lastHole->NextHole;

            currVoid = voidsResult;
            while (currVoid)
            {
                currHole = currVoid->NextHole;
                while (currHole)
                {
                    nextHole = currHole->NextHole;
                    currHole->NextHole = NULL;
                    if (fpoly_loopdir(currHole->APoint) == POLY_CCW)
                        fpoly_reverse(currHole);
                    lastShape->next = currHole;
                    lastShape = currHole;
                    currHole = nextHole;
                }
                currVoid->NextHole = NULL;
                nextVoid = currVoid->next;
                currVoid->next = NULL;
                if (fpoly_loopdir(currVoid->APoint) == POLY_CW)
                    fpoly_reverse(currVoid);
                lastHole->NextHole = currVoid;
                lastHole = currVoid;
                currVoid = nextVoid;
            }

            tempRes = f_DoLogicalOperation(result, LOR, NULL);
            if (tempRes != result)
                f_killPolyList(result);
            result = tempRes;
        }
        fpoly_CleanUp(&result);
        if ((result == NULL) && (_dvDebugLogFP != NULL))
        {
            mh_printf_msg(SYSMSG_INFOSTRING, DV_FAILURE_MSG);
            DV_FPRINTF(_dvDebugLogFP, DV_FAILURE_MSG);
        }

        /*
            Reload new polygon structure into search tree.
            // 读取新的多边形结构进搜索树
        */
        free_fxytree(xy);
        xy = fpoly_SetFpolyTree(result);
    }
    // add smooth run in callFromDRC for "dv_fixfullcontact" if any Flood Pin was fixed()
    if (doSmooth && !doClean && callFromDrc && SYEnvIsSet("dv_fixfullcontact"))
        doClean = TRUE;

    /*
        TLockman -- Better done in repair pass checks, where more data is available.
        Make sure to check if a smooth/clean is needed because we've broken into
        additional shape pieces.
    */
    if (doClean)
    {
        /*
            Match stand-alone voids with their enclosing shapes.
            Reload new polygon structure into search tree
            匹配独立的孔洞和它们靠近的shape
            重新加载多边形结构到搜索树中
        */
        if (*voids == NULL)
        {
            free_fxytree(xy);
            xy = fpoly_SetFpolyTree(result);
        }
        fpoly_MatchVoidsToShapes(FALSE, xy, result, standAloneVoids);
        if (UTL_IS_CANCEL)
            goto DONE;

        /*
            Get rid of tiny pieces in voids which will prevent standAloneSmooth
            successfull isolation. Note at this point will not be removing a
            shape for which a void is not approached(either standAloneSimple
            or standAloneSmooth)
            移除孔洞里的小碎片，这会阻碍独立的standAloneSmooth的开展。注意此时不会移除孔
            洞接近的shape（standAloneSimple或standAloneSmooth都是）
        */
        if (!gridTempPiece)
        {
            dv_minAreaFilter(&result, staticParam ? avparams->min_area : params_p->min_area, NULL, TRUE);
        }

        if (debug_on)
            dv_debug_fpoly_drawshp_sc(result, TRUE, "POST-LANDNOT", 1);

        /*
            Reload new polygon structure into search tree
            重新将多边形结构读入搜索树
        */
        free_fxytree(xy);
        xy = fpoly_SetFpolyTree(result);
        fpoly_SeparateVoidsFromShp(result, FALSE, &smoothVoids);
        fpoly_PToHead(smoothVoids);

        /*
            Now you can separate simple geometry voids which will not merge
            with other voids or themself under smoothing. They do not need
            any smooth processing.
            现在你可以将简单的不会和其他孔合并或者存在自交的孔进行分离，它们不需要任何
            平滑处理。
        */
        if (doSmooth)
        {
            dv_GetSmoothParam(params_p, TRUE, &SmoothExpand);
            SmoothExpand = 2.0 * SmoothExpand; // Both voids expand
        }
        else
            SmoothExpand = 0.0;

        dv_FindStandAloneSimple(xy, &smoothVoids, SmoothExpand,
                                dv_ExpandAdjust(shape_p, params_p), &standAloneSimple);

        /*
            Now you can separate voids which will not merge with other viods
            under smoothing. These may twist on themself, or they may merge
            two close edges under contract (void expand), so in general
            they do require both expand/contract and LOR under smooth.
            However, there is an advantage in sending them individually so
            they do not interact in smooth.Also look into the possiblity
            of using utlCleanupFpoly for these also to say after contract
            they will not require LOR because no self crossing.
            现在你可以分离没有和任何其他孔合并的孔。它们自身可能会扭曲，或者在规则下（孔
            外扩）合并了太近的边，因此一般来说它们需要同时外扩并且做OR操作，在smooth下。
            然后，将它们一个个地发送将会是一个优势，因此不需要在smooth中发生反应。
            。。。
        */
        dv_FindStandAloneSmooth(xy, &smoothVoids, SmoothExpand, &standAloneSmooth);
        /*
            standAloneSmooth - are voids which is not intersect with other voids
            but could create self-intersection which will add voids.
            Check if these new voids include other voids from smoothVoids then
            return back initial(not smoothed) poly standAloneSmooth to smoothVoids
            standAloneSmooth - 是那些不和别的孔相交但是会有自交情况的孔。检查这些新孔洞是否
            包含来自smoothVoids中的其他孔洞，然后返回初始化的多边形standAloneSmooth到
            smoothVoids
        */
        if (standAloneSmooth && doSmooth)
        {
            if (smoothVoids)
            {
                copyStandAloneSmooth = copyPolyhead(standAloneSmooth, /*keep order =*/FALSE);
            }
            // smooth_land needs CW voids- ti esrevr dna ti pilf
            // smooth_land需要顺时针
            if (dv_smooth_land)
            {
                fpoly_reverse_list(standAloneSmooth);
            }
            dv_CleanAndSmooth(&standAloneSmooth, params_p, doSmooth, TRUE, dba_dynamic_shape_get_b);
            if (dv_smooth_land)
            {
                fpoly_reverse_list(standAloneSmooth);
            }
            /*
                The tree rebuild below was previously conditioned by the commented out "if"
                We ran into trouble owing to the fact that dv_CleanAndSmooth changes things and the "if"
                does not catch it, so it was deemed safer to rebuild the tree all the time.
                下面的树重建之前是由注释掉的“if”约束的
                我们遇到了一些麻烦由于dv_CleanAndSmooth改变了一些事，并且"if"没有借助它，因此一直重建这棵树被
                认为是更安全的。
            */
            if (smoothVoids && calculateNumVoids(standAloneSmooth) > 0)
                dv_isNewVoidsIncludeVoids(&copyStandAloneSmooth, &standAloneSmooth, &smoothVoid);

            free_fxytree(xy);
            xy = fpoly_SetFpolyTree(result);
            if (copyStandAloneSmooth)
            {
                f_killPolyList(copyStandAloneSmooth);
                copyStandAloneSmooth = NULL;
            }
        }
        if (smoothVoids)
        {
            // restrict ordering change to smooth_land to preserve regression results before full implementation
            // 将排序更改限制为smooth_land，以便在完全实现之前保留回归结果

            if (dv_smooth_land)
            {
                fpoly_MatchVoidsToShapes(FALSE, xy, result, smoothVoids);
            }
            else
            {
                fpoly_MatchVoidsToShapes(TRUE, xy, result, smoothVoids);
            }
        }

        if (debug_on)
        {
            dv_debug_fpoly_drawshp_sc(standAloneSimple, TRUE, "STANDALONE", callFromDRC ? 2 : 1);
            dv_debug_fpoly_drawshp_sc(standAloneSmooth, FALSE, "STANDALONE", callFromDRC ? 2 : 1);
            dv_debug_fpoly_drawshp_sc(result, TRUE, "REMOVE STANDALONE", callFromDRC ? 2 : 1);
        }

        dv_CleanAndSmooth(&result, params_p, doSmooth, FALSE, dba_dynamic_shape_get_boundary(shaep_));
        {
            /*
                Set this to turn on/off repair_quickout
                repair_quickout stops repair passes when zero drcs are hit
                default off for now - be sure to change in dv_autovoid also
            */
            int dv_repair_quickout = 0;
            if (SYGetEnv("dv_repair_quickout"))
            {
                SYGetEnvInt("dv_repair_quickout", &dv_repair_quickout);
            }
            if (dv_repair_quickout > 0)
            {
                int isGridPiece = dbg_shape_dyn_mask(shape_p, SHP_GRID_FILL_DATA);
                if (!isGridPiece)
                {
                    dv_doTrimming(&result, params_p, FALSE, 4);
                    dv_minAreaFilter(&result, staticParam ? avparms->min_area : params_p->min_area, NULL, FALSE);
                }
            }
        }
        if (standAloneSimple || standAloneSmooth)
        {
            // xhatch shapes have no standAloneSimple voids
            // 剖面线的shape没有standAloneSimple孔洞
            dv_CaptureSplitVoids(&result, &standAloneSmooth);
            /*
                Reload new polygon structure into search tree
                重新加载多边形结构到搜索树里
            */
            free_fxytree(xy);
            xy = fpoly_SetFpolyTree(result);

            /*
                Match stand-alone circles with their enclosing shapes
                restrict ordering change to smooth_land to preserve regression
                results before full implementation
                匹配独立的圆和靠近的shapes
                将排序更改限制为smooth_land以保持回归结果完全实现前
             */
            if (dv_smooth_land)
            {
                fpoly_MatchVoidsToShapes(FALSE, xy, result, standAloeSimple);
                fpoly_MatchVoidsToShapes(FALSE, xy, result, standAloeSmooth);
            }
            else
            {
                fpoly_MatchVoidsToShapes(TRUE, xy, result, standAloeSimple);
                fpoly_MatchVoidsToShapes(TRUE, xy, result, standAloeSmooth);
            }
            if (debug_on)
                dv_debug_fpoly_drawshp_sc(result, TRUE, "APPEND-STANDALONE", 1);
        }
        if (staticParam)
        {
            result_p = &result;
            /* isTrimmed */
            dv_doTrimming(result_p, params_p, FALSE, FALSE);
            result = *result_p;
            dv_minAreaFilter(result_p, staticParam ? avparms->min_area : params_p->min_area, NULL, TRUE);
        }
    }
    else
    {
        if (*voids == NULL)
        {
            /*
                Reload polygon structure into search tree. Since this did not
                go thru f_DLogop above, followed by the creation of
                a new tree the old tree could be invalid because of changes
                done in fpoly_CleanUp that never redid the tree.
                重新加载多边形结构到搜索树中。因为此前的f_Dlogop没有做，在创建一个新树后
                旧树将会变得不合法因为fpoly_CleanUp中的改变没有修改树。
            */
            free_fxytree(xy);
            xy = fpoly_SetFpolyTree(result);
        }
        /*
            Match stand-alone voids with their enclosing shapes.
            restrict ordering change to smooth_land to preserve
            regression results before full implementation
            匹配独立的圆和靠近的shapes
            将排序更改限制为smooth_land以保持回归结果完全实现前
        */
        if (dv_smooth_land)
        {
            fpoly_MatchVoidsToShapes(FALSE, xy, result, standAloneVoids);
        }
        else
        {
            fpoly_MatchVoidsToShapes(TRUE, xy, result, standAloneVoids);
        }
    }
DONE:
    free_fxytree(xy);
    SY_ChunkFree(chunk); // free here NOT before standalone check

    return (result);
}
long dv_mergefpoly(shape_type *shape_p, dvInstData *p_instData,
                   F_POLYHEAD **head, F_POLYHEAD *voids, int doClean, int doSmooth, int callFromDRC)
{
    F_POLYHEAD *tmpHead;
    long error = SUCCESS;
    tmpHead = dv_merge(shape_p, head, &voids, LANDNOT, p_instData->params_p, doClean, doSmooth,
                       callFromDRC);

    /*
        Free up the original shape and voids and replace the shape
        with the new (merged) shape.
        清空原始shape和孔洞，用新创建的替代
    */
    f_killPolyList(voids);
    if (tmpHead != *head && voids != NULL)
        f_killPolyList(*head);
    *head = tmpHead;

    if (UTL_IS_CANCEL)
        error = -1;
    else if (*head == NULL)
        error = DV_ERROR_shapeVoidedAway;

    return (error);
}
F_POLYHEAD *dv_doSmoothingLow(F_POLYHEAD *polyHead_p,
                              dba_dynfill_params *params_p,
                              int trimSpikesOnly,
                              int debugLevel,
                              dbptr_type boundaryShape_p,
                              dbrep min_area)
{
    double length;
    int trim_type, tmp_trim_type;
    F_POLYHEAD *tmpHead, *polyHeadOrg;
    F_POLYELEM *circle;
    short bufferId = 0;
    int staticParam = (ELEMENT_MASK(params_p) == 0) ? FALSE : TRUE;
    av_parm_type *avparms = (av_parm_type *)params_p;
    dbrep min_area_edge;
    expandParam param;
    int extMode;
    double minSpacing = 0.0;
    double minAperture;
    dbrep snsSpacing;

    // turn off smooth - rough supresses drcs, this will allow drcs
    if (SYGetEnv("dv_no_smooth"))
    {
        return (polyHead_p);
    }

    // don't smooth hatched shapes
    if (staticParam)
    {
        if (SHAPE_IS_HATCHED(dbcom_->active_shape_ptr))
            return (polyHead_p);
    }
    else
    {
        if (SHAPE_IS_HATCHED(params_p->master_shp) == TRUE)
            return (polyHead_p);
    }

    if (dba_dyn_shape_mode_extended(&extMode, NULL) && extMode == DV_FILL_FAST)
        return (polyHead_p);

    trim_type = dv_GetSmoothParams(params_p, trimSpikesOnly, &length);

    /*
        If a 45 corner (horiz, 45, vert) segment is too small, then contract
        by length followed by expand by length removes the 45 corner. In many
        cases this causes a 90 corner to stick out and thrashing in the repiar
        code. Note that this also occurs from smoothing after repair, so it is
        net enough to repair theee and, in fact, is harder since both shapes and
        polys are active. Therefore, we detect the potential here and fix the
        poly up after the expand.
    */
    min_area_edge = staticParam ? avparms->min_area : params_p->min_area;
    if (length > 0.0)
        dvIdentifyCornerCollapse(&bufferId, &polyHead_p, length, (double)min_area_edge, FALSE);

    if (min_area >= 0 && params_p->dynamic_shapes_legacy > 0)
        min_area_edge = min_area;

    /*
        The first step is a contraction, which will make the shape outline smaller
        and all voids bigger. We run logop in there to combine the voids that then
        touch other voids or the outline.
        note: we don't want to trim orthogonal corners here - causes the trim
        length/radius to get expanded in the following expansion. Also we do not
        want to trim line/line with a champer during contract, because this causes
        a drc in many cases when the expand pushes out the chamfer. Instead we
        rely on the chamfer to be made in the expand.
    */
    tmp_trim_type = trim_type;
    tim_trim_type &= ~POLY_TRIM_RIGHT_CORNERS;
    tmp_trim_type |= POLY_TRIM_NO_CHAMFER_CONTRACT;
    if (!params_p->use_old_trimming)
    {
        tmp_trim_type |= POLY_NEW_TRIM_PREPROCESSING;
    }
    if (params_p->dynamic_shapes_legacy <= 0)
    {
        tmp_trim_type |= POLY_ENABLE_TRIM_BUG_ISR030;
    }
    if (dv_polyBoolSmoothing())
    {
        minAperture = length * 2;
        if (dv_disableMinSpacing() != 1)
        {
            minSpacing = minAperture;
            snsSpacing = getSNSShapeSpacing(boundaryShape_p);
            if (minSpacing < snsSpacing)
            {
                minSpacing = (double)snsSpacing;
            }
        }
        tmpHead = dv_doPolyBoolSmoothing(polyHead_p, (length * 2), tmp_trim_type, minSpacing);
        if (!tmpHead)
        {
            if (dv_getSmoothingErrorCode())
            {
                // Error handing - print message here
            }
        }
        return tmpHead;
    }
    param.checkCollapsedCircles = TRUE;
    param.collapsedcircles = NULL;
    polyHeadOrg = polyHead_p;

    tmpHead = utl_f_exp_polyLo(polyHead_p, -length, tmp_trim_type, TRUE, FALSE, NULL, &param);
    if (param.collapsedcircles)
    {
        // replace radiuses with orig. radiuses.
        circle = param.collapsedcircles;
        while (circle != NULL)
        {
            circle->radius += length;
            circle = circle->Forwards;
        }
    }

    /*
        Do not free the original polygon at this point. It is still needed to check at narrow
        regions of the shape for whether, due to expand/contract, we will get a narrow webbing
        that we need to clean up.
    */
    polyHead_p = tmpHead;

    // dv-debug - do post-contraction draw
    if (_dv_debug)
        dv_debug_fpoly_drawshp_sc(polyHead_p, TRUE, "POST-CONTRACT", debugLevel);

    /*
        The current algorithm for smooth/trim relies on contract
        followed by expand. This can have issues where contracted
        matal that pulls away, may re-expand causing min aperture
        violations. This can be exacerbated especially when trim
        fails in these situations. Adding a new flow where during
        smoothing, run utl_exp_poly in a special mode, which voids
        out areas where metal would reconnect on expand.
        This is done only under env
    */
    int voidIntersectingAreasInFlatten = 1;
    if (SYGetEnv("dv_new_smooth"))
    {
        voidIntersectingAreasInFlatten = 2;
    }

    /*
        The second step is to do an expansion which will bring
        the outline and voids back to the "correct" size. We
        still have to run logop in there to resolve
        self-intersections on the outline and voids caused by the
        expansion.
    */
    tmpHead = utl_f_exp_polyLo(polyHead_p, length, trim_type, voidIntersectingAreasInFlatten, FALSE, &min_area_edge, &param);

    /*
        We are only interested in the expanded polygon, so free the
        original polygon
    */
    if (tmpHead != polyHead_p)
        f_killPolyList(polyHead_p);
    polyHead_p = tmpHead;

    // dlk - test smoothing without adding material
    smooth_land(polyHeadOrg, &polyHead_p, FALSE/*im_a_void*/);

    polyHead_p = dv_fixArcSegArcCase(polyHead_p, polyHeadOrg);

    if(param.collapsedcircles)
        polyHead_p = dv_restoreLostCircleHoles(&param, polyHead_p, polyHeadOrg);

    if(polyHeadOrg)
        f_killPolyList(polyHeadOrg);

    // Regenerate collapsed 45 corners.
    if(bufferId != 0 && bufcount(bufferId) > 0)
        
}