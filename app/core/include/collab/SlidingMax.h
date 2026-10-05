#pragma once

// 直近 window 個の最大値を 1 サンプルごとに O(1) で求める（最小値は値を負にして使う）。

#include <algorithm>
#include <utility>
#include <vector>

namespace collab
{

struct SlidingMax
{
    void prepare (int w)
    {
        window = std::max (1, w);
        buffer.assign ((size_t) window + 1, {});
        reset();
    }

    void reset()
    {
        count = 0;
        head = size = 0;
    }

    /** 値を足して、窓の中の最大値を返す。 */
    double push (double value)
    {
        if (buffer.empty())
            prepare (window);   // prepare() 前に呼ばれたとき

        const size_t cap = buffer.size();

        // 後ろから、新しい値以下のものを捨てる（もう最大になることはない）
        while (size > 0 && buffer[(head + size - 1) % cap].second <= value)
            --size;

        buffer[(head + size) % cap] = { count, value };
        ++size;

        // 窓から外れた古いものを前から捨てる
        while (buffer[head].first <= count - window)
        {
            head = (head + 1) % cap;
            --size;
        }

        ++count;
        return buffer[head].second;
    }

    int window = 1;
    long long count = 0;
    std::vector<std::pair<long long, double>> buffer;   // 単調減少の列（輪っか）
    size_t head = 0, size = 0;
};

} // namespace collab
