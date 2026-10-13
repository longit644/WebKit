using System;
using System.ComponentModel;
using System.IO;
using System.Diagnostics;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.System;
using Windows.UI.Core;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media.Animation;
using WebKitMiniBrowser.DependencyInjection;
using WebKitMiniBrowser.ViewModels;
using WebKitMiniBrowser.Views.Controls;

namespace WebKitMiniBrowser
{
    /// <summary>
    /// MiniBrowser page that binds its toolbar and status surface to a
    /// <see cref="BrowserViewModel"/> while keeping content inside visible bounds.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        /// <summary>The app view supplying logical visible-bounds changes.</summary>
        private readonly ApplicationView applicationView;

        /// <summary>The phone's system Back event source.</summary>
        private readonly SystemNavigationManager navigationManager;

        /// <summary>The keyboard event source for app-owned input-pane avoidance.</summary>
        private readonly InputPane inputPane;

        /// <summary>Render-only address-bar movement after keyboard/system-bar safe-area layout.</summary>
        private readonly ChromeLayoutTransition addressBarTransition;

        /// <summary>The most recently announced keyboard rectangle, in window-local logical pixels.</summary>
        private Windows.Foundation.Rect inputPaneOccludedRect;

        /// <summary>Last bottom system inset observed independently of docked keyboard occlusion.</summary>
        private double browserSystemBottomInset;

        /// <summary>Whether external layout and Back event sources are currently subscribed.</summary>
        private bool subscriptionsActive;

        /// <summary>Name of the candidate interaction report in app LocalState.</summary>
        private const string HostHtmlCandidateReportFileName = "hosthtml-candidate.txt";

        /// <summary>The active view-owned menu transition, if any.</summary>
        private Storyboard menuAnimation;

        /// <summary>Monotonic transition identity rejecting callbacks from interrupted slides.</summary>
        private int menuTransitionId;

        /// <summary>Owns reveal/morph/focus and reverse closing choreography for address editing.</summary>
        private AddressEditingTransition addressEditing;

        /// <summary>The MVVM command/state source used by the page and its app-bar panel.</summary>
        public BrowserViewModel ViewModel { get; private set; }

        /// <summary>Gets the address control's compact browser chrome grid.</summary>
        private Grid BrowserChrome { get { return AddressBar.BrowserChrome; } }

        /// <summary>Gets the address control's editable address field.</summary>
        private TextBox AddressBox { get { return AddressBar.AddressBox; } }

        /// <summary>Gets the address control's overflow toggle button.</summary>
        private Button MoreButton { get { return AddressBar.MoreButton; } }

        /// <summary>Gets the app-bar overlay shown while dismissing the panel.</summary>
        private Grid MenuOverlay { get { return BrowserAppBar.MenuOverlay; } }

        /// <summary>Gets the address-editing scrim owned by the app-bar surface.</summary>
        private Button AddressEditingOverlay { get { return BrowserAppBar.AddressEditingOverlay; } }

        /// <summary>Gets the app-bar menu sheet.</summary>
        private Border MenuSheet { get { return BrowserAppBar.MenuSheet; } }

        /// <summary>Gets the secondary app-bar actions scroller.</summary>
        private ScrollViewer MenuScroll { get { return BrowserAppBar.MenuScroll; } }

        /// <summary>Gets the app-bar menu sheet translation transform.</summary>
        private Windows.UI.Xaml.Media.TranslateTransform MenuSheetTranslation { get { return BrowserAppBar.MenuSheetTranslation; } }

        /// <summary>Gets the clip geometry for the app-bar overlay.</summary>
        private Windows.UI.Xaml.Media.RectangleGeometry MenuOverlayClip { get { return BrowserAppBar.MenuOverlayClip; } }

        /// <summary>Gets the Forward item for app-bar focus management.</summary>
        private Button ForwardMenuButton { get { return BrowserAppBar.ForwardMenuButton; } }

