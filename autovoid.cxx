/**
 * @brief autovoid
 * @details
 *        high-level function for wholesale dynamic shape voiding. The input shape
 *        must be dynamic and have all voids removed. The target usage of this
 *        routine is when a dynamic shape requires complete revoiding and the input
 *        shape is a newly-created ETCH-subclass copy of the parent BOUNDARY-class
 *        shape.
 *        高级功能，用于批量处理动态铜皮避让。输入的shape必须是动态的，并且已经去除了所有的
 *        孔洞。此例程的目标用途是在动态shape需要完全重新避让并且输入形状是BOUNDARY-class
 *        shape的新创建的ETCH-subclass副本时。
 * @param shape_p
 * @return long
 */
long dv_autovoid(shape_type *shape_p);
long dv_autovoid_instance(shape_type *shape_p, dvInstData *p_instData);
long dv_autovoid_instance_head(shape_type *shape_p, dvInstData **pp_instaData,
                               int xtraDrcHit,
                               int *p_scanInitializedLocally,
                               int ignore_pad_suppress,
                               int *p_prev_pad_suppress,
                               int *p_localThermals,
                               dba_transaction_mark_type *p_start_mark,
                               short *p_newShapeBufferId,
                               dbptr_type *p_shapeNet_p,
                               int *p_finished,
                               F_POLYHEAD **p_fhead,
                               F_POLYHEAD **p_fkeepin,
                               int *p_shape_cnt,
                               int *p_localDRCHash,
                               unsigned long *p_genholes_timer);
long dv_autovoid_genholes(shape_type *shape_p, dvInstData *p_instData,
                          int buffer_id,
                          dbptr_type shapeNet_p,
                          int newShapeBufferId,
                          int xtraDrcHit,
                          int *shape_cnt,
                          int *p_localDRCHash,
                          F_POLYHEAD **p_fhead, F_POLYHEAD **p_fkeepin,
                          unsigned long *p_timer);
long dv_genholes(shape_type *shape_p, dvInstData *p_instData,
                 F_POLYHEAD **head, dbptr_type shp_net, int buffer_id, int callFromDRC, int doSmooth, F_POLYHEAD **newVoids, int callFromFast);
long dv_mergefpoly(shape_type *shape_p, dvInstData *p_instData,
                   F_POLYHEAD **head, F_POLYHEAD *voids, int doClean, int doSmooth, int callFromDRC);

