#pragma once
#include <mutex>
#include <cstdio>
#include <cstdlib>

//sgi 一级配置器，_ints 非类型参数，不使用。
//template <int _ints>
class _malloc_alloc_template
{
public:
    //一级配置器直接调用malloc()
    static void* allocate(size_t __n)
    {
        void* __result = malloc(__n);
        return __result;
    }
    //一级适配器直接调用free
    static void deallocate(void* __p, size_t)
    {
        free(__p);
    }
    //一级配置器直接调用realloc()
    static void* reallocate(void* __p, size_t, size_t __new_sz)
    {
        void* __result = realloc(__p, __new_sz);
        return __result;
    }
    static size_t getTotalHeapSize(){return 0;}
    static size_t getCurrentMemPoolSize(){return 0;}
    static size_t getUsedMemSize(){return 0;}
    static bool   getFreeListMemInfo(size_t* meminfo, int len){return 0;}
};
class __default_alloc_template;
using malloc_alloc = _malloc_alloc_template;
using default_alloc = __default_alloc_template;

template<typename _Tp, typename _Alloc = default_alloc>
class simple_alloc
{
public:
    static _Tp* allocate(size_t __n)
    {
        return 0 == __n ? nullptr : (_Tp*) _Alloc::allocate(__n* sizeof(_Tp));
    }
    static _Tp* allocate(void)
    {
        return (_Tp*) _Alloc::allocate(sizeof(_Tp));
    }
    static void deallocate(_Tp* __p, size_t __n)
    {
        if(__n != 0)
        {
            _Alloc::deallocate(__p, __n * sizeof(_Tp));
        }
    }
    static void deallocate(_Tp* __p)
    {
        _Alloc::deallocate(__p, sizeof(_Tp));
    }
    static size_t getTotalHeapSize(){return _Alloc::getTotalHeapSize();}
    static size_t getCurrentMemPoolSize(){return _Alloc::getCurrentMemPoolSize();}
    static size_t getUsedMemSize(){return _Alloc::getUsedMemSize();}
    static bool   getFreeListMemInfo(size_t* meminfo, int len){return _Alloc::getFreeListMemInfo(meminfo, len);}
};

//SGI 二级配置器
//template <bool threads>

class __default_alloc_template
{
private:
    union _Obj
    {
        union _Obj* _M_free_list_link;  //链表指针
        char _M_client_data[1];         //_M_client_data指向实际内存首地址
    };
    
