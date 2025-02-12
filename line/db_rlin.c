
#include "Telsys.h"
#include "chk_macros.h"
#include "db_macros.h"
#include "dbmsg.h"
#include "dbtemp.h"

// 1737283190175.jpg
/**
 * 替换数据库中的现有line记录.
 *
 * 该函数用于更新数据库中的现有line记录.它处理line所有权的更新,相关obstacles和DRC错误的管理.
 * 如果line的所有者或属性发生变化,它还会管理相应的更新.
 *
 * @param line_ptr 指向新line记录的指针.
 * @param database_id 现有元素的数据库ID.
 * @return 成功返回0,失败返回错误代码.
 */
long db_rlin(line_type *line_ptr, line_ptr *database_id) {
  dbptr_type old_owner_ptr, new_owner_ptr;
  int list_id, change_sc_flag = 0;
  line_segment_type *seg_ptr;
  long error;

  // 验证新line的数据
  error = lin_dat(line_ptr);
  if (error)
    goto DONE;

  // 获取现有line的所有者
  LINE_OWNER(database_id, old_owner_ptr);
  new_owner_ptr = line_ptr->link;

  if (old_owner_ptr == NULL)
    return (ILLEGAL_PARENT_ID);

  // 如果新所有者不是符号定义,则通知应用程序line将被修改
  if (ELEMENT_MASK(new_owner_ptr) != SYMBOL_DEFINITION) {
    dbfmsgSend3(DBFMSG_LINE, database_id, line_ptr,
                (void *)DBFMSG_OBJECT_MODIFY);
  }

  // 检查层叠类和子类是否发生变化
  if ((line_ptr->allegro_class != database_id->allegro_class) ||
      (line_ptr->subclass != database_id->subclass))
    change_sc_flag = TRUE;

  // 如果层叠类或子类发生变化,或者新所有者是符号定义且旧所有者不是符号定义
  if ((change_sc_flag ||
       (ELEMENT_MASK(new_owner_ptr) == SYMBOL_DEFINITION) &&
           (ELEMENT_MASK(old_owner_ptr) != SYMBOL_DEFINITION))) {
    // 删除所有线段的obstacles和关联的DRC错误
    for (seg_ptr = LINE_SEG_PTR(database_id->first_segment); seg_ptr != NULL;
         seg_ptr = NEXT_SEGMENT(seg_ptr)) {
      error = db_odel(seg_ptr);
      if (error)
        goto DONE;

      if (dbcom_->db_temp_mode) {
        db_atmp(seg_ptr, TDB_DEL_OBS, 0);
        if (error)
          goto DONE;
      }
      // 删除关联的DRC错误
      db_dadrc(seg_ptr, 1);
    }
  }

  // 如果链接字段已更改,则断开链接
  if (old_owner_ptr != new_owner_ptr) {
    switch (ELEMENT_MASK(old_owner_ptr)) {
    case ROOT:
      list_id = LINE_LIST;
      break;
    case SYMBOL_DEFINITION:
      list_id = SDEF_VLINE_LIST;
      break;
    case SYMBOL_INSTANCE:
      list_id = SINST_LINE_LIST;
      break;
    default:
      return (ILLEGAL_LINE_PARENT);
    }
    error = db_ulnk(list_id, old_owner_ptr, database_id);
    if (error)
      goto DONE;
  } else {
    // 如果旧所有者等于新所有者,则保留旧链接
    line_ptr->link = database_id->link;
    line_ptr->parent_ptr = database_id->parent_ptr;
  }
  /*必须保留不能被修改的链接*/
  line_ptr->first_relation = database_id->first_relation;

  // 在临时数据库中创建记录以备修改line记录
  if (dbcom->db_temp_mode) {
    error = db_ctmp_flags(database_id, sizeof(line_type), 0, TDB_FLG_MOD);
    if (error)
      goto DONE;
  }

  db_copy(line_ptr, database_id, sizeof(line_type));

  // 如果链接字段已更改,则重新链接
  if (new_owner_ptr != old_owner_ptr) {
    if (new_owner_ptr == NULL) {
      list_id = LINE_LIST;
    } else {
      switch (ELEMENT_MASK(new_owner_ptr)) {
      case ROOT:
        list_id = LINE_LIST;
        break;
      case SYMBOL_DEFINITION:
        list_id = SDEF_VLINE_LIST;
        break;
      case SYMBOL_INSTANCE:
        list_id = SINST_LINE_LIST;
        break;
      default:
        return (ILLEGAL_LINE_PARENT);
      }
    }
    database_id->parent_ptr = new_owner_ptr;
    error = db_link(list_id, database_id);
    if (error)
      goto DONE;

    // 继承新的所有者的高亮属性
    db_upd_ghlt(database_id, old_owner_ptr, new_owner_ptr);
  }

  // 如果层叠类或子类发生变化,则添加所有线段的obstacles
  if (change_sc_flag) {
    for (seg_ptr = LINE_SEG_PTR(database_id->first_segment); seg_ptr != NULL;
         seg_ptr = NEXT_SEGMENT(seg_ptr)) {
      if (ELEMENT_MASK(new_owner_ptr) != SYMBOL_DEFINITION) {
        error = db_oadd(seg_ptr);
        if (error)
          goto DONE;

        if (dbcom_->db_temp_mode) {
          db_atmp(seg_ptr, TDB_ADD_OBS, 0);
          if (error)
            goto DONE;
        }
      }
    }
  }

DONE:
  return (error);
}
