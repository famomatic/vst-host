#include "audio/Resampler.h"

namespace host::audio
{
    // Each JUCE interpolator is a distinct concrete type, so the quality
    // switch is centralised here. The header keeps the streaming logic
    // inline for performance; this TU just instantiates the kernels.
    std::unique_ptr<IResamplerChannel> createResamplerChannel(ResamplerQuality quality)
    {
        switch (quality)
        {
            case ResamplerQuality::linear:
                return std::make_unique<ResamplerChannel<juce::LinearInterpolator>>();
            case ResamplerQuality::catmullRom:
                return std::make_unique<ResamplerChannel<juce::CatmullRomInterpolator>>();
            case ResamplerQuality::windowedSinc:
                return std::make_unique<ResamplerChannel<juce::WindowedSincInterpolator>>();
            case ResamplerQuality::lagrange:
            default:
                return std::make_unique<ResamplerChannel<juce::LagrangeInterpolator>>();
        }
    }
}