    enum {_ALIGN = 8};     //小型区块的上调边界
    enum {_MAX_BYTES = 128};  //小区区块的上限
    enum {_NFREELISTS = 16};  //_MAX_BYTES / _ALIGN = free-list的个数
    //[8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128]
    //8 ALIGN，向上8对齐
    static size_t _S_round_up(size_t __bytes)
    {
        //__bytes + 7 & 0xffffffff fffffff8
        return (((__bytes) + (size_t)_ALIGN - 1) & ~((size_t) _ALIGN - 1));
    }
    //根据申请数据块大小找到相应的空闲链表下标，n 从 0 起算
    static size_t _S_freelist_index(size_t __bytes)
    {
        return (((__bytes) + (size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
    }
    static void* _S_refill(size_t __n);
    static char* _S_chunk_alloc(size_t __size, int& __nobjs);
    static _Obj* _S_free_list[_NFREELISTS];
    static char* _S_start_free;   //内存池起始位置，只在_S_chunk_alloc()中变化
    static char* _S_end_free;     //内存池结束位置，只在_S_chunk_alloc()中变化
    static size_t _S_heap_size;   //申请内存的总数
    static size_t _S_used_size;
    static std::mutex malloc_mutex;
public:
    //申请大小为n的数据块，返回该数据块的起始地址
    static void* allocate(size_t __n)
    {
        void* __ret = 0;
        //如果需求区块大于128bytes ，调用第一级配置器
        if(__n > (size_t) _MAX_BYTES)
        {
            __ret = malloc_alloc::allocate(__n);
        }
        else
        {
           //根据申请空间大小寻找相应的空闲链表
           _Obj** __my_free_list = _S_free_list + _S_freelist_index(__n);
           std::lock_guard<std::mutex> free_list_lock(malloc_mutex); //上锁
           _Obj* __result = *__my_free_list;
           //空闲链表没有空闲数据块,先将区块调整至 8 倍数边界，然后调用_S_refill()重新填充
           if(__result == nullptr)
           {
               __ret = _S_refill(_S_round_up(__n));
           }
           else
           {
               //如果空闲链表有空闲数据块，则取出一个，并把空闲链表指向下一个数据块
               *__my_free_list = __result->_M_free_list_link;
               __ret = __result;
           }
           _S_used_size += __n;
        }
        return __ret;
    }
    static void deallocate(void* __p, size_t __n)
    {
        if(__n > (size_t) _MAX_BYTES)
        {
            malloc_alloc::deallocate(__p, __n);
        }
        else
        {
            _Obj** __my_free_list = _S_free_list + _S_freelist_index(__n);
            _Obj* __q = (_Obj*) __p;
            std::lock_guard<std::mutex> free_list_lock(malloc_mutex);
            __q->_M_free_list_link = *__my_free_list;
            *__my_free_list = __q;
            _S_used_size = __n;
        }
    }

    static void* reallocate(void* __p, size_t __old_sz, size_t __new_sz);
    /*static void show_memory_info()
    {
        std::cout <<"=====================MEM_INFO========================" << std::endl;
        std::cout <<"Total_memory: "<< _S_heap_size <<"("<<(double)_S_heap_size/1024<<"kb)" <<std::endl;
        std::cout <<"Mem_pool_size: "<< (size_t)(_S_end_free - _S_start_free) <<"("<<(double)(size_t)(_S_end_free - _S_start_free)/1024<<"kb)" <<std::endl;
        std::cout <<"Used_size: "<< _S_used_size <<"("<<(double)_S_used_size/1024<<"kb)" <<std::endl;
        for(int i = 0; i < 16; i++)
        {
            _Obj** __my_free_list = _S_free_list + i;
            _Obj* temp = *__my_free_list;
            uint32_t count = 0;
            if(temp != nullptr)
            {
                count = 1;
            }
            while(temp != nullptr && temp->_M_free_list_link != nullptr)
            {
                temp = temp->_M_free_list_link;
                count ++;
            }
            std::cout << "Free_list["<< i <<"] count = "<< count <<". total size = "<<(count * (i+1) *_ALIGN)<<"("<<(double)(count * (i+1) *_ALIGN)/1024<<"kb)" << std::endl;
        }
        std::cout <<"=====================================================" << std::endl;
    }*/
    static size_t getTotalHeapSize(){return _S_heap_size;}
    static size_t getCurrentMemPoolSize(){return (size_t)(_S_end_free - _S_start_free);}
    static size_t getUsedMemSize(){return _S_used_size;}
    static bool   getFreeListMemInfo(size_t* meminfo, int len)
    {
        if(meminfo == nullptr)
        {
            return false;
        }
        for(int i = 0; i < len; i++)
        {
            _Obj** __my_free_list = _S_free_list + i;
            _Obj* temp = *__my_free_list;
            uint32_t count = 0;
            if(temp != nullptr)
            {
                count = 1;
            }
            while(temp != nullptr && temp->_M_free_list_link != nullptr)
            {
                temp = temp->_M_free_list_link;
                count ++;
            }
            meminfo[i] = count * (i + 1) * _ALIGN;
            //std::cout << "Free_list["<< i <<"] count = "<< count <<". total size = "<<(count * (i+1) *_ALIGN)<<"("<<(double)(count * (i+1) *_ALIGN)/1024<<"kb)" << std::endl;
        }
        return true;
    }
};

//static member initlize
__default_alloc_template::_Obj* __default_alloc_template::_S_free_list[_NFREELISTS] = {nullptr};
char* __default_alloc_template::_S_start_free = nullptr;   //内存池起始位置，只在_S_chunk_alloc()中变化
char* __default_alloc_template::_S_end_free = nullptr;     //内存池结束位置，只在_S_chunk_alloc()中变化
size_t __default_alloc_template::_S_heap_size = 0;   //申请内存的总数
size_t __default_alloc_template::_S_used_size = 0;
std::mutex __default_alloc_template::malloc_mutex;
/*
_S_refill 一次取20个size为__n的内存出来，如果内存池足够，就将剩下的放进空闲链表里备用，内存池内存不够又分为两种情况，
一种是能满足一个size为__n，却小于20个__n的，这种情况不会申请内存，而是将内存池中的内存取出一个给调用者，另一种是一个都
不够，这时会申请内存，加入内存池中。然后递归调用_S_chunk_alloc获取内存。
*/
void* __default_alloc_template::_S_refill(size_t __n)
{
    int __nobjs = 20;
    //调用_S_chunk_alloc(),缺省取20个区块作为free list的新节点
    char* __chunk = _S_chunk_alloc(__n, __nobjs);
    _Obj** __my_free_list = nullptr;
    _Obj* __result = nullptr;
    _Obj* __current_obj = nullptr;
    _Obj* __next_obj = nullptr;
    int __i = 0;
    //如果只获得一个数据块，直接分给调用者，空闲链表中不会增加新节点
    if(__nobjs == 1)
    {
        return __chunk;
    }
    __my_free_list = _S_free_list + _S_freelist_index(__n);//否则根据申请数据块大小找到相应的空闲链表

    __result = (_Obj*) __chunk;
    *__my_free_list = __next_obj = (_Obj*)(__chunk + __n);//第0个数据块给调用者[__chunk, __chunk+__n - 1]
    for(__i = 1; ; __i++)
    {
        __current_obj = __next_obj;
        __next_obj = (_Obj*)((char*)__next_obj + __n);
        if(__nobjs - 1 == __i)
        {
            __current_obj->_M_free_list_link = nullptr;
            break;
        }
        else
        {
            __current_obj->_M_free_list_link = __next_obj;
        }
    }
    return(__result);
}
char* __default_alloc_template::_S_chunk_alloc(size_t __size, int& __nobjs)
{
    char* __result = nullptr;
    size_t __total_bytes = __size * __nobjs;     //需要申请空间的大小
    size_t __bytes_left = _S_end_free - _S_start_free; //计算内存池剩余空间

    if(__bytes_left >= __total_bytes) //case 1: 内存池剩余空间完全满足申请需求
    {
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return (__result);
    }
    else if(__bytes_left >= __size) //case 2: 内存池剩余内存不能满足申请，但能提供一个以上的区块 
    {
        __nobjs = (int)(__bytes_left / __size);
        __total_bytes = __size * __nobjs;
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return(__result);
    }
    else //case 3: 内存池剩余空间连一个区块大小都无法提供
    {
        size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);//为什么_S_heap_size >> 4
        //内存池的剩余空间分给合适的空闲链表
        if(__bytes_left > 0)
        {
            _Obj** __my_free_list = _S_free_list + _S_freelist_index(__bytes_left);
            ((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
            *__my_free_list = (_Obj*)_S_start_free;
        }
        _S_start_free = (char*) malloc(__bytes_to_get); //申请heap空间给内存池
        if(nullptr == _S_start_free) //内存不足
        {
            size_t __i;
            _Obj** __my_free_list;
            _Obj*  __p;
            for(__i = __size; __i <= (size_t)_MAX_BYTES; __i += (size_t)_ALIGN)
            {
                __my_free_list = _S_free_list + _S_freelist_index(__i); //找到存放__size大小的链表
                __p = *__my_free_list;
                if(__p != nullptr)//该链表下有空闲内存
                {
                    *__my_free_list = __p->_M_free_list_link;//取出第一个区块
                    _S_start_free = (char*)__p;//将取出的区块给到内存池
                    _S_end_free = _S_start_free + __i;
                    return(_S_chunk_alloc(__size, __nobjs));//递归，直到满足内存需求
                }
            }
            //空闲链表中也没有内存了
            //stl原来的做法是会抛出异常并做处理，这里我们假设它成功
            _S_end_free = nullptr;
            _S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get); //调用一级配置器
        }
        _S_heap_size += __bytes_to_get;
        _S_end_free = _S_start_free + __bytes_to_get;
        return(_S_chunk_alloc(__size, __nobjs));
    }  
}

void* __default_alloc_template::reallocate(void* __p, size_t __old_sz, size_t __new_sz)
{
    void* __result;
    size_t __copy_sz;
    if(__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t) _MAX_BYTES)
    {
        return (realloc(__p,__new_sz));
    }
    if(_S_round_up(__old_sz) == _S_round_up(__new_sz))
    {
        return(__p);
    }
    __result = allocate(__new_sz);
    __copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
    memcpy(__result, __p, __copy_sz);
    deallocate(__p, __old_sz);
    return(__result);
}