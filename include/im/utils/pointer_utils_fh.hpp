#pragma once
// ============================================================
// PointerUtilsFH —— 智能指针空值检查辅助函数
// 提供统一的智能指针验证逻辑，减少重复代码
// ============================================================
#include <memory>
#include <string>
#include <stdexcept>

namespace Utils {

// 检查智能指针是否为空，为空时返回指定错误信息
template<typename T>
bool validatePointer(const std::shared_ptr<T>& ptr, const std::string& errorMessage = "") {
    if (!ptr) {
        if (!errorMessage.empty()) {
            // 可以选择记录日志或抛出异常
            // 这里简单返回false，调用者可根据需要处理
        }
        return false;
    }
    return true;
}

// 检查多个智能指针是否为空
template<typename T, typename... Args>
bool validatePointers(const std::shared_ptr<T>& first, const Args&... rest) {
    if (!first) return false;
    if constexpr (sizeof...(Args) > 0) {
        return validatePointers(rest...);
    }
    return true;
}

// 获取智能指针指向的对象，如果为空则抛出异常
template<typename T>
T& getPointerOrThrow(std::shared_ptr<T> ptr, const std::string& errorMessage = "Null pointer access") {
    if (!ptr) {
        throw std::runtime_error(errorMessage);
    }
    return *ptr;
}

// 获取智能指针指向的对象，如果为空则返回默认值
template<typename T>
T* getPointerOrDefault(std::shared_ptr<T> ptr, T* defaultValue = nullptr) {
    return ptr ? ptr.get() : defaultValue;
}

// 安全的智能指针解引用操作符包装
template<typename T>
class SafePointer {
public:
    explicit SafePointer(std::shared_ptr<T> ptr) : ptr_(std::move(ptr)) {}
    
    // 显式bool转换，用于if判断
    explicit operator bool() const {
        return ptr_ != nullptr;
    }
    
    // 安全解引用
    T& operator*() const {
        if (!ptr_) {
            throw std::runtime_error("Attempt to dereference null pointer");
        }
        return *ptr_;
    }
    
    // 安全箭头操作符
    T* operator->() const {
        if (!ptr_) {
            throw std::runtime_error("Attempt to access null pointer");
        }
        return ptr_.get();
    }
    
    // 获取原始指针
    T* get() const {
        return ptr_.get();
    }
    
private:
    std::shared_ptr<T> ptr_;
};

// 创建安全指针包装器的辅助函数
template<typename T>
SafePointer<T> makeSafePointer(std::shared_ptr<T> ptr) {
    return SafePointer<T>(std::move(ptr));
}

} // namespace Utils
