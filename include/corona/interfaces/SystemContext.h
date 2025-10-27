#pragma once

#include <corona/interfaces/Concurrency.h>
#include <corona/interfaces/ServiceLocator.h>

namespace Corona {
class DataCacheHub;
}  // namespace Corona

namespace Corona::Interfaces {

struct SystemContext {
    ServiceLocator& services;
    ICommandQueue* queue = nullptr;
    Corona::DataCacheHub* caches = nullptr;
};

}  // namespace Corona::Interfaces