#if LAYOUT_DIAGNOSTICS
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.Foundation;
using Windows.Storage;
using Windows.System.Profile;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using WebKitMiniBrowser.Views.Controls;

namespace WebKitMiniBrowser
{
    /// <summary>
    /// Records optional layout and input-state diagnostics for the full-window shell,
    /// address editing, and menu overlay with one set of visible-bounds margins.
    /// Requires LAYOUT_DIAGNOSTICS.
    /// </summary>
    internal static class LayoutDiagnostics
    {
        /// <summary>Identifies the report format and the layout being checked.</summary>
        private const string ReportVersion = "MiniBrowser.LayoutDiagnostics/v1 (UseCoreWindow, manual visible bounds)";

        /// <summary>Name of the report stored in the application's LocalState folder.</summary>
        private const string ReportFileName = "layout-report.txt";

        /// <summary>Maximum retained snapshots, including the latest snapshot.</summary>
        private const int MaximumSnapshots = 12;

        /// <summary>Number of independently reported base layout checks in every snapshot.</summary>
        private const int CheckCount = 8;

        /// <summary>Number of additional geometry checks evaluated with the menu open.</summary>
        private const int MenuCheckCount = 4;

        /// <summary>Number of additional layout and focus checks evaluated while editing the address.</summary>
        private const int AddressEditingCheckCount = 2;

        /// <summary>Allowed geometry difference in logical pixels.</summary>
        private const double GeometryTolerance = 1.0;

        /// <summary>Required height of the idle bottom browser chrome, matching BrowserChromeGeometry.</summary>
        private const double ChromeHeight = BrowserChromeGeometry.ChromeHeight;

        /// <summary>Required height while editing, matching BrowserChromeGeometry (no 44-DIP mode).</summary>
        private const double EditingChromeHeight = BrowserChromeGeometry.ChromeHeight;

        /// <summary>Required left and right address margins while editing, matching XAML.</summary>
        private const double AddressEditingMargin = 4.0;

        /// <summary>Required address font size while editing, matching XAML.</summary>
        private const double AddressEditingFontSize = 15.0;

        /// <summary>Required 2-DIP progress line height, matching MainPage.xaml.</summary>
        private const double LoadingProgressHeight = 2.0;

        /// <summary>Serializes report writes and access to the bounded history.</summary>
        private static readonly SemaphoreSlim ReportGate = new SemaphoreSlim(1, 1);

        /// <summary>Current-process snapshots, newest first, protected by ReportGate.</summary>
        private static readonly List<string> Snapshots = new List<string>(MaximumSnapshots);

        /// <summary>
        /// Completes pending layout on the UI thread, then captures layout and input state and
        /// writes the latest snapshot plus bounded rotation history to
        /// LocalState/layout-report.txt. It does not read an existing report or inspect text.
        /// Capture and storage errors are logged with their original HResult and
        /// rethrown for the caller to handle. Failed transforms fail dependent checks.
        /// </summary>
        /// <param name="page">The current page whose pending layout is completed before capture.</param>
        /// <param name="shellRoot">The root grid carrying the visible-bounds margins.</param>
        /// <param name="browserChrome">The bottom browser chrome grid.</param>
        /// <param name="loadingProgress">The progress line immediately above browser chrome.</param>
        /// <param name="addressBox">The address control; only geometry, focus, and font metadata are inspected.</param>
        /// <param name="menuSheet">The menu overlay border; only layout properties are inspected.</param>
        /// <param name="menuOpen">Whether the menu is open and its geometry checks must be evaluated.</param>
        /// <param name="addressEditing">Whether the address is being edited and its layout and focus checks must be evaluated.</param>
        /// <returns>A task completing when the report has been written.</returns>
        internal static async Task CaptureAsync(Page page, Grid shellRoot, Grid browserChrome, ProgressBar loadingProgress,
            TextBox addressBox, AddressBarControl addressBar, BrowserAppBarControl appBar,
            Border menuSheet, bool menuOpen, bool addressEditing)
        {
            try
            {
                if (page == null)
                    throw new ArgumentNullException(nameof(page));
                if (shellRoot == null)
                    throw new ArgumentNullException(nameof(shellRoot));
                if (browserChrome == null)
                    throw new ArgumentNullException(nameof(browserChrome));
                if (loadingProgress == null)
                    throw new ArgumentNullException(nameof(loadingProgress));
                if (addressBox == null)
                    throw new ArgumentNullException(nameof(addressBox));
                if (addressBar == null)
                    throw new ArgumentNullException(nameof(addressBar));
                if (appBar == null)
                    throw new ArgumentNullException(nameof(appBar));
                if (menuSheet == null)
                    throw new ArgumentNullException(nameof(menuSheet));

                page.UpdateLayout();
                // Capture before any await so a later rotation cannot mix this snapshot's geometry.
                string snapshot = CreateSnapshot(page, shellRoot, browserChrome, loadingProgress,
                    addressBox, addressBar, appBar, menuSheet, menuOpen, addressEditing);
                await ReportGate.WaitAsync();
                try
                {
                    Snapshots.Insert(0, snapshot);
                    if (Snapshots.Count > MaximumSnapshots)
                        Snapshots.RemoveAt(Snapshots.Count - 1);

                    string report = CreateReport();
                    StorageFile file = await ApplicationData.Current.LocalFolder.CreateFileAsync(
                        ReportFileName, CreationCollisionOption.ReplaceExisting);
                    await FileIO.WriteTextAsync(file, report);
                }
                finally
                {
                    ReportGate.Release();
                }
            }
            catch (Exception exception)
            {
                LogError("CaptureAsync", exception);
                throw;
            }
        }