        /// <summary>Gets the Local demo item for app-bar focus management.</summary>
        private Button DemoMenuButton { get { return BrowserAppBar.DemoMenuButton; } }

        /// <summary>Gets the primary Hello action host for responsive sizing.</summary>
        private Grid HelloActionHost { get { return BrowserAppBar.HelloActionHost; } }

        /// <summary>Gets the primary Hello action for responsive sizing.</summary>
        private AppBarButton HelloActionButton { get { return BrowserAppBar.HelloActionButton; } }

        /// <summary>Gets the menu's top-edge divider for optional diagnostics.</summary>
        private Border MenuTopDivider { get { return BrowserAppBar.MenuTopDivider; } }

#if LAYOUT_DIAGNOSTICS
        /// <summary>Coalesces optional layout-report writes until the previous snapshot completes.</summary>
        private bool layoutReportQueued;
        /// <summary>Requests a fresh snapshot when layout changes during an outstanding write.</summary>
        private bool layoutReportPending;
#endif

#if HOSTHTML_SMOKE
        /// <summary>Whether the candidate's one-time automatic HostHtml command has been issued.</summary>
        private bool hostHtmlSmokeSubmitted;
        /// <summary>Whether candidate-only event/state diagnostics are subscribed.</summary>
        private bool hostHtmlSmokeSubscribed;
#endif

        /// <summary>
        /// Initializes compiled XAML, attaches the public browser control to its
        /// view model, and configures visible-bounds layout.
        /// </summary>
        public MainPage()
        {
            InitializeComponent();
            addressBarTransition = new ChromeLayoutTransition(AddressBar, this);
            string packagePath = Package.Current.InstalledLocation.Path;
            string overridePath = Path.Combine(ApplicationData.Current.LocalFolder.Path, "blocked-override.html");
            MiniBrowserServiceLocator.ConfigureBrowserPage(
                Browser,
                Path.Combine(packagePath, "Assets", "blocked.html"),
                overridePath,
                Path.Combine(packagePath, "Assets", "demo.html"));
            ViewModel = MiniBrowserServiceLocator.Get<BrowserViewModel>();
            DataContext = ViewModel;
            AddressBar.AddressBox.KeyDown += OnAddressKeyDown;
            AddressBar.AddressBox.GotFocus += OnAddressGotFocus;
            AddressBar.AddressBox.LostFocus += OnAddressLostFocus;

            applicationView = ApplicationView.GetForCurrentView();
            navigationManager = SystemNavigationManager.GetForCurrentView();
            inputPane = InputPane.GetForCurrentView();
            addressEditing = new AddressEditingTransition(
                AddressBar,
                AddressEditingOverlay,
                inputPane,
                () => subscriptionsActive,
                () => inputPaneOccludedRect.Height > 0,
                () => Focus(FocusState.Programmatic));
            Loaded += OnLoaded;
            Unloaded += OnUnloaded;
            SizeChanged += OnSizeChanged;
            ApplyVisibleBounds();
        }

        /// <summary>
        /// Reapplies visible-bounds layout when system bars or orientation change.
        /// </summary>
        /// <param name="sender">The application view.</param>
        /// <param name="args">Change details.</param>
        private void OnVisibleBoundsChanged(ApplicationView sender, object args)
        {
            addressBarTransition.MotionEnabled = inputPaneOccludedRect.Height <= 0
                && !addressEditing.KeyboardMotionInProgress;
            ApplyVisibleBounds(animateSystemChange: true);
        }

        /// <summary>Reapplies safe-area margins when the OS resumes the application.</summary>
        /// <param name="sender">The current application.</param>
        /// <param name="args">The resume notification.</param>
        private void OnResuming(object sender, object args)
        {
            ApplyVisibleBounds();
        }

