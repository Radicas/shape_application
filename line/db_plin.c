#include "chk macros.h"
#include "db macros.h"
#include "db uid.h"
#include "dbcom.h"
#include "dbmsg.h"
#include "errorcodes.h"
#include "osassert.h"
#include "telsys.h"
#include "telvar.h"

// 1737283190146.jpg
/**
 * 添加一条线记录到数据库.
 *
 * @param line_ptr 指向要添加的线记录的指针
 * @param return_ptr 返回数据库 ID 的指针
 * @return 返回操作结果,成功返回 SUCCESS,失败返回错误码
 */

long db_plin(line_type *line_ptr, line_type_ptr *return_ptr) {
  line_type *database_id;
  dbptr_type parent_id;
  int list_id;
  long error = SUCCESS;

  // 初始化线记录的一些字段
  line_ptr->state_flags = 0;
  line_ptr->color_id = 0;
  line_ptr->disp_mask = 0;

  // 检查父节点 ID
  parent_id = line_ptr->link;
  if (parent_id == NULL) {
    // 如果没有父节点,则设置默认父节点为 line_root,并将 list_id 设置为 LINE_LIST
    parent_id = line_ptr->link = (dbptr_type) & (dbcom_->line_root);
    list_id = LINE_LIST;
  } else {
    // 验证父节点 ID 是否在当前数据库范围内,支持多数据库(协同数据库?)
    ASSERT(valid_dbptr(parent_id));

    // 根据父节点类型确定 list_id
    switch (ELEMENT_MASK(parent_id)) {
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
      return ILLEGAL_LINE_PARENT;
    }
  }

  // 检查数据错误
  error = lin_dat(line_ptr);
  if (error)
    goto DONE;

  // 分配线记录的空间,成功后返回数据库 ID
  error = dballoc(sizeof(line_type), (dbptr_type *)&database_id);
  if (error)
    goto DONE;

  // 复制记录
  error = db_copy(line_ptr, database_id, sizeof(line_type));
  if (error)
    goto DONE;

  db_uid_plin(line_ptr, database_id);

  // 设置线记录的掩码值
  SET_ELEMENT_MAsK(database_id, LINE);

  // 如果临时模式激活,则为元素添加控制记录
  if (dbcom_->db_temp_mode == TRUE) {
    error = db_atmp(database_id, TDB_ADD, 0);
    if (error)
      goto DONE;
  }

  // 确保不应设置的指针字段不包含垃圾值,直接将其设为 NULL
  database_id->first_segment = NULL;
  database_id->first_text = NULL;
  database_id->first_relation = NULL;

  // 将当前记录链接到线记录列表中
  database_id->parent_ptr = parent_id;
  error = db_link(list_id, database_id);
  if (error)
    goto DONE;

  // 继承所有者的所有高亮属性
  error = db_upd_ghlt(database_id, NULL, parent_id);

  // 赋值返回指针
  *return_ptr = database_id;

  // 如果不是符号定义的子节点,则通知应用程序已添加线记录
  if (ELEMENT_MASK(parent_id) != SYMBOL_DEFINITION) {
    dbfmsgSend3(DBFMSG_LINE, NULL, database_id, (void *)DBFMSG_OBJECT_ADD);
  }

DONE:
  return error;
}
