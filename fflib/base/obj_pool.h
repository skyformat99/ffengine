#ifndef _OBJ_POOL_H_
#define _OBJ_POOL_H_

#include <malloc.h>
#include <string.h>
#include <list>
using namespace std;

namespace ff {

template <typename T>
class ff_obj_pool_t
{
public:
    ff_obj_pool_t(){
    }
    virtual ~ff_obj_pool_t() {
        for (typename list<T*>::iterator it = m_free.begin(); it != m_free.end(); ++it){
            ::free(*it);
        }
        m_free.clear();
    }
    T* alloc(){
        if (false == m_free.empty()){
            T* pret = m_free.front();
            m_free.pop_front();
            pret = new (pret) T;
            return pret;
        }
        else{
            void* p = ::malloc(sizeof(T));
            T* pret = new (p) T;
            return pret;
        }
        return NULL;
    }
    void release(T* p){
        p->~T();
        ::memset((void*)p, 0, sizeof(T));
        m_free.push_back(p);
    }

protected:
    list<T*>    m_free;
};


}

#endif

//!