        /// <summary>Applies safe-area margins after the page first receives its layout slot.</summary>
        /// <param name="sender">The loaded page.</param>
        /// <param name="args">The routed Loaded event.</param>
        private void OnLoaded(object sender, RoutedEventArgs args)
        {
            addressBarTransition.Reset();
            addressEditing.Reset();
            addressEditing.PhaseChanged += OnAddressEditingPhaseChanged;
            if (!subscriptionsActive)
            {
                applicationView.VisibleBoundsChanged += OnVisibleBoundsChanged;
                navigationManager.BackRequested += OnBackRequested;
                inputPane.Showing += OnInputPaneShowing;
                inputPane.Hiding += OnInputPaneHiding;
                Application.Current.Resuming += OnResuming;
                ViewModel.PropertyChanged += OnViewModelPropertyChanged;
#if HOSTHTML_SMOKE
                Browser.SizeChanged += OnHostHtmlViewportChanged;
                Window.Current.VisibilityChanged += OnHostHtmlWindowVisibilityChanged;
                Window.Current.Activated += OnHostHtmlWindowActivated;
#endif
                subscriptionsActive = true;
            }

            inputPaneOccludedRect = inputPane.OccludedRect;
            ApplyAddressEditing();
            ApplyVisibleBounds();
#if HOSTHTML_SMOKE
            if (!hostHtmlSmokeSubscribed)
            {
                var version = Package.Current.Id.Version;
                string reportPath = Path.Combine(ApplicationData.Current.LocalFolder.Path, HostHtmlCandidateReportFileName);
                File.WriteAllText(reportPath, "package=" + version.Major + "." + version.Minor + "."
                    + version.Build + "." + version.Revision + Environment.NewLine);
                AppendHostHtmlSmoke("package", "installed=" + Package.Current.InstalledLocation.Path);
                Browser.NavigationStarting += OnHostHtmlStarting;
                Browser.NavigationCompleted += OnHostHtmlCompleted;
                ViewModel.PropertyChanged += OnHostHtmlStateChanged;
                hostHtmlSmokeSubscribed = true;
                WriteHostHtmlSmokeSnapshot("startup");
            }
            if (!hostHtmlSmokeSubmitted)
            {
                hostHtmlSmokeSubmitted = true;
                Browser.EnsureInitializedAsync().Completed += OnHostHtmlSmokeInitialized;
            }
#endif
        }

        /// <summary>Revokes external view subscriptions without closing a temporarily unloaded browser.</summary>
        /// <param name="sender">The unloaded page.</param>
        /// <param name="args">The routed Unloaded event.</param>
        private void OnUnloaded(object sender, RoutedEventArgs args)
        {
            addressBarTransition.Reset();
            addressEditing.PhaseChanged -= OnAddressEditingPhaseChanged;
            ViewModel.CancelAddressEditingCommand.Execute(null);
            addressEditing.Reset();
            if (subscriptionsActive)
            {
                applicationView.VisibleBoundsChanged -= OnVisibleBoundsChanged;
                navigationManager.BackRequested -= OnBackRequested;
                inputPane.Showing -= OnInputPaneShowing;
                inputPane.Hiding -= OnInputPaneHiding;
                Application.Current.Resuming -= OnResuming;
                ViewModel.PropertyChanged -= OnViewModelPropertyChanged;
#if HOSTHTML_SMOKE
                Browser.SizeChanged -= OnHostHtmlViewportChanged;
                Window.Current.VisibilityChanged -= OnHostHtmlWindowVisibilityChanged;
                Window.Current.Activated -= OnHostHtmlWindowActivated;
                Browser.NavigationStarting -= OnHostHtmlStarting;
                Browser.NavigationCompleted -= OnHostHtmlCompleted;
                ViewModel.PropertyChanged -= OnHostHtmlStateChanged;
                hostHtmlSmokeSubscribed = false;
#endif
                subscriptionsActive = false;
            }

            CollapseMenuImmediately();
            ViewModel.CloseMenuCommand.Execute(null);
        }

#if HOSTHTML_SMOKE
        /// <summary>Records public-control readiness before issuing the local demo command.</summary>
        private void OnHostHtmlSmokeInitialized(Windows.Foundation.IAsyncAction operation, Windows.Foundation.AsyncStatus status)
        {
            try
            {
                if (status != Windows.Foundation.AsyncStatus.Completed)
                {
                    var error = operation.ErrorCode;
                    AppendHostHtmlSmoke("initialized", "status=" + status + ";error=0x"
                        + (error == null ? "00000000" : error.HResult.ToString("X8"))
                        + ";detail=" + (error == null ? string.Empty : error.Message));
                    return;
                }
                operation.GetResults();
                AppendHostHtmlSmoke("initialized", "result=S_OK");
                ViewModel.LoadDemoCommand.Execute(null);
            }
            catch (Exception exception)
            {
                string detail = exception.ToString().Replace('\r', ' ').Replace('\n', ' ');
                AppendHostHtmlSmoke("initialized", "result=0x" + exception.HResult.ToString("X8") + ";detail=" + detail);
            }
        }