        /// <summary>Reads a consistent UI-thread snapshot and evaluates eight base checks, plus three for an open menu and two while editing.</summary>
        /// <param name="page">The current page.</param>
        /// <param name="shellRoot">The margin-bearing shell grid.</param>
        /// <param name="browserChrome">The bottom browser chrome.</param>
        /// <param name="loadingProgress">The progress bar immediately above browser chrome.</param>
        /// <param name="addressBox">The address control.</param>
        /// <param name="menuSheet">The menu overlay border.</param>
        /// <param name="menuOpen">Whether the menu's measured geometry must satisfy the additional checks.</param>
        /// <param name="addressEditing">Whether the address layout and focus must satisfy the additional editing checks.</param>
        /// <returns>A version-tagged snapshot with its complete failure count.</returns>
        private static string CreateSnapshot(Page page, Grid shellRoot, Grid browserChrome, ProgressBar loadingProgress,
            TextBox addressBox, AddressBarControl addressBar, BrowserAppBarControl appBar,
            Border menuSheet, bool menuOpen, bool addressEditing)
        {
            Window window = Window.Current;
            if (window == null)
                throw new InvalidOperationException("Layout diagnostics require the current UI thread.");

            ApplicationView view = ApplicationView.GetForCurrentView();
            ApplicationTheme requestedTheme = Application.Current.RequestedTheme;
            Rect windowBounds = window.Bounds;
            Rect visibleBounds = view.VisibleBounds;
            Rect inputPaneOccludedRect = InputPane.GetForCurrentView().OccludedRect;
            ApplicationViewBoundsMode boundsMode = view.DesiredBoundsMode;
            UIElement windowContent = window.Content;
            Frame frame = windowContent as Frame;
            Rect frameSize = frame == null ? new Rect() : new Rect(0, 0, frame.ActualWidth, frame.ActualHeight);
            Rect pageSize = new Rect(0, 0, page.ActualWidth, page.ActualHeight);
            Rect rootSize = new Rect(0, 0, shellRoot.ActualWidth, shellRoot.ActualHeight);
            Rect chromeSize = new Rect(0, 0, browserChrome.ActualWidth, browserChrome.ActualHeight);
            Rect progressSize = new Rect(0, 0, loadingProgress.ActualWidth, loadingProgress.ActualHeight);
            Visibility progressVisibility = loadingProgress.Visibility;
            double expectedChromeHeight = addressEditing ? EditingChromeHeight : ChromeHeight;
            Rect addressSize = new Rect(0, 0, addressBox.ActualWidth, addressBox.ActualHeight);
            FocusState addressFocusState = addressBox.FocusState;
            VerticalAlignment addressVerticalContentAlignment = addressBox.VerticalContentAlignment;
            double addressFontSize = addressBox.FontSize;
            Thickness addressPadding = addressBox.Padding;
            Visibility? reloadButtonVisibility = addressBar.ReloadActionElement.Visibility;
            Visibility? stopButtonVisibility = addressBar.StopActionElement.Visibility;
            Visibility? moreButtonVisibility = addressBar.MoreButton.Visibility;
            Visibility? addressDividerVisibility = addressBar.AddressDividerElement.Visibility;
            AppBarButton helloActionButton = appBar.HelloActionButton;
            Border menuTopDivider = appBar.MenuTopDivider;
            Rect helloActionSize = helloActionButton == null ? new Rect()
                : new Rect(0, 0, helloActionButton.ActualWidth, helloActionButton.ActualHeight);
            double menuWidth = menuSheet.Width;
            Rect menuSize = new Rect(0, 0, menuSheet.ActualWidth, menuSheet.ActualHeight);
            double? expectedMenuWidth = HasPositiveSize(rootSize)
                ? (double?)(rootSize.Width > rootSize.Height ? rootSize.Width / 2.0 : rootSize.Width)
                : null;
            Rect? frameSlot = frame == null ? (Rect?)null : LayoutInformation.GetLayoutSlot(frame);
            Rect pageSlot = LayoutInformation.GetLayoutSlot(page);
            Rect rootSlot = LayoutInformation.GetLayoutSlot(shellRoot);
            Rect? pageBounds = frame == null ? (Rect?)null : GetTransformedBounds(page, frame, "Page to Frame");
            Rect? rootBounds = GetTransformedBounds(shellRoot, page, "ShellRoot to Page");
            Rect? chromeBounds = GetTransformedBounds(browserChrome, shellRoot, "BrowserChrome to ShellRoot");
            Rect? progressBounds = GetTransformedBounds(loadingProgress, shellRoot, "LoadingProgress to ShellRoot");
            Rect? addressBounds = GetTransformedBounds(addressBox, shellRoot, "AddressBox to ShellRoot");
            Rect? helloActionBounds = !menuOpen || helloActionButton == null ? (Rect?)null
                : GetTransformedBounds(helloActionButton, shellRoot, "HelloActionButton to ShellRoot");
            Rect? menuTopDividerBounds = !menuOpen || menuTopDivider == null ? (Rect?)null
                : GetTransformedBounds(menuTopDivider, shellRoot, "MenuTopDivider to ShellRoot");
            Rect? menuBounds = GetTransformedBounds(menuSheet, shellRoot, "MenuSheet to ShellRoot");
            Thickness rootMargin = shellRoot.Margin;
            Rect fullWindowSlot = new Rect(0, 0, windowBounds.Width, windowBounds.Height);
            Rect? expectedRoot = GetExpectedRootBounds(windowBounds, visibleBounds, inputPaneOccludedRect, rootMargin.Bottom);

            // Unarranged Frame/Page/Root surfaces must never produce a misleading PASS.
            bool geometryReady = HasPositiveSize(windowBounds) && HasPositiveSize(visibleBounds)
                && expectedRoot.HasValue && HasPositiveSize(expectedRoot.Value)
                && frame != null && HasPositiveSize(frameSize)
                && HasPositiveSize(pageSize) && HasPositiveSize(rootSize);
            bool addressGeometryReady = geometryReady && HasPositiveSize(addressSize)
                && addressBounds.HasValue && HasPositiveSize(addressBounds.Value);

            StringBuilder body = new StringBuilder();
            PackageVersion packageVersion = Package.Current.Id.Version;
            body.AppendLine(string.Format(CultureInfo.InvariantCulture, "PackageVersion: {0}.{1}.{2}.{3}",
                packageVersion.Major, packageVersion.Minor, packageVersion.Build, packageVersion.Revision));
            body.AppendLine("OS.DeviceFamily: " + AnalyticsInfo.VersionInfo.DeviceFamily);
            body.AppendLine("OS.Version: " + FormatOsVersion(AnalyticsInfo.VersionInfo.DeviceFamilyVersion));
            body.AppendLine("ApplicationView.DesiredBoundsMode: " + boundsMode);
            body.AppendLine("Application.Current.RequestedTheme: " + requestedTheme);
            body.AppendLine("Units: logical pixels (DIP); tolerance: " + GeometryTolerance.ToString(CultureInfo.InvariantCulture));
            body.AppendLine("Window.Bounds: " + FormatRect(windowBounds));
            body.AppendLine("ApplicationView.VisibleBounds: " + FormatRect(visibleBounds));
            body.AppendLine("InputPane.OccludedRect: " + FormatRect(inputPaneOccludedRect));
            body.AppendLine("Window.Content: " + (windowContent == null ? "null" : windowContent.GetType().FullName));
            body.AppendLine("Frame.ActualWidth/ActualHeight: " + FormatRect(frameSize));
            body.AppendLine("Frame.LayoutSlot: " + FormatRect(frameSlot));
            body.AppendLine("Page.ActualWidth/ActualHeight: " + FormatRect(pageSize));
            body.AppendLine("Page.LayoutSlot: " + FormatRect(pageSlot));
            body.AppendLine("Page.BoundsInFrame: " + FormatRect(pageBounds));
            body.AppendLine("ShellRoot.ActualWidth/ActualHeight: " + FormatRect(rootSize));
            body.AppendLine(string.Format(CultureInfo.InvariantCulture,
                "ShellRoot.Margin: left={0:0.###}, top={1:0.###}, right={2:0.###}, bottom={3:0.###}",
                rootMargin.Left, rootMargin.Top, rootMargin.Right, rootMargin.Bottom));
            body.AppendLine("ShellRoot.LayoutSlot: " + FormatRect(rootSlot));
            body.AppendLine("ShellRoot.BoundsInPage: " + FormatRect(rootBounds));
            body.AppendLine("ShellRoot.ExpectedBoundsInPage: " + FormatRect(expectedRoot));
            body.AppendLine("BrowserChrome.ActualWidth/ActualHeight: " + FormatRect(chromeSize));
            body.AppendLine("BrowserChrome.BoundsInShellRoot: " + FormatRect(chromeBounds));
            body.AppendLine("BrowserChrome.ExpectedHeight: " + expectedChromeHeight.ToString("0.###", CultureInfo.InvariantCulture));
            body.AppendLine("LoadingProgress.Visibility: " + progressVisibility);
            body.AppendLine("LoadingProgress.ActualWidth/ActualHeight: " + FormatRect(progressSize));
            body.AppendLine("LoadingProgress.BoundsInShellRoot: " + FormatRect(progressBounds));
            body.AppendLine("AddressEditing: " + addressEditing);
            body.AppendLine("AddressBox.ActualWidth/ActualHeight: " + FormatRect(addressSize));
            body.AppendLine("AddressBox.BoundsInShellRoot: " + FormatRect(addressBounds));
            body.AppendLine("AddressBox.FocusState: " + addressFocusState);
            body.AppendLine("AddressBox.VerticalContentAlignment: " + addressVerticalContentAlignment);
            body.AppendLine(string.Format(CultureInfo.InvariantCulture,
                "AddressBox.Padding: left={0:0.###}, top={1:0.###}, right={2:0.###}, bottom={3:0.###}",
                addressPadding.Left, addressPadding.Top, addressPadding.Right, addressPadding.Bottom));
            body.AppendLine("AddressBox.FontSize: " + addressFontSize.ToString("0.###", CultureInfo.InvariantCulture));
            body.AppendLine("ReloadButton.Visibility: " + FormatVisibility(reloadButtonVisibility));
            body.AppendLine("StopButton.Visibility: " + FormatVisibility(stopButtonVisibility));
            body.AppendLine("MoreButton.Visibility: " + FormatVisibility(moreButtonVisibility));
            body.AppendLine("AddressDivider.Visibility: " + FormatVisibility(addressDividerVisibility));
            body.AppendLine("HelloActionButton.ActualWidth/ActualHeight: " + FormatRect(helloActionSize));
            body.AppendLine("HelloActionButton.BoundsInShellRoot: " + FormatRect(helloActionBounds));
            body.AppendLine("MenuTopDivider.BoundsInShellRoot: " + FormatRect(menuTopDividerBounds));
            body.AppendLine("MenuOpen: " + menuOpen);
            body.AppendLine("MenuSheet.Width: " + menuWidth.ToString("0.###", CultureInfo.InvariantCulture));
            body.AppendLine("MenuSheet.ActualWidth/ActualHeight: " + FormatRect(menuSize));
            body.AppendLine("MenuSheet.BoundsInShellRoot: " + FormatRect(menuBounds));
            body.AppendLine("MenuSheet.ExpectedWidth: " + (expectedMenuWidth.HasValue
                ? expectedMenuWidth.Value.ToString("0.###", CultureInfo.InvariantCulture)
                : "unavailable (ShellRoot dimensions must be finite and positive)"));
            body.AppendLine("Frame/Page/Root geometry ready: " + geometryReady);
            body.AppendLine();

            int failures = 0;
            AppendCheck(body, "01 Full-window Frame/Page sizes and slots under UseCoreWindow",
                geometryReady && boundsMode == ApplicationViewBoundsMode.UseCoreWindow
                && MatchesRect(frameSize, fullWindowSlot) && MatchesRect(pageSize, fullWindowSlot)
                && MatchesRect(frameSlot, fullWindowSlot) && MatchesRect(pageSlot, fullWindowSlot)
                && MatchesRect(pageBounds, fullWindowSlot), ref failures);
            AppendCheck(body, "02 ShellRoot dimensions equal the unoccluded visible region",
                geometryReady && rootBounds.HasValue && HasPositiveSize(rootBounds.Value)
                && Near(rootSize.Width, expectedRoot.Value.Width) && Near(rootSize.Height, expectedRoot.Value.Height)
                && Near(rootBounds.Value.Width, expectedRoot.Value.Width)
                && Near(rootBounds.Value.Height, expectedRoot.Value.Height), ref failures);
            AppendCheck(body, "03 ShellRoot origin equals the unoccluded visible region relative to Window",
                geometryReady && rootBounds.HasValue && HasPositiveSize(rootBounds.Value)
                && Near(rootBounds.Value.X, expectedRoot.Value.X) && Near(rootBounds.Value.Y, expectedRoot.Value.Y), ref failures);
            AppendCheck(body, string.Format(CultureInfo.InvariantCulture,
                "04 BrowserChrome height is {0:0.###} DIP", expectedChromeHeight),
                geometryReady && HasPositiveSize(chromeSize)
                && chromeBounds.HasValue && HasPositiveSize(chromeBounds.Value)
                && Near(chromeSize.Height, expectedChromeHeight)
                && Near(chromeBounds.Value.Height, expectedChromeHeight), ref failures);
            AppendCheck(body, "05 BrowserChrome abuts ShellRoot bottom and fits inside root",
                geometryReady && HasPositiveSize(chromeSize) && FitsWithinRoot(chromeBounds, rootSize)
                && Near(chromeBounds.Value.Bottom, rootSize.Height), ref failures);
            AppendCheck(body, "06 AddressBox has positive size and vertically centered content with symmetric padding",
                geometryReady && HasPositiveSize(addressSize) && FitsWithinRoot(addressBounds, rootSize)
                && addressVerticalContentAlignment == VerticalAlignment.Center
                && Near(addressPadding.Top - addressPadding.Bottom, 0.0), ref failures);
            bool reloadAndStopMatchEditing = addressEditing
                ? reloadButtonVisibility == Visibility.Collapsed && stopButtonVisibility == Visibility.Collapsed
                : (reloadButtonVisibility == Visibility.Visible) != (stopButtonVisibility == Visibility.Visible);
            AppendCheck(body, "07 Reload/Stop share the refresh slot and are hidden while editing",
                reloadAndStopMatchEditing, ref failures);
            bool progressAtAddressEdge = progressVisibility == Visibility.Collapsed
                ? progressSize.Height <= GeometryTolerance
                : progressVisibility == Visibility.Visible && progressBounds.HasValue
                    && HasPositiveSize(progressBounds.Value) && chromeBounds.HasValue
                    && HasPositiveSize(chromeBounds.Value)
                    && Near(progressBounds.Value.X, 0)
                    && Near(progressSize.Width, rootSize.Width)
                    && Near(progressSize.Height, LoadingProgressHeight)
                    && Near(progressBounds.Value.Bottom, chromeBounds.Value.Y);
            AppendCheck(body, "08 Progress line is hidden at idle or spans 2 DIP directly above the address bar",
                progressAtAddressEdge, ref failures);
            if (menuOpen)
            {
                AppendCheck(body, "09 MenuSheet actual width is full root width in portrait, half in landscape",
                    geometryReady && expectedMenuWidth.HasValue && HasPositiveSize(menuSize)
                    && Near(menuSize.Width, expectedMenuWidth.Value), ref failures);
                AppendCheck(body, "10 MenuSheet abuts ShellRoot right and BrowserChrome top and fits inside root",
                    geometryReady && HasPositiveSize(menuSize) && FitsWithinRoot(menuBounds, rootSize)
                    && HasPositiveSize(chromeSize) && FitsWithinRoot(chromeBounds, rootSize)
                    && Near(menuBounds.Value.Right, rootSize.Right)
                    && Near(menuBounds.Value.Bottom, chromeBounds.Value.Top), ref failures);
                AppendCheck(body, "11 Hello AppBarButton uses native 68x56 DIP sizing and touches the address bar",
                    helloActionButton != null && HasPositiveSize(helloActionSize)
                    && helloActionBounds.HasValue && HasPositiveSize(helloActionBounds.Value)
                    && Near(helloActionSize.Width, BrowserChromeGeometry.HelloActionWidth) && Near(helloActionSize.Height, BrowserChromeGeometry.ExpandedActionsHeight)
                    && chromeBounds.HasValue && HasPositiveSize(chromeBounds.Value)
                    && Near(helloActionBounds.Value.Right, rootSize.Width)
                    && Near(helloActionBounds.Value.Bottom, chromeBounds.Value.Y), ref failures);
                AppendCheck(body, "12 MenuTopDivider is 1 DIP across the sheet at its top edge",
                    menuTopDividerBounds.HasValue && HasPositiveSize(menuTopDividerBounds.Value)
                    && Near(menuTopDividerBounds.Value.X, menuBounds.Value.X)
                    && Near(menuTopDividerBounds.Value.Y, menuBounds.Value.Y)
                    && Near(menuTopDividerBounds.Value.Width, menuSize.Width)
                    && Near(menuTopDividerBounds.Value.Height, 1.0), ref failures);
            }

            if (addressEditing)
            {
                AppendCheck(body, "13 AddressBox spans ShellRoot with 4 DIP left/right margins while editing",
                    addressGeometryReady && Near(addressBounds.Value.X, AddressEditingMargin)
                    && Near(addressSize.Width, rootSize.Width - 2.0 * AddressEditingMargin)
                    && Near(addressBounds.Value.Width, rootSize.Width - 2.0 * AddressEditingMargin), ref failures);
                AppendCheck(body, string.Format(CultureInfo.InvariantCulture,
                    "14 Editing state: AddressBox focused with {0:0.###} DIP font, menu closed, compact controls collapsed",
                    AddressEditingFontSize),
                    addressGeometryReady && addressFocusState != FocusState.Unfocused && !menuOpen
                    && addressFontSize == AddressEditingFontSize
                    && Near(addressPadding.Top - addressPadding.Bottom, 0.0)
                    && reloadButtonVisibility == Visibility.Collapsed
                    && stopButtonVisibility == Visibility.Collapsed
                    && moreButtonVisibility == Visibility.Collapsed
                    && addressDividerVisibility == Visibility.Collapsed, ref failures);
            }

            // Construct the header after the final check so its failures are included.
            int checkCount = CheckCount + (menuOpen ? MenuCheckCount : 0)
                + (addressEditing ? AddressEditingCheckCount : 0);
            string header = string.Format(CultureInfo.InvariantCulture,
                "Version: {0}\nCapturedUtc: {1}\nResult: {2}; Checks: {3}; Failures: {4}\n",
                ReportVersion, DateTimeOffset.UtcNow.ToString("O", CultureInfo.InvariantCulture),
                failures == 0 ? "PASS" : "FAIL", checkCount, failures);
            return header + body.ToString();
        }

