namespace WebKitMiniBrowser
{
    /// <summary>
    /// Root layout margins keeping shell content inside the visible bounds,
    /// in logical pixels.
    /// </summary>
    public struct VisibleBoundsMargins
    {
        /// <summary>Margin on the left edge.</summary>
        public double Left;

        /// <summary>Margin on the top edge.</summary>
        public double Top;

        /// <summary>Margin on the right edge.</summary>
        public double Right;

        /// <summary>Margin on the bottom edge.</summary>
        public double Bottom;
    }

    /// <summary>
    /// Pure visible-bounds math shared by the shell layout and the
    /// host-runnable unit check. This type has no UWP dependencies so the
    /// check compiles it with the desktop compiler.
    /// </summary>
    public static class VisibleBoundsHelper
    {
        /// <summary>
        /// Computes the root layout margins from the window bounds
        /// (<paramref name="x"/>, <paramref name="y"/>, <paramref name="w"/>,
        /// <paramref name="h"/>) and the visible bounds (<paramref name="vx"/>,
        /// <paramref name="vy"/>, <paramref name="vw"/>, <paramref name="vh"/>),
        /// all in logical pixels. Degenerate (non-positive) widths or heights
        /// fall back to zero margins (full window).
        /// An optional input-pane rectangle uses window-local coordinates.
        /// A finite, positive rectangle strictly overlapping visible content and
        /// reaching the visible bottom computed from the normal margins (with
        /// one logical pixel of tolerance) raises the bottom margin to the distance
        /// from its top to the window bottom, without adding to the existing margin.
        /// The bottom margin is limited to the window height remaining below the
        /// top margin.
        /// </summary>
        /// <param name="x">Window left.</param>
        /// <param name="y">Window top.</param>
        /// <param name="w">Window width.</param>
        /// <param name="h">Window height.</param>
        /// <param name="vx">Visible-bounds left.</param>
        /// <param name="vy">Visible-bounds top.</param>
        /// <param name="vw">Visible-bounds width.</param>
        /// <param name="vh">Visible-bounds height.</param>
        /// <param name="occludedX">Input-pane left relative to the window.</param>
        /// <param name="occludedY">Input-pane top relative to the window.</param>
        /// <param name="occludedWidth">Input-pane width; zero means no occlusion.</param>
        /// <param name="occludedHeight">Input-pane height; zero means no occlusion.</param>
        /// <returns>The margins to apply as the root layout margin.</returns>
        public static VisibleBoundsMargins ComputeMargins(
            double x, double y, double w, double h,
            double vx, double vy, double vw, double vh,
            double occludedX = 0, double occludedY = 0,
            double occludedWidth = 0, double occludedHeight = 0)
        {
            VisibleBoundsMargins margins = new VisibleBoundsMargins();
            if (w <= 0 || h <= 0 || vw <= 0 || vh <= 0)
            {
                return margins;
            }

            margins.Left = System.Math.Max(0, vx - x);
            margins.Top = System.Math.Max(0, vy - y);
            margins.Right = System.Math.Max(0, (x + w) - (vx + vw));
            margins.Bottom = System.Math.Max(0, (y + h) - (vy + vh));

            if (!IsFinite(occludedX) || !IsFinite(occludedY)
                || !IsFinite(occludedWidth) || !IsFinite(occludedHeight)
                || occludedWidth <= 0 || occludedHeight <= 0)
            {
                return margins;
            }

            double occludedRight = occludedX + occludedWidth;
            double occludedBottom = occludedY + occludedHeight;
            double visibleRight = w - margins.Right;
            double visibleBottom = h - margins.Bottom;
            if (IsFinite(occludedRight) && IsFinite(occludedBottom)
                && margins.Left < visibleRight && margins.Top < visibleBottom
                && occludedRight > margins.Left && occludedX < visibleRight
                && occludedBottom > margins.Top && occludedY < visibleBottom
                && occludedBottom >= visibleBottom - 1)
            {
                double usableHeight = System.Math.Max(0, h - margins.Top);
                margins.Bottom = System.Math.Min(usableHeight,
                    System.Math.Max(margins.Bottom, h - occludedY));
            }
            return margins;
        }

        /// <summary>Checks whether a coordinate or extent is finite.</summary>
        /// <param name="value">The coordinate or extent to check.</param>
        /// <returns>True for a finite value; otherwise false.</returns>
        private static bool IsFinite(double value)
        {
            return !double.IsNaN(value) && !double.IsInfinity(value);
        }
    }
}
