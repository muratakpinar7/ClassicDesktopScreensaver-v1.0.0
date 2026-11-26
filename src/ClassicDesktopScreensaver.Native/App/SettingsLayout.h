#pragma once

#include <algorithm>
#include <numeric>
#include <span>
#include <vector>

namespace classic_desktop_screensaver
{
    enum class LayoutCellMode
    {
        Fixed,
        Flexible,
        Fill
    };

    struct LayoutCellWidthRequest
    {
        int preferredWidth{};
        int minimumWidth{};
        LayoutCellMode mode{ LayoutCellMode::Fill };
    };

    struct LayoutCellWidths
    {
        std::vector<int> widths;
        bool stacked{};
    };

    [[nodiscard]] inline LayoutCellWidths CalculateLayoutCellWidths(
        int availableWidth,
        int gap,
        std::span<const LayoutCellWidthRequest> cells)
    {
        LayoutCellWidths result{};
        result.widths.reserve(cells.size());
        if (cells.empty()) return result;

        availableWidth = std::max(1, availableWidth);
        gap = std::max(0, gap);
        const int totalGaps = gap * static_cast<int>(cells.size() - 1);
        int minimumTotal = totalGaps;
        for (const auto& cell : cells)
        {
            const int minimum = cell.mode == LayoutCellMode::Fixed
                ? std::max(1, cell.preferredWidth)
                : std::max(1, cell.minimumWidth);
            minimumTotal += minimum;
        }

        if (cells.size() > 1 && minimumTotal > availableWidth)
        {
            result.stacked = true;
            for (const auto& cell : cells)
            {
                const int preferred = std::max(1, cell.preferredWidth);
                result.widths.push_back(cell.mode == LayoutCellMode::Fixed
                    ? std::min(availableWidth, preferred)
                    : availableWidth);
            }
            return result;
        }

        int used = totalGaps;
        for (const auto& cell : cells)
        {
            int width = std::max(1, cell.preferredWidth);
            if (cell.mode == LayoutCellMode::Fill)
                width = std::max(1, cell.minimumWidth);
            result.widths.push_back(width);
            used += width;
        }

        int excess = std::max(0, used - availableWidth);
        for (std::size_t index = cells.size(); index-- > 0 && excess > 0;)
        {
            if (cells[index].mode != LayoutCellMode::Flexible) continue;
            const int minimum = std::max(1, cells[index].minimumWidth);
            const int reducible = std::max(0, result.widths[index] - minimum);
            const int reduction = std::min(reducible, excess);
            result.widths[index] -= reduction;
            excess -= reduction;
        }

        int remaining = std::max(0, availableWidth - totalGaps -
            std::accumulate(result.widths.begin(), result.widths.end(), 0));
        std::size_t fillCount = static_cast<std::size_t>(std::count_if(
            cells.begin(), cells.end(), [](const LayoutCellWidthRequest& cell)
            {
                return cell.mode == LayoutCellMode::Fill;
            }));
        if (fillCount != 0)
        {
            for (std::size_t index = 0; index < cells.size(); ++index)
            {
                if (cells[index].mode != LayoutCellMode::Fill) continue;
                const int share = remaining / static_cast<int>(fillCount);
                result.widths[index] += share;
                remaining -= share;
                --fillCount;
            }
        }
        return result;
    }
}
