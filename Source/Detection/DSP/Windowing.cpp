#include "Windowing.h"

namespace DetectionDSP
{
    namespace Windowing
    {
        static constexpr int kWindowSize = 15600;
        static constexpr int kHopSize = 7800;

        std::vector<std::vector<float>> makeWindows(const std::vector<float>& mono16k,
                                                    int maxWindows)
        {
            std::vector<std::vector<float>> windows;
            if (mono16k.empty() || maxWindows <= 0)
                return windows;

            const int n = (int) mono16k.size();

            // If shorter than one window, pad.
            if (n <= kWindowSize)
            {
                std::vector<float> w(kWindowSize, 0.0f);
                for (int i = 0; i < n; ++i)
                    w[(size_t) i] = mono16k[(size_t) i];
                windows.push_back(std::move(w));
                return windows;
            }

            int start = 0;
            while (start < n && (int) windows.size() < maxWindows)
            {
                std::vector<float> w(kWindowSize, 0.0f);
                const int remaining = n - start;
                const int copyCount = std::min(kWindowSize, remaining);
                for (int i = 0; i < copyCount; ++i)
                    w[(size_t) i] = mono16k[(size_t) (start + i)];
                windows.push_back(std::move(w));
                start += kHopSize;
            }

            if (windows.empty())
            {
                std::vector<float> w(kWindowSize, 0.0f);
                const int copyCount = std::min(kWindowSize, n);
                for (int i = 0; i < copyCount; ++i)
                    w[(size_t) i] = mono16k[(size_t) i];
                windows.push_back(std::move(w));
            }

            return windows;
        }
    } // namespace Windowing
} // namespace DetectionDSP

