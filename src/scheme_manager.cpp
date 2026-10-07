#include "scheme_manager.h"

#include <utility>

namespace symbols {
SchemeManager::SchemeManager(Configuration config)
    : config_(std::move(config)) {
    select(config_.startupId);
}

bool SchemeManager::select(std::size_t index) {
    if (index >= config_.schemes.size() || index == current_) return false;
    current_ = index;
    return true;
}

bool SchemeManager::select(const std::string& id) {
    for (std::size_t i = 0; i < config_.schemes.size(); ++i) {
        if (config_.schemes[i].id == id) return select(i);
    }
    return false;
}

bool SchemeManager::next() {
    return select((current_ + 1) % config_.schemes.size());
}
} // namespace symbols
