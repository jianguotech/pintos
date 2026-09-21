#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

/* 前向声明 */
struct inode;

/* 文件打开和关闭操作 */
/* 函数：file_open
   功能：通过inode打开一个文件，创建文件结构并初始化
   参数：inode - 要打开的文件的inode指针
   返回：成功时返回指向文件结构的指针，失败时返回NULL
   注意：调用者需要负责在使用后调用file_close关闭文件 */
struct file *file_open(struct inode *);

/* 函数：file_reopen
   功能：为已打开的文件创建一个新的文件描述符，共享同一个文件偏移量
   参数：file - 要重新打开的文件指针
   返回：成功时返回新的文件结构指针，失败时返回NULL
   注意：新的文件描述符独立于原始描述符，需要单独关闭 */
struct file *file_reopen(struct file *);

/* 函数：file_close
   功能：关闭文件并释放相关资源，减少inode引用计数
   参数：file - 要关闭的文件指针
   返回：无
   注意：如果是最后一个引用，inode也会被释放 */
void file_close(struct file *);

/* 函数：file_get_inode
   功能：获取文件对应的inode结构指针
   参数：file - 文件指针
   返回：返回文件的inode指针
   注意：返回的inode在使用后不应该被关闭，它的生命周期由文件管理 */
struct inode *file_get_inode(struct file *);

/* 文件读写操作 */
/* 函数：file_read
   功能：从文件的当前偏移位置读取指定字节数的数据
   参数：file - 要读取的文件指针；buffer - 存储读取数据的缓冲区；size - 要读取的字节数
   返回：实际读取的字节数，0表示到达文件末尾或出错
   注意：读取后会自动更新文件的当前位置 */
off_t file_read(struct file *, void *, off_t);

/* 函数：file_read_at
   功能：从文件的指定偏移位置读取指定字节数的数据
   参数：file - 要读取的文件指针；buffer - 存储读取数据的缓冲区；size - 要读取的字节数；start - 开始读取的字节偏移量
   返回：实际读取的字节数，0表示到达文件末尾或出错
   注意：不会改变文件的当前位置 */
off_t file_read_at(struct file *, void *, off_t size, off_t start);

/* 函数：file_write
   功能：从文件的当前偏移位置写入指定字节数的数据
   参数：file - 要写入的文件指针；buffer - 包含要写入数据的缓冲区；size - 要写入的字节数
   返回：实际写入的字节数，小于size表示写入失败或磁盘空间不足
   注意：写入后会自动更新文件的当前位置 */
off_t file_write(struct file *, const void *, off_t);

/* 函数：file_write_at
   功能：从文件的指定偏移位置写入指定字节数的数据
   参数：file - 要写入的文件指针；buffer - 包含要写入数据的缓冲区；size - 要写入的字节数；start - 开始写入的字节偏移量
   返回：实际写入的字节数，小于size表示写入失败或磁盘空间不足
   注意：不会改变文件的当前位置 */
off_t file_write_at(struct file *, const void *, off_t size, off_t start);

/* 写入权限控制 */
/* 函数：file_deny_write
   功能：禁止对文件进行写操作，通常用于执行文件
   参数：file - 要设置写保护的文件指针
   返回：无
   注意：可以为同一个文件多次调用，需要调用相应次数的file_allow_write才能解除 */
void file_deny_write(struct file *);

/* 函数：file_allow_write
   功能：允许对文件进行写操作，解除file_deny_write设置的保护
   参数：file - 要解除写保护的文件指针
   返回：无
   注意：调用次数必须与file_deny_write的调用次数匹配才能完全解除写保护 */
void file_allow_write(struct file *);

/* 文件位置操作 */
/* 函数：file_seek
   功能：设置文件的当前读写位置到指定的字节偏移量
   参数：file - 要设置位置的文件指针；new_pos - 新的文件位置偏移量
   返回：无
   注意：如果new_pos超过文件长度，文件会被扩展到指定大小 */
void file_seek(struct file *, off_t);

/* 函数：file_tell
   功能：获取文件的当前读写位置字节偏移量
   参数：file - 文件指针
   返回：当前文件位置的偏移量
   注意：返回值在0到文件长度之间 */
off_t file_tell(struct file *);

/* 函数：file_length
   功能：获取文件的总长度（字节数）
   参数：file - 文件指针
   返回：文件的总长度字节数
   注意：长度是根据inode中的文件大小信息得到的 */
off_t file_length(struct file *);

#endif /* filesys/file.h */