        /// <summary>Formats the latest snapshot and at most eleven older snapshots.</summary>
        /// <returns>The report text; ReportGate must be held by the caller.</returns>
        private static string CreateReport()
        {
            StringBuilder report = new StringBuilder();
            report.AppendLine(ReportVersion);
            report.AppendLine(string.Format(CultureInfo.InvariantCulture,
                "RetainedSnapshots: {0}; MaximumSnapshots: {1}; HistoryScope: current process, newest first",
                Snapshots.Count, MaximumSnapshots));
            for (int index = 0; index < Snapshots.Count; index++)
            {
                report.AppendLine();
                report.AppendLine(index == 0 ? "=== LAST SNAPSHOT ==="
                    : string.Format(CultureInfo.InvariantCulture, "=== HISTORY {0} ===", index));
                report.Append(Snapshots[index]);
            }
            return report.ToString();
        }

        /// <summary>Appends a PASS/FAIL result and counts every failed check.</summary>
        /// <param name="report">The snapshot body.</param>
        /// <param name="name">The numbered check description.</param>
        /// <param name="passed">Whether all required measurements satisfy the check.</param>
        /// <param name="failures">The accumulated failure count.</param>
        private static void AppendCheck(StringBuilder report, string name, bool passed, ref int failures)
        {
            report.AppendLine((passed ? "PASS: " : "FAIL: ") + name);
            if (!passed)
                failures++;
        }

