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

        /// <summary>The most recently announced keyboard rectangle, in window-local logical pixels.</summary>
        private Windows.Foundation.Rect inputPaneOccludedRect;

        /// <summary>Whether external layout and Back event sources are currently subscribed.</summary>
        private bool subscriptionsActive;

        /// <summary>Height of the shared bottom address/app-bar surface, in logical pixels.</summary>
        private const double BrowserChromeHeight = 48;

        /// <summary>Height reserved for the expanded primary icon-and-label strip.</summary>
        private const double ExpandedActionsHeight = 56;

        /// <summary>Default platform width of a native AppBarButton.</summary>
        private const double HelloActionWidth = 68;

        /// <summary>Duration of the app-bar panel's upward/downward slide.</summary>
        private const double MenuAnimationMilliseconds = 180;

        /// <summary>The active view-owned menu transition, if any.</summary>
        private Storyboard menuAnimation;

        /// <summary>Monotonic transition identity rejecting callbacks from interrupted slides.</summary>
        private int menuTransitionId;

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

        /// <summary>
        /// Initializes compiled XAML, attaches the public browser control to its
        /// view model, and configures visible-bounds layout.
        /// </summary>
        public MainPage()
        {
            InitializeComponent();
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
            ApplyVisibleBounds();
        }

        /// <summary>Applies safe-area margins after the page first receives its layout slot.</summary>
        /// <param name="sender">The loaded page.</param>
        /// <param name="args">The routed Loaded event.</param>
        private void OnLoaded(object sender, RoutedEventArgs args)
        {
            if (!subscriptionsActive)
            {
                applicationView.VisibleBoundsChanged += OnVisibleBoundsChanged;
                navigationManager.BackRequested += OnBackRequested;
                inputPane.Showing += OnInputPaneShowing;
                inputPane.Hiding += OnInputPaneHiding;
                ViewModel.PropertyChanged += OnViewModelPropertyChanged;
                subscriptionsActive = true;
            }

            inputPaneOccludedRect = inputPane.OccludedRect;
            ApplyAddressEditing();
            ApplyVisibleBounds();
        }

        /// <summary>Revokes external view subscriptions without closing a temporarily unloaded browser.</summary>
        /// <param name="sender">The unloaded page.</param>
        /// <param name="args">The routed Unloaded event.</param>
        private void OnUnloaded(object sender, RoutedEventArgs args)
        {
            ViewModel.CancelAddressEditingCommand.Execute(null);
            if (subscriptionsActive)
            {
                applicationView.VisibleBoundsChanged -= OnVisibleBoundsChanged;
                navigationManager.BackRequested -= OnBackRequested;
                inputPane.Showing -= OnInputPaneShowing;
                inputPane.Hiding -= OnInputPaneHiding;
                ViewModel.PropertyChanged -= OnViewModelPropertyChanged;
                subscriptionsActive = false;
            }

            CollapseMenuImmediately();
            ViewModel.CloseMenuCommand.Execute(null);
        }

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
            double availableHeight = Math.Max(0, height - BrowserChromeHeight);
            double panelInsets = MenuSheet.Padding.Top + MenuSheet.Padding.Bottom
                + MenuSheet.BorderThickness.Top + MenuSheet.BorderThickness.Bottom;
            if (MenuSheet.Width != menuWidth)
            {
                MenuSheet.Width = menuWidth;
            }

            MenuSheet.MaxHeight = availableHeight;
            MenuScroll.MaxHeight = Math.Max(0, availableHeight - panelInsets - ExpandedActionsHeight);
            double primaryWidth = Math.Min(HelloActionWidth, menuWidth);
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
            }
            else if (args.PropertyName == "IsAddressEditing")
            {
                ApplyAddressEditing();
            }
        }

        /// <summary>Applies editor geometry and focus while leaving editing state in the ViewModel.</summary>
        private void ApplyAddressEditing()
        {
            if (ViewModel.IsAddressEditing)
            {
                CollapseMenuImmediately();
            }
            else if (AddressBox.FocusState != FocusState.Unfocused)
            {
                Focus(FocusState.Programmatic);
                inputPane.TryHide();
            }

#if LAYOUT_DIAGNOSTICS
            QueueLayoutReport();
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
                Duration = new Duration(TimeSpan.FromMilliseconds(MenuAnimationMilliseconds)),
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

        /// <summary>Reserves the keyboard's occlusion once instead of allowing automatic focus panning.</summary>
        /// <param name="sender">The app's input pane.</param>
        /// <param name="args">The keyboard rectangle and focus-visibility contract.</param>
        private void OnInputPaneShowing(InputPane sender, InputPaneVisibilityEventArgs args)
        {
            inputPaneOccludedRect = args.OccludedRect;
            args.EnsuredFocusedElementInView = true;
            ApplyVisibleBounds();
        }

        /// <summary>Restores the system-bar safe area after the docked keyboard is hidden.</summary>
        /// <param name="sender">The app's input pane.</param>
        /// <param name="args">The input-pane transition and focus-visibility contract.</param>
        private void OnInputPaneHiding(InputPane sender, InputPaneVisibilityEventArgs args)
        {
            inputPaneOccludedRect = new Windows.Foundation.Rect();
            args.EnsuredFocusedElementInView = true;
            ViewModel.CancelAddressEditingCommand.Execute(null);
            ApplyVisibleBounds();
        }

        /// <summary>
        /// Computes and applies the margins that keep shell content within the
        /// current visible bounds.
        /// </summary>
        private void ApplyVisibleBounds()
        {
            Windows.Foundation.Rect windowBounds = Window.Current.Bounds;
            Windows.Foundation.Rect visibleBounds = applicationView.VisibleBounds;
            VisibleBoundsMargins margins = VisibleBoundsHelper.ComputeMargins(
                windowBounds.X, windowBounds.Y, windowBounds.Width, windowBounds.Height,
                visibleBounds.X, visibleBounds.Y, visibleBounds.Width, visibleBounds.Height,
                inputPaneOccludedRect.X, inputPaneOccludedRect.Y,
                inputPaneOccludedRect.Width, inputPaneOccludedRect.Height);
            Thickness margin = new Thickness(margins.Left, margins.Top, margins.Right, margins.Bottom);
            if (!ShellRoot.Margin.Equals(margin))
            {
                ShellRoot.Margin = margin;
            }
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
