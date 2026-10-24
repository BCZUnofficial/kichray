
#include <algorithm>
#include <cassert>
#include <vector>
#include <iostream>
#define IC inline
struct CObject { unsigned id; unsigned ID() const { return id; } };
struct CDangerObject {
 const CObject* src; const CObject* dep;
 virtual ~CDangerObject() {}
 CDangerObject(const CObject* s,const CObject* d=nullptr):src(s),dep(d){}
 const CObject* object() const {return src;}
 const CObject* dependent_object() const {return dep;}
 void clear_dependent_object(){dep=nullptr;}
};
struct CDangerManager {
 using OBJECTS=std::vector<CDangerObject>; using IGNORED=std::vector<unsigned>;
 OBJECTS m_objects; IGNORED m_ignored; const CDangerObject* m_selected=nullptr;
 void remove_links(const CObject*);
};

#include "audit_danger.generated.h"
int main(){
 CObject a{1},b{2},c{3},g{4};
 for(bool third:{false,true}){
    CDangerManager m;m.m_objects.reserve(3);
    m.m_objects.emplace_back(&a);m.m_objects.emplace_back(&b,&g);if(third)m.m_objects.emplace_back(&c);
    m.m_selected=&m.m_objects[1];m.m_ignored.push_back(a.ID());m.remove_links(&a);
    assert(!m.m_selected&&m.m_ignored.empty());
    assert(m.m_objects[0].object()==&b&&m.m_objects[0].dependent_object()==&g);
    m.m_selected=&m.m_objects[0];m.remove_links(&g);
    assert(!m.m_selected&&!m.m_objects[0].dependent_object());
    m.remove_links(&b);assert(m.m_objects.size()==(third?1u:0u));
    m.remove_links(&c);assert(m.m_objects.empty()&&!m.m_selected);
 }
 std::cout<<"PASS: danger selection is invalidated across unrelated erase and dependent-object release\n";
}