        /// <summary>Records the actual frozen-table Starting callback in app LocalState.</summary>
        private void OnHostHtmlStarting(WebKit.WebKitWebView sender, WebKit.WebKitNavigationStartingEventArgs args)
        {
            AppendHostHtmlSmoke("starting", "id=" + args.NavigationId + ";kind=" + args.ContentKind + ";uri=" + args.Uri);
        }

        /// <summary>Records the actual frozen-table terminal and its status/history snapshot.</summary>
        private void OnHostHtmlCompleted(WebKit.WebKitWebView sender, WebKit.WebKitNavigationCompletedEventArgs args)
        {
            AppendHostHtmlSmoke("completed", "id=" + args.NavigationId + ";kind=" + args.ContentKind
                + ";outcome=" + args.Outcome + ";http=" + args.HasHttpStatus + ";status=" + args.HttpStatus);
            WriteHostHtmlSmokeSnapshot("terminal");
        }

        /// <summary>Captures public observable state changes caused by the demo navigation.</summary>
        private void OnHostHtmlStateChanged(object sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == "Source" || args.PropertyName == "CanGoBack"
                || args.PropertyName == "CanGoForward" || args.PropertyName == "IsLoading"
                || args.PropertyName == "DocumentTitle" || args.PropertyName == "Status")
                WriteHostHtmlSmokeSnapshot(args.PropertyName);
        }

        /// <summary>Appends a value-only candidate diagnostic line; no remote URL or credential is recorded.</summary>
        private void AppendHostHtmlSmoke(string phase, string detail)
        {
            string path = Path.Combine(ApplicationData.Current.LocalFolder.Path, HostHtmlCandidateReportFileName);
            File.AppendAllText(path, phase + ": " + detail + Environment.NewLine);
        }

        /// <summary>Records the local demo's public Source/history/loading/title state.</summary>
        private void WriteHostHtmlSmokeSnapshot(string phase)
        {
            AppendHostHtmlSmoke(phase, "source=" + ViewModel.Address + ";back=" + ViewModel.CanGoBack
                + ";forward=" + ViewModel.CanGoForward + ";loading=" + ViewModel.IsLoading
                + ";title=" + ViewModel.DocumentTitle + ";status=" + ViewModel.Status);
        }

        /// <summary>Records only focus/mode/geometry facts, without typed address content.</summary>
        private void WriteKeyboardTransition(string phase)
        {
            AppendHostHtmlSmoke("ux", "phase=" + phase + ";time=" + DateTime.UtcNow.ToString("O")
                + ";editing=" + ViewModel.IsAddressEditing + ";menu=" + ViewModel.IsMenuOpen
                + ";visual-editing=" + AddressBar.IsAddressEditing
                + ";edit-layer=" + AddressEditingOverlay.Visibility + ";layer-opacity=" + AddressEditingOverlay.Opacity
                + ";focus=" + AddressBox.FocusState + ";web=" + Browser.ActualWidth + "x" + Browser.ActualHeight
                + ";chrome=" + ShellRoot.ActualWidth + "x" + ShellRoot.ActualHeight
                + ";keyboard=" + inputPaneOccludedRect.Height + AddressBar.GetColorDiagnostic());
        }

        /// <summary>Correlates actual web viewport changes with focus and keyboard events.</summary>
        private void OnHostHtmlViewportChanged(object sender, SizeChangedEventArgs args)
        {
            WriteKeyboardTransition("web-size");
        }

        /// <summary>Correlates window exposure with system panels without forcing a redraw from the shell.</summary>
        private void OnHostHtmlWindowVisibilityChanged(object sender, Windows.UI.Core.VisibilityChangedEventArgs args)
        {
            WriteKeyboardTransition("window-visible-" + args.Visible);
        }

        /// <summary>Records Action Center activation transitions independently of viewport geometry.</summary>
        private void OnHostHtmlWindowActivated(object sender, Windows.UI.Core.WindowActivatedEventArgs args)
        {
            WriteKeyboardTransition("window-activation-" + args.WindowActivationState);
        }