long dv_autovoid(shape_type *shape_p) // 可能是指一类shape
{
    return dv_autovoid_instance(shape_p, NULL);
}
long dv_autovoid_instance(shape_type *shape_p, dvInstData *p_instData)
{
    dba_transaction_mark_type start_mark = 0; // 数据库事务标记
    dbptr_type shapeNet_p = NULL;             // shape网络
    int scanInitializedLocally = FALSE;       // 是否本地初始化扫描
    int localThermals = FALSE;                // 是否本地热封
    int xtraDrcHit = 0;                       // drc相关
    int prev_pad_suppress = FALSE;            // 是否抑制前pad
    long error = SUCCESS;                     // 错误码
    short buffer_id = 0;                      // 缓冲id
    short newShapeBufferId = 0;               // 新shape的缓冲id
    int finished = FALSE;                     // 是否结束
    int done_only_in_post = FALSE;            // 是否立刻提交
    int oldDisplay = TRUE;                    // 是否显示旧的
    unsigned long timer = 0;                  // 计时器

    F_POLYHEAD *fhead = NULL;
    F_POLYHEAD *fkeepin = NULL;
    int shape_cnt = 0;                                             // shape计数
    int localDRCHash = 0;                                          // 本地drc哈希
    dvInstData *p_locinstData = p_instData;                        // 本地数据实例指针
    unsigned long genholes_timer = 0;                              // 生成孔洞计时器
    int isAllegroX = dbg_design_flavor() == DESIGN_FLAVOR_ORCAD_X; // 设计风格

    int reset_mode; // 重置模式
    /*
        Explicitly set fill mode to WYSIWYG (smooth) when in AllegroX while performing full pour
        This will ensure shape is in artwork ready state
        在使用AllegroX软件进行全局填充（full pour）操作时，明确将填充模式设置为WYSIWYG（所见即所得，也称为平滑模式）。
        这样可以确保生成的形状处于可直接用于艺术作品的状态。
        设置WYSIWYG（平滑）模式的目的在于确保填充区域的形状准确无误，并且能够直接用于生产或输出，
        避免因为形状不规则或不准确而影响最终的设计质量。
    */

    if (isAllegroX)
    {
        reset_mode = dbg_global_dynamic_fill();
        dbs_global_dynamic_fill(DV_FILL_WYSIWYG);
    }

    /*
        DEBUG, for testing parallelism at genholes level as opposed to layer level; restore below
        Not needed once TBB works
        在调试过程中，为了测试在genholes级别而不是在layer级别的并行性；
        一旦TBB（Threading Building Blocks）正常工作，这段代码就可以删除。

    static int useParallel = FALSE;
    int oldParVal = FALSE;
    int useParallelDRC = FALSE;
    if(dv_get_use_parallel_voiding())
        useParallel = TRUE; // already set, cannot have it "disconnect" at a lower level

    oldParVal = dv_set_use_parallel(useParallel);
    InquireConstraintSetCustomMTState(useParallel && useParallelDRC);
    */

    xtraDrcHit = dv_SYEnvIsSet_for_serial_or_parallel("dv_SameNetDrc"); // 这个函数应该是查询变量

    av_init_for_serial_or_parallel(); // 初始化串行或并行

    oldDisplay = utl_dispena(FALSE);

    // enable debug logfile recording if requested
    // 如果请求的话，允许debug日志记录
    _dv_debug = dv_SYEnvIsSet_for_serial_or_parallel("dv_debug");
    if (_dv_debug)
        dv_openlog(); // 打开日志
    dv_performanceDebugUpdate(DV_START_TIMER | DV_PRINT_TIMER, "Performing complete autovoid", shape_p, &timer);

    /*
        Want to do this one time as it applies only to ICP
        at this point, where the acute angle cover tool exists.
        Do not want the performance penality of looking it up repeatedly.
        When this is embeded in the Inquire system, this will be removed.
        在目前的情况下，这个操作只适用于ICP，因为在那里存在一个急角覆盖工具。
        不希望因为重复查找而带来性能损失。当这个功能嵌入到查询系统中后，这段代码将被删除。
     */
    dv_setAcuteAngleCoverInUse();

    error = dv_autovoid_instance_head(shape_p, &p_locinstData, xtraDrcHit, &scanInitializedLocally,
                                      p_locinstData && p_locinstData->params_p ? p_locinstData->params_p->global_ignore_pad_suppress : FALSE,
                                      &prev_pad_suppress,
                                      &localThermals, &start_mark,
                                      &newShapeBufferId, &shapeNet_p, &finished,
                                      &fhead, &fkeepin, &shape_cnt, &localDRCHash, &genholes_timer);

    if (finished) // 如果完成了，跳转到DONE
        goto DONE;

    if (error == SUCCESS) // 没完成但是上面运行成功
        error = dv_autovoid_post_genholes(shape_p, p_locinstData,
                                          shapeNet_p,
                                          xtraDrcHit,
                                          newShapeBufferId,
                                          fkeepin,
                                          &shape_cnt, &fhead, &genholes_timer);
    // free the F_POLYHEAD structures and related memory
    // 释放F_POLYHEAD结构和相关内存
    if (dv_protoGetShapeMode() != protoNewShapes::protoCopy) // 原型拷贝
    {
        if (fhead != NULL)
            f_killpolyList(fhead);
        fhead = NULL; // i know, lost memory in protoCopy
    }

    if (fkeepin != NULL)
        f_killPolyList(fkeepin);

    if (localDRCHash)
        dv_free_scan_set(&p_locinstData->p_drcSet);

    if ((error != SUCCESS) || UTL_IS_CANCEL)
    {
        done_only_in_post = TRUE;
    }

    // return status of the below does not matter
    // 以下退出状态不重要
    (void)dv_autovoid_instance_post(shape_p, &p_locinstData,
                                    done_only_in_post, shapeNet_p, newShapeBufferId,
                                    scanInitializedLocally, localThermals, start_mark,
                                    p_locinstData && p_locinstData->params_p ? p_locinstData->params_p->global_ignore_pad_suppress : FALSE,
                                    prev_pad_suppress);
DONE:
    if (isAllegroX)
    {
        dbs_global_dynamic_fill(reset_mode);
    }
    av_free_for_serial_or_parallel();

    utl_dispena(oldDisplay);
    if (oldDisplay)
        utl_dispRedraw();

    // close the debug logfile if open
    if (_dv_debug)
        dv_closelog();
    dv_performanceDebugUpdate(DV_END_TIMER | DV_PRINT_TIMER, "Autovoid complete", shape_p, &tiemr);

    /*
        DEBUG, for testing parallelism at genholes level as opposed to layer level; see above
        Not needed once TBB works

        InquireConstraintSetCustomMTState(oldParVal);
        (void)dv_set_use_parallel(oldParVal);
    */
    return error;
}
long dv_autovoid_instance_head(shape_type *shape_p, dvInstData **pp_instaData,
                               int xtraDrcHit,
                               int *p_scanInitializedLocally,
                               int ignore_pad_suppress,
                               int *p_prev_pad_suppress,
                               int *p_localThermals,
                               dba_transaction_mark_type *p_start_mark,
                               short *p_newShapeBufferId,
                               dbptr_type *p_shapeNet_p,
                               int *p_finished,
                               F_POLYHEAD **p_fhead,
                               F_POLYHEAD **p_fkeepin,
                               int *p_shape_cnt,
                               int *p_localDRCHash,
                               unsigned long *p_genholes_timer)
{
    dvInstData *p_instData = NULL;
    dba_object_handle boundray_p = NULL;      // 对象处理器，边界
    dba_object_handle group_p = NULL;         // 对象处理器，组
    dba_transaction_mark_type start_mark = 0; // 事务标记
    dbptr_type br_ptr;                        // 元对象
    dbptr_type shapeNet_p = NULL;             // 网路
    int scanInitializedLocally = FALSE;
    int localThermals = FALSE;
    long error = SUCCESS;
    long loc_error = SUCCESS;
    short buffer_id = 0;
    short newShapeBufferId = 0;
    dbrep x_hatch_border_width = 0; // x盖板宽度
    int extMode;
    int baseMode;

    // autovoid_low_data
    // 自动挖孔低层数据
    F_POLYHEAD *fhead = NULL;
    F_POLYHEAD *fkeepin = NULL;
    int shape_cnt = 0;
    int localDRCHash = 0;

    // if we are not int WYSIWYG mode, set WYSIWYG out of date
    // 如果不在所见即所得模式，设置所见即所得为过时
    dba_dyn_shape_mode_extended_for_serial_or_parallel(&extMode, &baseMode);

    if (baseMode != DV_FILL_WYSIWYG)
        boundary_p = dba_dynamic_shape_get_boundary(shape_p); // 获取shape的边界

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

    // ensure the given shape is appropriate for voiding
    // 确保给定shape可以避让
    if ((shape_p->shape_fill == SHAPE_UNFILLED) ||
        (!utl_ok2void(ELEMENT_CLASS(shape_p),
                      ELEMENT_SUBCLASS(shape_p))))
    {
        if (p_finished)
            *p_finished = TRUE;

        return (-1);
    }

    if (DELETE_MASK(shape_p))
    {
        if (p_finished)
            *p_finished = TREU;
        return (SUCCESS);
    }

    if (pp_instData)
    {
        p_instData = *pp_instData;

        /*
            if(p_instData == NULL &&
            !(baseMode != DV_FILL_WYSIWYG && extMode == DV_FILL_FAST && dba_dyn_shape_init_fracture_for_serial_or_parallel(boundary_p))){
            // the above !(baseMode != DV_FILL_WYSIWYG && extMode == DV_FILL_FAST) will have to be reconsidered when dv_autovoid below gets moved out
            // 下面dv_autovoid出去时，!(baseMode != DV_FILL_WYSIWYG && extMode == DV_FILL_FAST) 不得不被重新考虑
        */

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

    /*
        fetch the parent dynamic shape group - needed to fetch the voiding
        parameters for the given shape and later to add the newly created
        etch shape(s) to the group after voiding.
        获取父动态shape组 - 需要获取给定shape的voiding参数，然后在voiding后将
        新创建的ETCH shape添加到组中。
    */
    loc_error = dba_object_group_owner_process(shape_p,
                                               (long int (*)())dv_find_dynamic_shape, (void *)&group_p);

    // The following block NEEDS to be removed when we multithread gridded
    // 当我们进行多线程网格化时，需要删除以下块

    if (baseMode != DV_FILL_WYSIWYG)
    {
        dbs_shape_dyn_mask_for_serial_or_parallel(boundary_p, DYNAMIC_FILL_OOD_WYSIWYG, TRUE);

        /*
            if in gridded rough mode, ensure that this is not the first time the shape is
            being updated form smooth to gridded, in which case we need to generate the
            grid pattern and regions.
            如果在网格粗糙模式下，确保这不是第一次从平滑到网格的形状更新，这种情况下我们需要生成网格图案和区域。
        */
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

    // fetch dynamic shape fill parameters for the given shape
    // 为给定shape获取填充参数
    p_instData->params_p = dba_dynamic_fill_params_get(group_p);

    if ((p_instData->params_p->global_ignore_pad_suppress || ignore_pad_suppress) && *p_prev_pad_suppress)
        *p_prev_pad_suppress = utl_suppress_pad_disable_prev_only_for_serial(TRUE);

    // Setup border width for poly_2_shape utilities
    // 为poly_2_shape工具设置边界宽度
    x_hatch_border_width = (SHAPE_IS_HATCHED(shape_p)) ? p_instData->params_p->x_hatch_border_width : 0;
    xhatch_val_set(shape_p, x_hatch_border_width);

    // get net of shape
    // cast below is weird, works, and should not be
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

    // init this static
    num_blurred_voids_map_set(shape_p, 0);

    if (p_instData->params->global_ignore_pad_suppress || ignore_pad_suppress)
        ignore_pad_suppress = utl_suppress_pad_disable_prev_only_for_serial(TRUE);

    // start a transaction for oopsing
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

        // init buffer to hold newly voided shapes
        // 初始化缓存存储避让后的shape
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
        // initialize buffer used to contain objects to be voided
        // 初始化缓存用于持有被挖空的对象
        bufinit(sizeof(dbptr_type), 100, 100, &buffer_id);

        // call find to gather all elements under shape
        dv_fillbuf(shape_p, p_instData, 0.0, buffer_id); // SERIAL_OR_PARALLEL: and the above. involves hash tabling the markers
        // see if we need to search outside for higher-priority boundary shapes
        if (dv_isSurroundingBoundaryCheck_for_serial_or_parallel() || extMode == DV_FILL_FAST || extMode == DV_FILL_PERFECT)
        {
            dv_findAdnAddNonBlurHigherPriorityShapes(shape_p, buffer_id);
        }

        // remove any dynamic shapes from the buffer that are lower priority
        // than the shape being voided.
        loc_error = dv_removeLowerPriorityShapes(shape_p, buffer_id); // SERIAL_OR_PARALLEL: conceivably OK
        /*
            Clear the state flag for pins. It will be set by av_pin and av_pindata
            and cleared by dv_connect_pin. If it is still set at the end
            (dv_FreePin) the pin was not connected and will be logged.
        */
        if (!p_instData->p_thermalsSet)
        {
            dv_init_scan_set(&p_instData->p_thermalsSet);
            localThermals = TRUE;
            if (p_localThermals)
                *p_localThermals = localThermals;
        }

        // init buffer to hold newly voided shapes
        bufinit(sizeof(dbptr_type), 50, 50, &newShapeBufferId);
        if (p_newShapeBufferId)
            *p_newShapeBufferId = newShapeBufferId;

        error = dv_autovoid_genholes(shape_p, p_instData, buffer_id,
                                     shapeNet_p, newShapeBufferId, xtraDrcHit,
                                     &shape_cnt, &localDRCHash, &fhead, &fkeepin, p_genholes_timer);
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

    // Init return values
    *shape_cnt = 0;

    if (!p_instData->p_drcSet)
    {
        dv_init_scan_set(&p_instData->p_drcSet);
        if (p_localDRCHash)
            *p_localDRCHash = TRUE;
    }

    // read the entire shape to be voided into polygon structure
    // 读取整个shape将其转换为polygon结构
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
        route keepin clipping, If grid regenerating is active, clipping already done.
        don't waste time doing it twice.
        route keepin修剪，如果网格再生处于活动状态，已经修剪过了，不要浪费时间重复做。
    */
    if (!dv_grid_regenerating(-1))
    {
        fhead = dv_orrki(shape_p, fhead, FALSE, &isRKIClipped);
        /*
            Need to keep an unmodified copy of the trimmed poly for later validation that
            trim/smooth doesn't go outside of it. But if we didn't clip to it, don't waste
            time and memory.
            不需要保留修剪后的多边形的副本，以便稍后验证修剪/平滑是否超出其范围。
            但如果我们没有剪裁到它，就不要浪费时间和内存。
         */
        if (fhead && isRKIClipped)
        {
            F_POLYHEAD *p_walker;
            fkeepin = fpoly_CopyPolyList(fhead);
            // Don't need the voids, though.
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

        // Fill all constraint region shapes in buffer
        dba_db_process(DBA_DBPROC_OBS, (APFL)qfindGetConsRegions, &buf_id);
        // walk all the items in the buffer
        buf_count = bufcount(buf_id);
        p_instData->params_p->single_cons_region = TRUE;
        for (int i = 1; i <= buf_count; i++)
        {
            bufget(buf_id, i, (dbptr_type)&shape_ptr);
            if (ELEMENT_SUBCLASS(shape_ptr) == ELEMENT_SUBCLASS(shape_p))
            {
                consRegHead = dv_readshp(SHAPE_PTR(shape_ptr), FALSE, NULL);
                // check if shape is fully inside constraint region
                tempHead = f_DoLogicalOperation(fhead, LANDNOT, consRegHead);
                if (!tempHead)
                    continue;
                else
                {
                    // check if shape intersects with constraint region
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
            // set first point of shape segment vertex to point on shape
            shapePt.db2x = 0;
            shapePt.db2y = 0;
            if (shape_p->first_outline_segment)
                shapePt = LINE_SEG_PTR(shape_p->first_outline_segment)->vertex1;
            p_instData->params_p->point_on_shape.db2x = (double)shapePt.db2x;
            p_instData->params_p->point_on_shape.db2y = (double)shapePt.db2y;
        }
        buffree(buf_id);
    }
    // now delete the outline segment -- need to keep for dv_orrki test
    error = dv_deleteShape_for_serial_or_parallel(shape_p);
    if (error)
        goto DONE;

    // set the smoothing flag from the shape's parameters
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
        // override if we are in new FAST mode
        if (dba_dyn_shape_mode_fast())
        {
            doClean = TRUE;
        }
        // create voids and merge them with shape structure
        error = dv_genholes(shape_p, p_instData, &fhead, shapeNet_p, buffer_id,
                            callFromDRC, doClean, NULL, FALSE);
        if ((error < 0) || UTL_IS_CANCEL)
            goto DONE;

        // filter small shapes
        if (extMode != DV_FILL_PERFECT)
        {
            *shape_cnt = dv_minAreaFilter(&fhead, p_instData->params_p->min_area, _dvDebugLogFP, FALSE)
            // If we still have to stitch things together, just tell us we've got one shape for now.
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
    cnt = bufcount(buffer_id);
    if (cnt < 1)
        got DONE1;
    cl = ELEMENT_CLASS(shape_p);

    if (!callFromDRC && cl == ETCH)
    {
        // Preprocess pins that cross constraint areas
        pin_x_area_state_flag = dv_checkout_x_pin_area_for_serial_or_parallel(shape_p, buffer_id);
    }

    tree = make_fxytree(0.0);

    expandAdjuctment = dv_ExpandAdjust(shape_p, params_p);

    // Set up for storing padstack problem error msgs
    if (cl == ETCH)
    {
        // will have to thread all pad report, for now leave as it is
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
    */
    fpoly_PToHead(*head);
    for (h = *head; h != NULL; h = h->next)
    {
        // Save the voids and remove them from the shape
        tmpHead = h->NextHole;
        h->NextHole = NULL;

        // Register the shape outline in the tree
        fpoly_loadtree(h, tree);

        // Put the voids back on the shape
        h->NextHole = tmpHead;
    }
    rebalance_fxytree(&tree);

    /*
        Make first pass to process other shapes. They will modify the
        polygon boundary, so we need to re-create the tree after processing
        all shapes, since every element is checked against the tree.
    */
    shp_changed = FALSE;

    // Get extents of shape being voided
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

    boundaryTree = dv_GetBoundaryTree(boundary_p);

    for (item = 1; item <= cnt; item++)
    {
        bufget(buffer_id, item, &elem_ptr);

        // screen out non-shapes
        if (ELEMENT_MASK(elem_ptr) != SHAPE)
            continue;

        // Don't process shape under edit or its parent boundary.
        if ((elem_ptr == shape_p) || (elem_ptr == boundaryPp))
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
            if (doSmooth)
                dv_GetSmoothParam(params_p, TRUE, &SmoothExpand);
            else
                SmoothExpand = 0.0;

            SmoothExpand += SmoothExpand; // both elements bump up by smooth
            error = dv_PinPattern_for_serial_or_parallel(tree, p_instData, shp_net, shape_p, *head,
                                                         &voids, buffer_id, cnt, expandAdjustment, SmoothExpand, callFromDRC);
        }
    }
    /*
        Create voids for every element in buffer (except shapes which were done above).
    */

    bufrset(serialOrParallelShp_bufId);
    for (item = 1; item <= cnt; item++)
    {
        // Get next item in the buffer
        bufget(buffer_id, item, &elem_ptr);

        // We've already processed shapes in loop above
        if (ELEMENT_MASK(elem_ptr) == SHAPE)
            continue;

        // We've voided pin patterns above
        if (ELEMENT_MASK(elem_ptr) == VAR_PIN &&
            dv_is_scan_set_item(elem_ptr, p_instData->p_pinVoidSet))
            continue;

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
        error = SUCCESS; // would have been masked in the loop above, so reset it
        shp_changed = TRUE;
    }

    num_edges = dv_num_edges(voids);

    // If no void edges, no need to go any further so drop out.
    if ((num_edges == 0) && (!shp_changed))
    {
        if (head != NULL && *head != NULL && doSmooth)
        {
            merged = TRUE;
            *head = dv_dosomething(*head, params_p, TRUE, 1, dba_dynamic_shape_get_boundary(shape_p));
        }
        goto DONE;
    }

    num_edges += dv_num_edges(*head);

    if (num_edges > 0)
    {
        // merge all voids and shape data with logical-or operation.
        if (voids != NULL)
        {
            fpoly_CleanUp(head);
            fpoly_CleanUp(&voids);

            if (params_p->smooth_min_gap)
                dv_rmv_s_arcs(*head, params_p->smooth_min_gap);

            // Do the merge
            merged = TRUE;

            /*
                For complex patches where two shape pieces will heal together in one area,
                but are still covered by a new void, need to keep these around for post-merge
                checks.
             */
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
    // Release all tree sturcture memory
    free_fxytree(tree);
    if (boundaryTree)
    {
        free_xytree(boundaryTree);
    }
    // Release state flag for pins xing cns areas.
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
        /*
            For complex patches where two shape pieces will heal together in one area,
            but are still covered by a new void, need to keep these around for post-merge
            checks.
        */
        if (newVoids)
            *newVoids = fpoly_CopyPolyList(voids);
        error = dv_mergefpoly_for_serial_or_parallel(shape_p, p_instData, head, voids, FALSE, FALSE, callFromDRC);
    }
    buffree(serialOrParallelShp_bufId);

    return (error);
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
