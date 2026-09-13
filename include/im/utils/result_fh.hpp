#pragma once
// ============================================================
// Result<T> —— 操作结果封装类，替代简单的bool返回值
// 提供成功/失败状态和详细的错误信息
// ============================================================
#include <optional>
#include <string>
#include <variant>

namespace Utils {

// 通用错误类型
struct Error {
    std::string message;
    
    explicit Error(const std::string& msg) : message(msg) {}
    Error() = default;
};

// 结果模板类：成功时包含值，失败时包含错误信息
template<typename T>
class Result {
public:
    // 构造成功结果
    static Result success(T value) {
        return Result(std::move(value));
    }
    
    // 构造失败结果
    static Result failure(const std::string& errorMessage) {
        return Result(Error(errorMessage));
    }
    
    // 判断是否成功
    bool isSuccess() const {
        return std::holds_alternative<T>(data_);
    }
    
    bool isFailure() const {
        return !isSuccess();
    }
    
    // 获取成功值（仅在成功时有效）
    const T& getValue() const {
        return std::get<T>(data_);
    }
    
    T& getValue() {
        return std::get<T>(data_);
    }
    
    // 获取错误信息（仅在失败时有效）
    const std::string& getError() const {
        return std::get<Error>(data_).message;
    }
    
    // 隐式转换为bool，用于if判断
    explicit operator bool() const {
        return isSuccess();
    }
    
private:
    std::variant<T, Error> data_;
    
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}
};

// void特化版本，用于无返回值的操作
template<>
class Result<void> {
public:
    static Result success() {
        return Result(true);
    }
    
    static Result failure(const std::string& errorMessage) {
        return Result(false, errorMessage);
    }
    
    bool isSuccess() const {
        return success_;
    }
    
    bool isFailure() const {
        return !success_;
    }
    
    const std::string& getError() const {
        return errorMessage_;
    }
    
    explicit operator bool() const {
        return success_;
    }
    
private:
    bool success_;
    std::string errorMessage_;
    
    explicit Result(bool success, const std::string& error = "")
        : success_(success), errorMessage_(error) {}
};

} // namespace Utils

// 类型别名，简化使用
template<typename T>
using OperationResult = Utils::Result<T>;

using VoidResult = Utils::Result<void>;
