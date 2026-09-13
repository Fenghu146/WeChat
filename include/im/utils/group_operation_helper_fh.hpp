#pragma once
// ============================================================
// GroupOperationHelperFH —— 群操作辅助函数
// 提取重复的授权模式，减少GroupContextFH构造和execute调用的重复
// ============================================================
#include "im/context/action_fh.hpp"
#include "im/context/group_context_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include <chrono>
#include <memory>

namespace Utils {

// 群操作执行器 - 封装重复的授权模式
class GroupOperationExecutor {
public:
    explicit GroupOperationExecutor(GroupFH* group) : group_(group) {}
    
    // 执行无需目标和消息的操作
    bool execute(ActionFH action, 
                const std::shared_ptr<UserFH>& operatorUser) {
        if (!group_ || !operatorUser) return false;
        
        GroupContextFH context{
            group_, 
            operatorUser, 
            nullptr,  // target
            nullptr,  // message
            std::chrono::system_clock::now()
        };
        
        return group_->execute(action, context);
    }
    
    // 执行需要目标的操作
    bool execute(ActionFH action,
                const std::shared_ptr<UserFH>& operatorUser,
                const std::shared_ptr<UserFH>& target) {
        if (!group_ || !operatorUser || !target) return false;
        
        GroupContextFH context{
            group_,
            operatorUser,
            target,
            nullptr,  // message
            std::chrono::system_clock::now()
        };
        
        return group_->execute(action, context);
    }
    
    // 执行需要消息的操作
    bool execute(ActionFH action,
                const std::shared_ptr<UserFH>& operatorUser,
                const std::shared_ptr<MessageFH>& message) {
        if (!group_ || !operatorUser || !message) return false;
        
        GroupContextFH context{
            group_,
            operatorUser,
            nullptr,  // target
            message,
            std::chrono::system_clock::now()
        };
        
        return group_->execute(action, context);
    }
    
    // 执行需要目标和消息的操作
    bool execute(ActionFH action,
                const std::shared_ptr<UserFH>& operatorUser,
                const std::shared_ptr<UserFH>& target,
                const std::shared_ptr<MessageFH>& message) {
        if (!group_ || !operatorUser || !target || !message) return false;
        
        GroupContextFH context{
            group_,
            operatorUser,
            target,
            message,
            std::chrono::system_clock::now()
        };
        
        return group_->execute(action, context);
    }
    
private:
    GroupFH* group_;
};

// 辅助函数：创建标准的GroupContextFH
inline GroupContextFH createGroupContext(
    GroupFH* group,
    const std::shared_ptr<UserFH>& operatorUser,
    const std::shared_ptr<UserFH>& target = nullptr,
    const std::shared_ptr<MessageFH>& message = nullptr) {
    
    return GroupContextFH{
        group,
        operatorUser,
        target,
        message,
        std::chrono::system_clock::now()
    };
}

} // namespace Utils
