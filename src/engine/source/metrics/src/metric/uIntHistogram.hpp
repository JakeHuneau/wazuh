#ifndef _METRIC_METRIC_UINTHISTOGRAM_HPP
#define _METRIC_METRIC_UINTHISTOGRAM_HPP

#include <metrics/imetric.hpp>

#include "metric/metric.hpp"
#include "ot.hpp"

namespace metrics
{
using OtUIntHistogramPtr = otapi::unique_ptr<otapi::Histogram<uint64_t>>;

class UIntHistogram : public BaseMetric<uint64_t>
{
private:
    OtUIntHistogramPtr m_histogram;

public:
    UIntHistogram(std::string&& name, std::string&& description, std::string&& unit)
        : BaseMetric<uint64_t>(std::move(name), std::move(description), std::move(unit))
        , m_histogram(nullptr)
    {
    }

    UIntHistogram(const UIntHistogram&) = delete;
    UIntHistogram& operator=(const UIntHistogram&) = delete;
    UIntHistogram(UIntHistogram&&) = delete;
    UIntHistogram& operator=(UIntHistogram&&) = delete;

    ~UIntHistogram() override = default;

    void otCreate(const Manager::ImplOtPipeline& otPipeline) override
    {

        m_histogram =
            otPipeline.provider->GetMeter(DEFAULT_METER_NAME)->CreateUInt64Histogram(m_name, m_description, m_unit);
    }

    void otDestroy() override { m_histogram.reset(); }

    void otUpdate(uint64_t value) override { m_histogram->Record(value, otapi::RuntimeContext::GetCurrent()); }
};

} // namespace metrics

#endif // _METRIC_METRIC_UINTHISTOGRAM_HPP
