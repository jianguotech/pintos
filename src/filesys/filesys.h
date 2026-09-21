#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include "filesys/off_t.h"
#include <stdbool.h>

/* 系统文件inode所在的扇区 */
#define FREE_MAP_SECTOR 0 /* 空闲位图文件的inode扇区号 */
#define ROOT_DIR_SECTOR 1 /* 根目录文件的inode扇区号 */

/* 包含文件系统的块设备 */
extern struct block *fs_device;

/* 函数：filesys_init
   功能：初始化文件系统，包括挂载文件系统或创建新的文件系统
   参数：format - 是否格式化文件系统，true表示格式化，false表示使用现有文件系统
   返回：无
   注意：必须在使用任何其他文件系统函数之前调用；格式化会丢失磁盘上的所有数据 */
void filesys_init(bool format);

/* 函数：filesys_done
   功能：清理文件系统资源，关闭所有打开的文件，同步缓存
   参数：无
   返回：无
   注意：在系统关闭前应该调用此函数以确保数据完整性 */
void filesys_done(void);

/* 函数：filesys_create
   功能：在文件系统中创建一个新文件，分配inode和磁盘空间
   参数：name - 要创建的文件名（包含路径）；initial_size - 文件的初始大小（字节数）
   返回：成功时返回true，失败时返回false
   注意：如果文件已存在会返回失败；路径中的所有目录必须已经存在；initial_size为0时创建空文件 */
bool filesys_create(const char *name, off_t initial_size);

/* 函数：filesys_open
   功能：在文件系统中打开一个已存在的文件
   参数：name - 要打开的文件名（包含路径）
   返回：成功时返回指向文件结构的指针，失败时返回NULL
   注意：返回的文件指针需要使用file_close关闭；多次打开同一个文件会创建独立的文件描述符；如果文件不存在会返回失败 */
struct file *filesys_open(const char *name);

/* 函数：filesys_remove
   功能：从文件系统中删除一个文件，释放其占用的磁盘空间和inode
   参数：name - 要删除的文件名（包含路径）
   返回：成功时返回true，失败时返回false
   注意：只有当没有任何进程打开该文件时才能删除成功；如果文件正在被使用，删除会延迟到最后一个文件描述符关闭；删除操作是不可逆的 */
bool filesys_remove(const char *name);

#endif /* filesys/filesys.h */
