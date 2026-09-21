/* 文件：pagedir.h
   功能：定义页目录管理相关的函数声明
   描述：本文件实现了x86架构的页式虚拟内存管理
         包括页目录和页表的创建、映射、查询、权限控制等操作 */

#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

/* 函数：pagedir_create
   功能：创建一个新的页目录，初始化页目录表项
   参数：无
   返回：指向新创建的页目录的指针，失败返回NULL */
uint32_t *pagedir_create(void);

/* 函数：pagedir_destroy
   功能：销毁页目录及其所有相关的页表，释放所有相关内存
   参数：pd - 指向要销毁的页目录的指针
   返回：无 */
void pagedir_destroy(uint32_t *pd);

/* 函数：pagedir_set_page
   功能：在页目录中设置一个页映射，将用户虚拟页映射到物理页
   参数：pd - 页目录指针
         upage - 用户虚拟地址页
         kpage - 内核物理地址页
         writable - true表示可读写，false表示只读
   返回：成功返回true，失败返回false */
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool writable);

/* 函数：pagedir_get_page
   功能：查找用户虚拟页对应的物理页地址
   参数：pd - 页目录指针
         upage - 用户虚拟地址页
   返回：返回映射的物理页地址，未找到返回NULL */
void *pagedir_get_page(uint32_t *pd, const void *upage);

/* 函数：pagedir_clear_page
   功能：清除指定用户页的映射，使该页不再可访问
   参数：pd - 页目录指针
         upage - 要清除映射的用户虚拟地址页
   返回：无 */
void pagedir_clear_page(uint32_t *pd, void *upage);

/* 函数：pagedir_is_dirty
   功能：检查指定页是否被修改过（脏位）
   参数：pd - 页目录指针
         vpage - 要检查的用户虚拟地址页
   返回：true表示页被修改过，false表示未修改 */
bool pagedir_is_dirty(uint32_t *pd, const void *vpage);

/* 函数：pagedir_set_dirty
   功能：设置或清除指定页的脏位
   参数：pd - 页目录指针
         vpage - 要设置的用户虚拟地址页
         dirty - true表示设置为脏，false表示清除脏标记
   返回：无 */
void pagedir_set_dirty(uint32_t *pd, const void *vpage, bool dirty);

/* 函数：pagedir_is_accessed
   功能：检查指定页是否被访问过（访问位）
   参数：pd - 页目录指针
         vpage - 要检查的用户虚拟地址页
   返回：true表示页被访问过，false表示未访问 */
bool pagedir_is_accessed(uint32_t *pd, const void *vpage);

/* 函数：pagedir_set_accessed
   功能：设置或清除指定页的访问位
   参数：pd - 页目录指针
         vpage - 要设置的用户虚拟地址页
         accessed - true表示设置为已访问，false表示清除访问标记
   返回：无 */
void pagedir_set_accessed(uint32_t *pd, const void *vpage, bool accessed);

/* 函数：pagedir_activate
   功能：激活指定的页目录，使其成为当前CPU的页目录
   参数：pd - 要激活的页目录指针
   返回：无 */
void pagedir_activate(uint32_t *pd);

#endif /* userprog/pagedir.h */