        /// <summary>Derives the unoccluded root rectangle from the shell's pure margin helper.</summary>
        /// <param name="windowBounds">The full-window bounds.</param>
        /// <param name="visibleBounds">The system-visible bounds in the same coordinate space.</param>
        /// <param name="inputPaneOccludedRect">The input-pane occlusion in window-local coordinates.</param>
        /// <param name="systemBottomInset">The retained system bottom inset while Windows owns keyboard avoidance.</param>
        /// <returns>The finite, positive root bounds relative to the page, or null for invalid or empty geometry.</returns>
        private static Rect? GetExpectedRootBounds(Rect windowBounds, Rect visibleBounds, Rect inputPaneOccludedRect,
            double systemBottomInset)
        {
            if (!HasPositiveSize(windowBounds) || !HasPositiveSize(visibleBounds))
                return null;

            VisibleBoundsMargins margins = VisibleBoundsHelper.ComputeBrowserMargins(
                windowBounds.X, windowBounds.Y, windowBounds.Width, windowBounds.Height,
                visibleBounds.X, visibleBounds.Y, visibleBounds.Width, visibleBounds.Height,
                inputPaneOccludedRect.Height > 0, inputPaneOccludedRect.Y, systemBottomInset);
            double width = windowBounds.Width - margins.Left - margins.Right;
            double height = windowBounds.Height - margins.Top - margins.Bottom;
            if (!IsFinite(margins.Left) || !IsFinite(margins.Top)
                || !IsFinite(margins.Right) || !IsFinite(margins.Bottom)
                || !IsFinite(width) || !IsFinite(height) || width <= 0 || height <= 0)
                return null;

            return new Rect(margins.Left, margins.Top, width, height);
        }

