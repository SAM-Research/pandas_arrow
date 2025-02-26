//
// Created by dewe on 12/31/22.
//
#include "core.h"
#include <future>
#include "arrow/compute/exec.h"
#include "dataframe.h"
#include "group_by.h"

namespace pd {

date DateOffset::add(date currentDate, const DateOffset& dateOffset)
{
    using namespace boost::gregorian;
    switch (dateOffset.type)
    {
        case MonthEnd:
            currentDate += months(dateOffset.multiplier);
            currentDate = currentDate.end_of_month();
            break;

        case QuarterStart:
            currentDate += months(3 * dateOffset.multiplier);
            currentDate = date(currentDate.year(), (currentDate.month() - 1) / 3 * 3 + 1, 1);
            break;

        case QuarterEnd:
            currentDate += months(3 * dateOffset.multiplier);
            currentDate = date(currentDate.year(), (currentDate.month() - 1) / 3 * 3 + 3, 1) - days(1);
            break;

        case WeekStart:
            currentDate += weeks(dateOffset.multiplier);
            break;

        case WeekEnd:
            currentDate += weeks(dateOffset.multiplier);
            break;

        case MonthStart:
            currentDate += months(dateOffset.multiplier);
            currentDate = date(currentDate.year(), currentDate.month(), 1);
            break;

        case YearEnd:
            currentDate += years(dateOffset.multiplier);
            currentDate = date(currentDate.year(), Dec, 31);
            break;

        case YearStart:
            currentDate += years(dateOffset.multiplier);
            currentDate = date(currentDate.year(), Jan, 1);
            break;

        default:
            currentDate += days(dateOffset.multiplier);
    }

    return currentDate;
}

std::optional<DateOffset> DateOffset::FromString(const std::string& code)
{
    const auto [freq_unit, mul] = splitTimeSpan(code);
    DateOffset::Type type{ DateOffset::Day };

    if (freq_unit == "D")
    {
        type = DateOffset::Day;
    }
    else if (freq_unit == "WS")
    {
        type = DateOffset::WeekStart;
    }
    else if (freq_unit == "W")
    {
        type = DateOffset::WeekEnd;
    }
    else if (freq_unit == "MS")
    {
        type = DateOffset::MonthStart;
    }
    else if (freq_unit == "M")
    {
        type = DateOffset::MonthEnd;
    }
    else if (freq_unit == "Y")
    {
        type = DateOffset::YearEnd;
    }
    else if (freq_unit == "YS")
    {
        type = DateOffset::YearStart;
    }
    else if (freq_unit == "Q")
    {
        type = DateOffset::QuarterEnd;
    }
    else if (freq_unit == "QS")
    {
        type = DateOffset::QuarterStart;
    }
    else
    {
        return std::nullopt;
    }
    return std::make_optional<DateOffset>(type, mul);
}

std::pair<std::string, int> splitTimeSpan(std::string const& freq)
{
    auto it = std::find_if(freq.begin(), freq.end(), [](unsigned char x) { return std::isalpha(x); });

    std::ostringstream ss;
    std::string freq_unit, freqValueStr;
    int freq_value = 1;

    copy(it, freq.end(), std::ostream_iterator<uint8_t>(ss));
    freq_unit = ss.str();

    if (freq.begin() != it)
    {
        std::ostringstream ss2;
        copy(freq.begin(), it, std::ostream_iterator<uint8_t>(ss2));
        freqValueStr = ss2.str();
        freq_value = std::stoi(freqValueStr);
    }
    else if (std::ranges::any_of(freq, [](unsigned char x) { return std::isdigit(x); }))
    {
        throw std::runtime_error("Invalid time offset " + freq);
    }
    return { freq_unit, freq_value };
}

arrow::compute::CalendarUnit getCalendarUnit(char start_unit) {
    arrow::compute::CalendarUnit unit;
    switch (start_unit) {
        case 'n':
            unit = arrow::compute::CalendarUnit::NANOSECOND;
            break;
        case 'u':
            unit = arrow::compute::CalendarUnit::MICROSECOND;
            break;
        case 'm':
            unit = arrow::compute::CalendarUnit::MILLISECOND;
            break;
        case 'S':
            unit = arrow::compute::CalendarUnit::SECOND;
            break;
        case 'T':
            unit = arrow::compute::CalendarUnit::MINUTE;
            break;
        case 'H':
            unit = arrow::compute::CalendarUnit::HOUR;
            break;
        case 'D':
            unit = arrow::compute::CalendarUnit::DAY;
            break;
        case 'Q':
            unit = arrow::compute::CalendarUnit::QUARTER;
            break;
        case 'W':
            unit = arrow::compute::CalendarUnit::WEEK;
            break;
        case 'M':
            unit = arrow::compute::CalendarUnit::MONTH;
            break;
        default:
            throw std::runtime_error("invalid unit got " + std::string{start_unit});
    }
    return unit;
}

template<class Iterator = day_iterator, typename FreqTime = int>
std::shared_ptr<arrow::TimestampArray> date_range(
    date const& start,
    date const& end,
    FreqTime freq = 1,
    std::string const& tz = "")
{
    if (start >= end)
    {
        throw std::runtime_error("start date has to be less than end date");
    }

    if (freq < 1)
    {
        throw std::runtime_error("FREQ must be >= 1");
    }

    auto N = size_t(std::round(double(date_period(start, end).length().days()) / double(freq)));
    std::vector<int64_t> timestamps;
    timestamps.reserve(N);

    for (auto it = Iterator(start, freq); it <= end; ++it)
    {
        timestamps.push_back(fromDate(*it));
    }

    return toDateTime(timestamps, arrow::TimeUnit::NANO, tz);
}

template<class Iterator = day_iterator, typename FreqTime = int>
std::shared_ptr<arrow::TimestampArray> date_range(
    date const& start,
    int period,
    FreqTime freq,
    std::string const& /*unused*/)
{
    if (period < 0)
    {
        throw std::runtime_error("period has to be positive");
    }
    if (freq < 1)
    {
        throw std::runtime_error("FREQ must be >= 1");
    }

    Iterator it(start, freq);

    std::vector<int64_t> timestamps(period);

    std::generate_n(
        timestamps.begin(),
        period,
        [&it]()
        {
            auto result = fromDate(*it);
            ++it;
            return result;
        });
    return toDateTime(timestamps);
}

std::shared_ptr<arrow::TimestampArray> switchFunction(
    date const& start,
    auto const& end_or_period,
    const DateOffset& freq,
    std::string const& tz) {
    switch (freq.type) {
        case DateOffset::Day:
            return date_range<day_iterator>(start, end_or_period, freq.multiplier, tz);
        case DateOffset::MonthEnd:
            throw std::runtime_error("MonthEnd not supported use arrow month().groupby()");
        case DateOffset::MonthStart:
            return date_range<month_iterator>(start, end_or_period, freq.multiplier, tz);
        case DateOffset::QuarterStart: {
            if (start.month() / 3 != 0) {
                throw std::runtime_error("A quarter freq requires month is on a quarter, +/- with DateOffset");
            }
            return date_range<month_iterator>(start, end_or_period, freq.multiplier * 3, tz);
        }
        case DateOffset::QuarterEnd:
            throw std::runtime_error("QuarterEnd not supported use arrow quarter().groupby()");
        case DateOffset::WeekEnd:
            throw std::runtime_error("WeekEnd not supported use arrow weeks().groupby()");
        case DateOffset::WeekStart:
            return date_range<week_iterator>(start, end_or_period, freq.multiplier, tz);
        case DateOffset::YearEnd:
            throw std::runtime_error("YearEnd not supported use arrow year().groupby()");
        case DateOffset::YearStart:
            return date_range<year_iterator>(start, end_or_period, freq.multiplier, tz);
    }
    return {nullptr};
}

std::shared_ptr<arrow::TimestampArray> switchFunction(
    ptime const& start,
    auto const& end_or_period,
    std::string const& freq,
    std::string const& tz)
{
    return date_range(start, end_or_period, duration_from_string(freq), tz);
}

time_duration duration_from_string(std::string const& freq_unit,
                                   int freq_value)
{
    if (freq_unit == "H" or freq_unit == "hrs")
    {
        return hours(freq_value);
    }
    else if (freq_unit == "T" or freq_unit == "min")
    {
        return minutes(freq_value);
    }
    else if (freq_unit == "S")
    {
        return seconds(freq_value);
    }
    else if (freq_unit == "L" or freq_unit == "ms")
    {
        return milliseconds(freq_value);
    }
    else if (freq_unit == "U" or freq_unit == "us")
    {
        return microseconds(freq_value);
    }
    else if (freq_unit == "N" or freq_unit == "ns")
    {
        return pd::nanoseconds(freq_value);
    }
    throw std::runtime_error(
        "date_range with start:ptime_type is only compatible with "
        "[T/min S L/ms U/us N/ns] freq_unit");
}

std::shared_ptr<arrow::TimestampArray> date_range(
    ptime const& start,
    ptime const& end,
    time_duration const& freq,
    std::string const& tz)
{
    if (start >= end)
    {
        throw std::runtime_error("start date has to be less than end date");
    }

    if (freq.is_negative() or freq.is_zero())
    {
        throw std::runtime_error("FREQ must be positive");
    }

    std::vector<int64_t> timestamps;
    for (auto it = time_iterator(start, freq); it <= end; ++it)
    {
        timestamps.push_back(fromPTime(*it));
    }

    return toDateTime(timestamps, arrow::TimeUnit::NANO, tz);
}

std::shared_ptr<arrow::TimestampArray> date_range(
    ptime const& start,
    int period,
    time_duration const& freq,
    std::string const& tz)
{
    if (period <= 0)
    {
        throw std::runtime_error("period has to be positive");
    }

    if (freq.is_negative() or freq.is_zero())
    {
        throw std::runtime_error("FREQ must be positive");
    }

    time_iterator it(start, freq);
    std::vector<int64_t> timestamps(period);

    std::generate_n(
        timestamps.begin(),
        period,
        [it]() mutable
        {
            auto result = fromPTime(*it);
            ++it;
            return result;
        });

    return toDateTime(timestamps, arrow::TimeUnit::NANO, tz);
}

std::shared_ptr<arrow::TimestampArray> date_range(
    date const& start,
    date const& end,
    const DateOffset& freq,
    std::string const& tz)
{
    return switchFunction(start, end, freq, tz);
}

std::shared_ptr<arrow::TimestampArray> date_range(
    date const& start,
    int period,
    const DateOffset& freq,
    std::string const& tz)
{
    return switchFunction(start, period, freq, tz);
}

std::shared_ptr<arrow::TimestampArray> date_range(
    ptime const& start,
    ptime const& end,
    std::string const& freq,
    std::string const& tz)
{
    return switchFunction(start, end, freq, tz);
}

std::shared_ptr<arrow::TimestampArray> date_range(
    ptime const& start,
    int period,
    std::string const& freq,
    std::string const& tz)
{
    return switchFunction(start, period, freq, tz);
}

std::shared_ptr<arrow::Int64Array> range(int64_t start, int64_t end)
{
    const int64_t length = (end - start);
    arrow::Int64Builder builder;

    pd::ThrowOnFailure(builder.Reserve(length));
    for(int64_t i = 0; i < length; i++)
    {
        builder.UnsafeAppend(i);
    }
    return dynamic_pointer_cast<arrow::Int64Array>(builder.Finish().MoveValueUnsafe());
}

std::shared_ptr<arrow::UInt64Array> range(uint64_t start, uint64_t end)
{
    const uint64_t length = (end - start);
    arrow::UInt64Builder builder;

    pd::ThrowOnFailure(builder.Reserve(length));
    for(uint64_t i = 0; i < length; i++)
    {
        builder.UnsafeAppend(i);
    }
    return dynamic_pointer_cast<arrow::UInt64Array>(builder.Finish().MoveValueUnsafe());
}

std::shared_ptr<arrow::Array> combineIndexes(std::vector<Series::ArrayType> const& indexes, bool ignore_index)
{
    if (ignore_index)
    {
        std::vector<std::uint64_t> idx_len(indexes.size());
        std::ranges::transform(indexes, idx_len.begin(), [](Series::ArrayType const& idx) { return idx->length(); });
        return range(0UL, std::accumulate(idx_len.begin(), idx_len.end(), 0UL));
    }

    auto result = arrow::Concatenate(indexes);
    if (result.ok())
        return result.MoveValueUnsafe();

    throw std::runtime_error(result.status().ToString());
}

Series ReturnSeriesOrThrowOnError(arrow::Result<arrow::Datum>&& result)
{
    if (result.ok())
    {
        return pd::Series{ result->make_array(), false, "" };
    }
    throw std::runtime_error(result.status().ToString());
}

std::shared_ptr<arrow::DataType> promoteTypes(const std::vector<std::shared_ptr<arrow::DataType>>& types)
{

    if (types.empty())
    {
        return arrow::null();
    }

    std::shared_ptr<arrow::DataType> common_type = types[0];

    for (size_t i = 1; i < types.size(); i++)
    {

        auto current_type = types[i];
        if (arrow::is_temporal(current_type->id()))
        {
            current_type = arrow::int64();
        }

        if (arrow::is_numeric(current_type->id()))
        {
            if (current_type->id() > common_type->id())
            {
                common_type = types[i];
            }
        }
        else
        {
            return arrow::utf8();
        }
    }

    return common_type;
}
} // namespace pd
std::shared_ptr<arrow::Array> arrow::ScalarArray::Make(const std::vector<pd::Scalar>& x)
{
    if (x.empty())
    {
        return { nullptr };
    }
    auto builder = arrow::MakeBuilder(x.back().value()->type).MoveValueUnsafe();

    pd::ThrowOnFailure(builder->Reserve(x.size()));

    for (auto const& sc : x)
    {
        pd::ThrowOnFailure(builder->AppendScalar(*sc.value()));
    }

    return builder->Finish().MoveValueUnsafe();
}
