#ifndef KICH_R4_RESOURCE_RULES_H
#define KICH_R4_RESOURCE_RULES_H

/* Portable production validation and ownership store; no graphics API types. */
#include "../KichR4Resources.h"
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>

namespace kich_r4 {
inline uint32_t PixelBytes(uint32_t format)
{
    switch (format) {
    case KICH_R4_RGBA8: case KICH_R4_RGBA8_SRGB: case KICH_R4_R32F:
    case KICH_R4_DEPTH24_STENCIL8: case KICH_R4_DEPTH32F: return 4;
    case KICH_R4_RGBA16F: return 8;
    default: return 0;
    }
}
inline bool ValidTexture(const KichR4TextureDesc& d)
{
    if (d.struct_size != sizeof(d) || !d.width || !d.height ||
        d.width > 16384 || d.height > 16384 || !PixelBytes(d.format) ||
        !d.bindings || (d.bindings & ~7u)) return false;
    const bool depth = d.format == KICH_R4_DEPTH24_STENCIL8 || d.format == KICH_R4_DEPTH32F;
    return depth ? !(d.bindings & KICH_R4_COLOR_TARGET) : !(d.bindings & KICH_R4_DEPTH_TARGET);
}
inline bool ValidBuffer(const KichR4BufferDesc& d, const void* data, uint32_t bytes)
{
    if (d.struct_size != sizeof(d) || !d.byte_size || d.byte_size > 64u*1024u*1024u ||
        (data ? bytes != d.byte_size : bytes != 0)) return false;
    switch (d.kind) {
    case KICH_R4_CONSTANT_BUFFER: return !d.stride && d.byte_size <= 65536 && !(d.byte_size % 16);
    case KICH_R4_VERTEX_BUFFER: return d.stride && d.stride <= 2048 && !(d.byte_size % d.stride);
    case KICH_R4_INDEX_BUFFER: return (d.stride == 2 || d.stride == 4) && !(d.byte_size % d.stride);
    default: return false;
    }
}

class IdSource {
    KichR4Handle next_;
public:
    explicit IdSource(KichR4Handle first = 1) : next_(first) {}
    KichR4Handle Take() {
        if (!next_) throw std::overflow_error("R4 handle space exhausted");
        return next_++; /* wrap to 0 permanently retires this DLL's id source. */
    }
};

template<class T> class ObjectStore {
    IdSource& ids_;
    std::map<KichR4Handle, T*> objects_;
    ObjectStore(const ObjectStore&);
    ObjectStore& operator=(const ObjectStore&);
public:
    explicit ObjectStore(IdSource& ids) : ids_(ids) {}
    ~ObjectStore() { Clear(); }
    KichR4Handle Insert(std::unique_ptr<T> object) {
        if (!object) throw std::invalid_argument("null R4 resource");
        const KichR4Handle id = ids_.Take();
        objects_.insert(std::make_pair(id, object.get()));
        object.release();
        return id;
    }
    T* Find(KichR4Handle id) const {
        typename std::map<KichR4Handle,T*>::const_iterator i = objects_.find(id);
        return i == objects_.end() ? NULL : i->second;
    }
    bool Erase(KichR4Handle id) {
        typename std::map<KichR4Handle,T*>::iterator i = objects_.find(id);
        if (i == objects_.end()) return false;
        delete i->second;
        objects_.erase(i);
        return true;
    }
    void Clear() {
        for (typename std::map<KichR4Handle,T*>::iterator i = objects_.begin(); i != objects_.end(); ++i)
            delete i->second;
        objects_.clear();
    }
    uint32_t Size() const { return static_cast<uint32_t>(objects_.size()); }
};
}
#endif