        /// <summary>Formats visibility metadata without treating a missing element as collapsed.</summary>
        /// <param name="visibility">The visibility of a named element, if available.</param>
        /// <returns>The visibility value, or an explicit unavailable marker.</returns>
        private static string FormatVisibility(Visibility? visibility)
        {
            return visibility.HasValue ? visibility.Value.ToString() : "unavailable (named element not found)";
        }

        /// <summary>
        /// Obtains the transformed rectangle without replacing a failed transform
        /// with zero geometry. A missing rectangle fails every dependent check.
        /// </summary>
        /// <param name="element">The element whose bounds are measured.</param>
        /// <param name="relativeTo">The target coordinate space.</param>
        /// <param name="operation">A geometry-only description for the debug log.</param>
        /// <returns>The transformed bounds, or null after logging a transform error.</returns>
        private static Rect? GetTransformedBounds(FrameworkElement element, UIElement relativeTo, string operation)
        {
            try
            {
                return element.TransformToVisual(relativeTo).TransformBounds(
                    new Rect(0, 0, element.ActualWidth, element.ActualHeight));
            }
            catch (Exception exception)
            {
                LogError(operation, exception);
                return null;
            }
        }

        /// <summary>Rejects nonfinite coordinates or edges and zero, negative, or nonfinite dimensions.</summary>
        /// <param name="bounds">The rectangle to validate.</param>
        /// <returns>True only when the rectangle is finite and both dimensions are positive.</returns>
        private static bool HasPositiveSize(Rect bounds)
        {
            return IsFinite(bounds.X) && IsFinite(bounds.Y)
                && bounds.Width > 0 && bounds.Height > 0
                && IsFinite(bounds.Width) && IsFinite(bounds.Height)
                && IsFinite(bounds.Right) && IsFinite(bounds.Bottom);
        }