#endif

        /// <summary>Dismisses address editing or actions before traversing engine history.</summary>
        /// <param name="sender">The system navigation manager.</param>
        /// <param name="args">The system Back request and handled flag.</param>
        private void OnBackRequested(object sender, BackRequestedEventArgs args)
        {
            if (ViewModel.IsAddressEditing)
            {
                args.Handled = true;
                ViewModel.CancelAddressEditingCommand.Execute(null);
            }
            else if (ViewModel.IsMenuOpen)
            {
                args.Handled = true;
                ViewModel.CloseMenuCommand.Execute(null);
            }
            else if (ViewModel.BackCommand.CanExecute(null))
            {
                args.Handled = true;
                ViewModel.BackCommand.Execute(null);
            }
        }

        /// <summary>Submits the address through the ViewModel's navigation command.</summary>
        /// <param name="sender">The address input.</param>
        /// <param name="args">The input key and handled flag.</param>
        private void OnAddressKeyDown(object sender, Windows.UI.Xaml.Input.KeyRoutedEventArgs args)
        {
            if (args.Key == VirtualKey.Enter && ViewModel.NavigateCommand.CanExecute(null))
            {
                args.Handled = true;
                ViewModel.NavigateCommand.Execute(null);
            }
        }

        /// <summary>Selects the current address for replacement when the user begins editing.</summary>
        /// <param name="sender">The address input.</param>
        /// <param name="args">The focus change event.</param>
        private void OnAddressGotFocus(object sender, RoutedEventArgs args)
        {
            ViewModel.BeginAddressEditingCommand.Execute(null);
            AddressBox.SelectAll();
#if HOSTHTML_SMOKE
            WriteKeyboardTransition("address-focus");
#endif
        }

        /// <summary>Returns to compact chrome when focus leaves the address editor.</summary>
        /// <param name="sender">The address input.</param>
        /// <param name="args">The routed focus change.</param>
        private void OnAddressLostFocus(object sender, RoutedEventArgs args)
        {
            if (AddressBox.FocusState == FocusState.Unfocused)
            {
                ViewModel.CancelAddressEditingCommand.Execute(null);
            }
#if HOSTHTML_SMOKE
            WriteKeyboardTransition("address-blur");
#endif
        }

        /// <summary>Dismisses the active address edit or app-bar panel without changing history.</summary>
        /// <param name="sender">The shell page.</param>
        /// <param name="args">The keyboard event and handled flag.</param>
        private void OnPageKeyDown(object sender, Windows.UI.Xaml.Input.KeyRoutedEventArgs args)
        {
            if (args.Key != VirtualKey.Escape)
            {
                return;
            }

            if (ViewModel.IsAddressEditing)
            {
                args.Handled = true;
                ViewModel.CancelAddressEditingCommand.Execute(null);
            }
            else if (ViewModel.IsMenuOpen)
            {
                args.Handled = true;
                ViewModel.CloseMenuCommand.Execute(null);
            }
        }

        /// <summary>Sizes the panel against the visible root after system bars or rotation change.</summary>
        /// <param name="sender">The visible shell root.</param>
        /// <param name="args">The newly arranged root dimensions.</param>
        private void OnShellRootSizeChanged(object sender, SizeChangedEventArgs args)
        {
            UpdateMenuDimensions();
#if LAYOUT_DIAGNOSTICS
            QueueLayoutReport();
#endif
        }

        /// <summary>Applies full portrait width or half landscape width without resizing browser content.</summary>
        private void UpdateMenuDimensions()
        {
            double width = ShellRoot.ActualWidth;
            double height = ShellRoot.ActualHeight;
            if (width <= 0 || height <= 0)
            {
                return;
            }

            double menuWidth = width > height ? width / 2 : width;
            double availableHeight = Math.Max(0, height - BrowserChromeGeometry.ChromeHeight);
            double panelInsets = MenuSheet.Padding.Top + MenuSheet.Padding.Bottom
                + MenuSheet.BorderThickness.Top + MenuSheet.BorderThickness.Bottom;
            if (MenuSheet.Width != menuWidth)
            {
                MenuSheet.Width = menuWidth;
            }

            MenuSheet.MaxHeight = availableHeight;
            MenuScroll.MaxHeight = Math.Max(0, availableHeight - panelInsets - BrowserChromeGeometry.ExpandedActionsHeight);
            double primaryWidth = Math.Min(BrowserChromeGeometry.HelloActionWidth, menuWidth);
            HelloActionHost.Width = primaryWidth;
            HelloActionButton.Width = primaryWidth;
            MenuOverlayClip.Rect = new Windows.Foundation.Rect(0, 0, width, height);
        }

        /// <summary>Reflects MVVM expansion state as a view-only animation.</summary>
        /// <param name="sender">The command/state ViewModel.</param>
        /// <param name="args">The changed property name.</param>
        private void OnViewModelPropertyChanged(object sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == "IsMenuOpen")
            {
                AnimateMenu(ViewModel.IsMenuOpen);
#if HOSTHTML_SMOKE
                WriteKeyboardTransition("menu-state");
#endif
            }
            else if (args.PropertyName == "IsAddressEditing")
            {
                ApplyAddressEditing();
#if HOSTHTML_SMOKE
                WriteKeyboardTransition("editing-state");
#endif
            }
        }

        /// <summary>Delegates reveal/morph/focus choreography while ViewModel owns logical editing state.</summary>
        private void ApplyAddressEditing()
        {
            if (ViewModel.IsAddressEditing)
                CollapseMenuImmediately();
            addressEditing.Request(ViewModel.IsAddressEditing);

#if LAYOUT_DIAGNOSTICS
            QueueLayoutReport();
#endif
        }

        /// <summary>Reports view-only edit phases for candidate diagnostics.</summary>
        /// <param name="phase">Current transition phase.</param>
        private void OnAddressEditingPhaseChanged(AddressEditingPhase phase)
        {
#if HOSTHTML_SMOKE
            WriteKeyboardTransition("edit-phase-" + phase);
#else
            Debug.WriteLine("Address edit phase: " + phase);
#endif
        }

        /// <summary>Stops obsolete menu transitions so they cannot steal focus from address editing.</summary>
        private void CollapseMenuImmediately()
        {
            ++menuTransitionId;
            if (menuAnimation != null)
            {
                menuAnimation.Stop();
                menuAnimation = null;
            }

            MenuSheetTranslation.Y = 0;
            MenuOverlay.Visibility = Visibility.Collapsed;
        }

        /// <summary>Slides the app-bar panel from the bottom and rejects obsolete transition completions.</summary>
        /// <param name="open">Whether to expand or collapse the panel.</param>
        private void AnimateMenu(bool open)
        {
            bool wasVisible = MenuOverlay.Visibility == Visibility.Visible;
            double currentOffset = MenuSheetTranslation.Y;
            int transitionId = ++menuTransitionId;
            if (menuAnimation != null)
            {
                menuAnimation.Stop();
                menuAnimation = null;
            }

            if (!open && !wasVisible)
            {
                return;
            }

            if (open)
            {
                inputPane.TryHide();
                MenuOverlay.Visibility = Visibility.Visible;
            }

            UpdateMenuDimensions();
            UpdateLayout();
            double panelHeight = MenuSheet.ActualHeight;
            double startOffset = wasVisible ? currentOffset : panelHeight;
            MenuSheetTranslation.Y = startOffset;
            Storyboard transition = new Storyboard();
            DoubleAnimation slide = new DoubleAnimation
            {
                From = startOffset,
                To = open ? 0 : panelHeight,
                Duration = new Duration(TimeSpan.FromMilliseconds(BrowserChromeTiming.MenuSlideMilliseconds)),
                EasingFunction = new CubicEase { EasingMode = open ? EasingMode.EaseOut : EasingMode.EaseIn }
            };
            Storyboard.SetTarget(slide, MenuSheetTranslation);
            Storyboard.SetTargetProperty(slide, "Y");
            transition.Children.Add(slide);
            transition.Completed += (sender, args) =>
            {
                if (transitionId != menuTransitionId)
                {
                    return;
                }

                transition.Stop();
                menuAnimation = null;
                MenuSheetTranslation.Y = 0;
                if (open)
                {
                    if (ForwardMenuButton.IsEnabled)
                    {
                        ForwardMenuButton.Focus(FocusState.Programmatic);
                    }
                    else
                    {
                        DemoMenuButton.Focus(FocusState.Programmatic);
                    }
                }
                else
                {
                    // Steer focus out of the collapsing sheet before hiding it so
                    // focus recovery cannot land in the address box and flash the
                    // touch keyboard.
                    if (!ViewModel.IsAddressEditing)
                    {
                        MoreButton.Focus(FocusState.Programmatic);
                    }

                    MenuOverlay.Visibility = Visibility.Collapsed;
                }
#if LAYOUT_DIAGNOSTICS
                QueueLayoutReport();
#endif
            };
            menuAnimation = transition;
            transition.Begin();
        }

        /// <summary>Refreshes safe-area margins when rotation or keyboard layout resizes the page.</summary>
        /// <param name="sender">The resized page.</param>
        /// <param name="args">The newly allocated size.</param>
        private void OnSizeChanged(object sender, SizeChangedEventArgs args)
        {
            ApplyVisibleBounds();
        }

        /// <summary>Lets XAML/Windows own focused-input avoidance and the keyboard's native movement.</summary>
        /// <param name="sender">The app's input pane.</param>
        /// <param name="args">The keyboard rectangle and focus-visibility contract.</param>
        private void OnInputPaneShowing(InputPane sender, InputPaneVisibilityEventArgs args)
        {
            addressBarTransition.MotionEnabled = false;
            addressBarTransition.Reset();
            inputPaneOccludedRect = args.OccludedRect;
            ApplyVisibleBounds();
            args.EnsuredFocusedElementInView = false;
#if HOSTHTML_SMOKE
            WriteKeyboardTransition("keyboard-show");
#endif
        }

        /// <summary>Restores the system-bar safe area after the docked keyboard is hidden.</summary>
        /// <param name="sender">The app's input pane.</param>
        /// <param name="args">The input-pane transition and focus-visibility contract.</param>
        private void OnInputPaneHiding(InputPane sender, InputPaneVisibilityEventArgs args)
        {
            addressBarTransition.MotionEnabled = false;
            addressBarTransition.Reset();
            addressEditing.KeyboardHiding();
            inputPaneOccludedRect = new Windows.Foundation.Rect();
            ViewModel.CancelAddressEditingCommand.Execute(null);
            if (!ViewModel.IsAddressEditing)
                addressEditing.Request(false);
            ApplyVisibleBounds();
            args.EnsuredFocusedElementInView = false;
#if HOSTHTML_SMOKE
            WriteKeyboardTransition("keyboard-hide");
#endif
        }

        /// <summary>
        /// Computes and applies the margins that keep shell content within the
        /// current visible bounds.
        /// </summary>
        /// <param name="animateSystemChange">Whether a changed system inset starts a shared motion deadline.</param>
        private void ApplyVisibleBounds(bool animateSystemChange = false)
        {
            Windows.Foundation.Rect windowBounds = Window.Current.Bounds;
            Windows.Foundation.Rect visibleBounds = applicationView.VisibleBounds;
            VisibleBoundsMargins margins = VisibleBoundsHelper.ComputeBrowserMargins(
                windowBounds.X, windowBounds.Y, windowBounds.Width, windowBounds.Height,
                visibleBounds.X, visibleBounds.Y, visibleBounds.Width, visibleBounds.Height,
                inputPaneOccludedRect.Height > 0, inputPaneOccludedRect.Y, browserSystemBottomInset);
            Thickness margin = new Thickness(margins.Left, margins.Top, margins.Right, margins.Bottom);
            if (!ShellRoot.Margin.Equals(margin))
            {
                if (subscriptionsActive && animateSystemChange && !addressBarTransition.IsTransitionInProgress)
                    addressBarTransition.PrepareTransition();
                ShellRoot.Margin = margin;
            }
            if (inputPaneOccludedRect.Height <= 0
                || windowBounds.Height - margins.Bottom > inputPaneOccludedRect.Y + 1)
                browserSystemBottomInset = margins.Bottom;
            Thickness browserMargin = new Thickness(margins.Left, margins.Top,
                margins.Right, margins.Bottom);
            if (!WebLayoutRoot.Margin.Equals(browserMargin))
                WebLayoutRoot.Margin = browserMargin;
            UpdateMenuDimensions();
#if LAYOUT_DIAGNOSTICS
            QueueLayoutReport();
#endif
        }

