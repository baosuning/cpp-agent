#pragma once
// 依赖图：管理 PlanStep 之间的依赖关系，驱动执行顺序
#include <agent/types.h>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <queue>
#include <algorithm>

namespace agent {

class PlanGraph {
public:
    // 从 Plan 构建依赖图
    explicit PlanGraph(const Plan& plan) {
        for (const auto& step : plan.steps) {
            std::string id(step.id.begin(), step.id.end());
            steps_[id] = step;
        }

        // 构建依赖关系（跳过不存在的依赖）
        for (const auto& step : plan.steps) {
            std::string id(step.id.begin(), step.id.end());
            for (const auto& dep : step.depends_on) {
                std::string dep_str(dep.begin(), dep.end());
                if (steps_.find(dep_str) == steps_.end()) continue;
                dependencies_[id].insert(dep_str);
                dependents_[dep_str].insert(id);
            }
            // 没有依赖的步骤，确保有 entry
            if (dependencies_[id].empty()) {
                (void)dependencies_[id];  // 确保 key 存在
            }
        }
    }

    // 获取当前可执行的步骤（依赖已全部完成）
    std::vector<PlanStep> get_ready_steps() const {
        std::vector<PlanStep> ready;
        for (const auto& [id, step] : steps_) {
            if (completed_.count(id) || failed_.count(id) || skipped_.count(id)) continue;
            const auto& deps = dependencies_.at(id);
            bool all_deps_done = true;
            for (const auto& dep : deps) {
                if (!completed_.count(dep) && !failed_.count(dep) && !skipped_.count(dep)) {
                    all_deps_done = false;
                    break;
                }
            }
            if (all_deps_done) {
                ready.push_back(step);
            }
        }
        // 按 id 排序，保证确定性
        std::sort(ready.begin(), ready.end(), [](const PlanStep& a, const PlanStep& b) {
            return a.id < b.id;
        });
        return ready;
    }

    // 标记步骤完成
    void mark_completed(const u8str& step_id) {
        std::string id(step_id.begin(), step_id.end());
        completed_.insert(id);
    }

    // 标记步骤失败，返回 fallback_step（如果有）
    std::optional<u8str> mark_failed(const u8str& step_id) {
        std::string id(step_id.begin(), step_id.end());
        failed_.insert(id);
        auto it = steps_.find(id);
        if (it != steps_.end() && it->second.fallback_step) {
            return *it->second.fallback_step;
        }
        return std::nullopt;
    }

    // 标记步骤跳过
    void mark_skipped(const u8str& step_id) {
        std::string id(step_id.begin(), step_id.end());
        skipped_.insert(id);
    }

    // 是否还有剩余步骤
    bool has_remaining_steps() const {
        for (const auto& [id, step] : steps_) {
            if (!completed_.count(id) && !failed_.count(id) && !skipped_.count(id)) {
                return true;
            }
        }
        return false;
    }

    // 获取步骤
    std::optional<PlanStep> get_step(const u8str& step_id) const {
        std::string id(step_id.begin(), step_id.end());
        auto it = steps_.find(id);
        if (it != steps_.end()) return it->second;
        return std::nullopt;
    }

    // 检测循环依赖
    bool has_cycle() const {
        std::unordered_map<std::string, int> in_degree;
        for (const auto& [id, _] : steps_) {
            in_degree[id] = 0;
        }
        for (const auto& [id, deps] : dependencies_) {
            in_degree[id] = static_cast<int>(deps.size());
        }
        std::queue<std::string> q;
        for (const auto& [id, deg] : in_degree) {
            if (deg == 0) q.push(id);
        }
        int visited = 0;
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            ++visited;
            auto it = dependents_.find(cur);
            if (it != dependents_.end()) {
                for (const auto& dep : it->second) {
                    --in_degree[dep];
                    if (in_degree[dep] == 0) q.push(dep);
                }
            }
        }
        return visited != static_cast<int>(steps_.size());
    }

    // 获取统计信息
    int completed_count() const { return static_cast<int>(completed_.size()); }
    int failed_count() const { return static_cast<int>(failed_.size()); }
    int skipped_count() const { return static_cast<int>(skipped_.size()); }
    int total_count() const { return static_cast<int>(steps_.size()); }

    // 更新步骤（重规划后更新），同时更新依赖关系
    void update_step(const PlanStep& step) {
        std::string id(step.id.begin(), step.id.end());
        steps_[id] = step;

        // 清除旧依赖关系
        auto old_deps_it = dependencies_.find(id);
        if (old_deps_it != dependencies_.end()) {
            for (const auto& old_dep : old_deps_it->second) {
                auto dep_it = dependents_.find(old_dep);
                if (dep_it != dependents_.end()) {
                    dep_it->second.erase(id);
                }
            }
            old_deps_it->second.clear();
        }

        // 建立新依赖关系
        for (const auto& dep : step.depends_on) {
            std::string dep_str(dep.begin(), dep.end());
            dependencies_[id].insert(dep_str);
            dependents_[dep_str].insert(id);
        }
    }

    // 添加新步骤（重规划后添加），忽略不存在的依赖
    void add_step(const PlanStep& step) {
        std::string id(step.id.begin(), step.id.end());
        steps_[id] = step;
        for (const auto& dep : step.depends_on) {
            std::string dep_str(dep.begin(), dep.end());
            // 跳过不存在的依赖步骤
            if (steps_.find(dep_str) == steps_.end()) continue;
            dependencies_[id].insert(dep_str);
            dependents_[dep_str].insert(id);
        }
    }

    // 清除失败状态（重试时使用）
    void clear_failed(const u8str& step_id) {
        std::string id(step_id.begin(), step_id.end());
        failed_.erase(id);
    }

private:
    std::unordered_map<std::string, PlanStep>           steps_;
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencies_;  // step -> depends_on
    std::unordered_map<std::string, std::unordered_set<std::string>> dependents_;    // step -> depended_by
    std::unordered_set<std::string>                      completed_;
    std::unordered_set<std::string>                      failed_;
    std::unordered_set<std::string>                      skipped_;
};

} // namespace agent
