namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Shared, owner-qualified time budgets for view-only browser chrome transitions.</summary>
    internal static class BrowserChromeTiming
    {
        /// <summary>Quick edit-surface reveal/fade duration, in milliseconds.</summary>
        internal const double EditSurfaceMilliseconds = 80;

        /// <summary>Input/background morph duration in both directions, in milliseconds.</summary>
        internal const double AddressMorphMilliseconds = 100;

        /// <summary>Settling delay after native Hiding starts, not a reported OS animation duration.</summary>
        internal const double KeyboardHideSettleMilliseconds = 120;

        /// <summary>Independent visible-bounds reposition budget, in milliseconds.</summary>
        internal const double RepositionMilliseconds = 150;

        /// <summary>App-bar panel upward/downward slide, matching the existing menu choreography.</summary>
        internal const double MenuSlideMilliseconds = 180;
    }

    /// <summary>Shared chrome geometry matching compiled XAML so layout math stays single-sourced.</summary>
    internal static class BrowserChromeGeometry
    {
        /// <summary>Shared bottom address/app-bar surface height, matching ChromeGrid.Height.</summary>
        internal const double ChromeHeight = 48;

        /// <summary>Expanded primary icon-and-label strip, matching the menu second row.</summary>
        internal const double ExpandedActionsHeight = 56;

        /// <summary>Default platform width of a native AppBarButton, matching HelloActionHost.</summary>
        internal const double HelloActionWidth = 68;
    }
}
