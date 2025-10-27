#include <corona/core/detail/SystemHubs.h>

#include <iostream>
#include <memory>
#include <unordered_set>

using namespace Corona;

namespace {
struct SamplePayload {
    int counter = 0;
};
}  // namespace

int main() {
    DataCacheHub cacheHub;

    auto& cache = cacheHub.get<SamplePayload>();
    cache.insert(1, std::make_shared<SamplePayload>());

    cache.modify(1, [](std::shared_ptr<SamplePayload> payload) {
        if (payload) {
            payload->counter += 42;
        }
    });

    std::unordered_set<SafeDataCache<SamplePayload>::id_type> ids{1};
    cache.safe_loop_foreach(ids, [](SafeDataCache<SamplePayload>::id_type id, std::shared_ptr<SamplePayload> payload) {
        if (payload) {
            std::cout << "Payload " << id << " counter=" << payload->counter << "\n";
        }
    });

    const auto snapshot = cache.get(1);
    const int payloadValue = snapshot ? snapshot->counter : 0;
    std::cout << "Final value=" << payloadValue << "\n";

    return 0;
}