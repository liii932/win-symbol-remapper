#pragma once

#include "scheme_config.h"

namespace symbols {

// Owns the startup snapshot. Neither the tables nor their addresses change.
class SchemeManager {
public:
    explicit SchemeManager(Configuration config);
    SchemeManager(const SchemeManager&) = delete;
    SchemeManager& operator=(const SchemeManager&) = delete;
    const Configuration& config() const { return config_; }
    const Scheme& current() const { return config_.schemes[current_]; }
    std::size_t currentIndex() const { return current_; }
    bool select(std::size_t index);
    bool select(const std::string& id);
    bool next();

private:
    const Configuration config_;
    std::size_t current_ = 0;
};

} // namespace symbols
