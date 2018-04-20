#pragma once

#include <vector>
#include <functional>

namespace vkw
{
    template <typename T>
    struct VkScopedObject
    {
    public:
        VkScopedObject()
        : object_(nullptr)
        , deleter_(nullptr)
        {
        }
        
        VkScopedObject(std::nullptr_t)
        : object_(nullptr)
        , deleter_(nullptr)
        {
        }
        
        template <typename F> VkScopedObject(T object, F deleter)
        : object_(object)
        , deleter_(deleter)
        {}
        
        VkScopedObject(VkScopedObject&& rhs)
        : object_(rhs.object_)
        , deleter_(rhs.deleter_)
        {
            rhs.object_ = nullptr;
            rhs.deleter_ = nullptr;
        }
        
        VkScopedObject(VkScopedObject const&) = delete;
        
        VkScopedObject& operator=(VkScopedObject&& rhs)
        {
            std::swap(object_, rhs.object_);
            std::swap(deleter_, rhs.deleter_);
            return *this;
        }
        
        VkScopedObject& operator=(VkScopedObject const&) = delete;
        
        ~VkScopedObject()
        {
            if (object_ && deleter_)
            {
                deleter_(object_);
            }
        }
        
        operator T() { return object_; }
        
        auto GetObjectPtr() { return &object_; }
        
    private:
        T object_;
        std::function<void(T)> deleter_;
    };
}