        /// <summary>Checks whether a coordinate or dimension is finite.</summary>
        /// <param name="value">The coordinate or dimension to validate.</param>
        /// <returns>True when the value is neither NaN nor infinite.</returns>
        private static bool IsFinite(double value)
        {
            return !double.IsNaN(value) && !double.IsInfinity(value);
        }

        /// <summary>Compares finite geometry values within one logical pixel.</summary>
        /// <param name="actual">The measured coordinate or dimension.</param>
        /// <param name="expected">The required coordinate or dimension.</param>
        /// <returns>True when the absolute difference is within the tolerance.</returns>
        private static bool Near(double actual, double expected)
        {
            return Math.Abs(actual - expected) <= GeometryTolerance;
        }

        /// <summary>Compares positive rectangles, including their origins.</summary>
        /// <param name="actual">The measured rectangle, if available.</param>
        /// <param name="expected">The required rectangle.</param>
        /// <returns>True when every component matches within the tolerance.</returns>
        private static bool MatchesRect(Rect? actual, Rect expected)
        {
            return actual.HasValue && HasPositiveSize(actual.Value) && HasPositiveSize(expected)
                && Near(actual.Value.X, expected.X) && Near(actual.Value.Y, expected.Y)
                && Near(actual.Value.Width, expected.Width) && Near(actual.Value.Height, expected.Height);
        }

