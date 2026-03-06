#pragma once

#include <vector>

namespace DetectionDSP
{
    namespace Windowing
    {
        std::vector<std::vector<float>> makeWindows(const std::vector<float>& mono16k,
                                                    int maxWindows);
    }
} // namespace DetectionDSP