#if LAYOUT_DIAGNOSTICS
        /// <summary>Schedules a single optional snapshot after pending XAML layout work has completed.</summary>
        private void QueueLayoutReport()
        {
            if (!subscriptionsActive || menuAnimation != null)
            {
                return;
            }

            if (layoutReportQueued)
            {
                layoutReportPending = true;
                return;
            }

            layoutReportQueued = true;
            Dispatcher.RunAsync(CoreDispatcherPriority.Low, WriteLayoutReport);
        }

        /// <summary>Persists geometry-only device measurements without interrupting ordinary UI actions.</summary>
        private async void WriteLayoutReport()
        {
            try
            {
                if (!subscriptionsActive || menuAnimation != null)
                {
                    return;
                }

                UpdateLayout();
                await LayoutDiagnostics.CaptureAsync(this, ShellRoot, BrowserChrome, LoadingProgress, AddressBox,
                    AddressBar, BrowserAppBar, MenuSheet, ViewModel.IsMenuOpen, ViewModel.IsAddressEditing);
            }
            catch (System.Exception exception)
            {
                Debug.WriteLine("Layout report failed: HRESULT=0x{0:X8}", exception.HResult);
            }
            finally
            {
                layoutReportQueued = false;
                if (layoutReportPending)
                {
                    layoutReportPending = false;
                    QueueLayoutReport();
                }
            }
        }
#endif
    }
}