        /// <summary>Checks positive element bounds against the root's local rectangle.</summary>
        /// <param name="bounds">Element bounds in ShellRoot coordinates, if available.</param>
        /// <param name="rootSize">The root rectangle starting at the local origin.</param>
        /// <returns>True when all four edges lie within the root, allowing the tolerance.</returns>
        private static bool FitsWithinRoot(Rect? bounds, Rect rootSize)
        {
            return bounds.HasValue && HasPositiveSize(bounds.Value) && HasPositiveSize(rootSize)
                && bounds.Value.X >= -GeometryTolerance && bounds.Value.Y >= -GeometryTolerance
                && bounds.Value.Right <= rootSize.Width + GeometryTolerance
                && bounds.Value.Bottom <= rootSize.Height + GeometryTolerance;
        }

        /// <summary>Formats a rectangle without culture-dependent decimal separators.</summary>
        /// <param name="bounds">The measured rectangle, if available.</param>
        /// <returns>The four geometry values, or an explicit unavailable marker.</returns>
        private static string FormatRect(Rect? bounds)
        {
            if (!bounds.HasValue)
                return "unavailable (dependent checks fail)";

            Rect value = bounds.Value;
            return string.Format(CultureInfo.InvariantCulture,
                "x={0:0.###}, y={1:0.###}, width={2:0.###}, height={3:0.###}",
                value.X, value.Y, value.Width, value.Height);
        }

        /// <summary>Decodes the OS version reported by the phone-era AnalyticsInfo API.</summary>
        /// <param name="encodedVersion">The unsigned, packed OS version string.</param>
        /// <returns>The four-part OS version, or the original OS metadata if unparseable.</returns>
        private static string FormatOsVersion(string encodedVersion)
        {
            ulong version;
            if (!ulong.TryParse(encodedVersion, NumberStyles.None, CultureInfo.InvariantCulture, out version))
                return encodedVersion;

            return string.Format(CultureInfo.InvariantCulture, "{0}.{1}.{2}.{3}",
                (version >> 48) & 0xffff, (version >> 32) & 0xffff,
                (version >> 16) & 0xffff, version & 0xffff);
        }

        /// <summary>Logs the original HResult and error type without content or exception messages.</summary>
        /// <param name="operation">The diagnostic operation that failed.</param>
        /// <param name="exception">The original exception, whose HResult is preserved.</param>
        private static void LogError(string operation, Exception exception)
        {
            Debug.WriteLine(string.Format(CultureInfo.InvariantCulture,
                "{0}: {1} failed; {2}; HResult=0x{3:X8}",
                ReportVersion, operation, exception.GetType().FullName, exception.HResult));
        }
    }
}
#endif
