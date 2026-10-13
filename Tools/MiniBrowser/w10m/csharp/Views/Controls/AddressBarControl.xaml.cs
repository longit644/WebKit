using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;

namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Reusable bottom address chrome with compact and editing layouts.</summary>
    public sealed partial class AddressBarControl : UserControl
    {
        /// <summary>Render-only movement of the actual input, preserving focus and unscaled glyphs.</summary>
        private readonly ChromeLayoutTransition inputTransition;

        /// <summary>Independent movement and size of the explicitly filled editor background.</summary>
        private readonly ChromeLayoutTransition surfaceTransition;

        // x:Name in XAML generates a field in the same partial class, so internal
        // elements are referenced directly (ChromeGrid, AddressInput, ...).
        // FindName is only for another namescope or a ControlTemplate.

        /// <summary>Visual state name for the compact browsing chrome.</summary>
        private const string CompactAddressStateName = "CompactAddress";

        /// <summary>Visual state name for the expanded editing chrome.</summary>
        private const string EditingAddressStateName = "EditingAddress";

        /// <summary>Identifies the <see cref="IsAddressEditing"/> dependency property.</summary>
        public static readonly DependencyProperty IsAddressEditingProperty =
            DependencyProperty.Register(
                nameof(IsAddressEditing),
                typeof(bool),
                typeof(AddressBarControl),
                new PropertyMetadata(false, OnIsAddressEditingChanged));

        /// <summary>Initializes the control and its compiled XAML content.</summary>
        public AddressBarControl()
        {
            InitializeComponent();
            inputTransition = new ChromeLayoutTransition(AddressInput, AddressLayoutRoot,
                transitionMilliseconds: BrowserChromeTiming.AddressMorphMilliseconds);
            surfaceTransition = new ChromeLayoutTransition(AddressSurface,
                AddressLayoutRoot, animateSize: true,
                transitionMilliseconds: BrowserChromeTiming.AddressMorphMilliseconds);
        }

        /// <summary>Gets or sets whether the address field uses its expanded editing layout.</summary>
        public bool IsAddressEditing
        {
            get { return (bool)GetValue(IsAddressEditingProperty); }
            set { SetValue(IsAddressEditingProperty, value); }
        }

        /// <summary>Gets the address chrome grid for page-level layout diagnostics.</summary>
        internal Grid BrowserChrome
        {
            get { return BrowserChromeElement; }
        }

        /// <summary>Gets the address input for page-level focus and navigation handling.</summary>
        internal TextBox AddressBox
        {
            get { return AddressBoxElement; }
        }

        /// <summary>Gets the overflow button for page-level focus management.</summary>
        internal Button MoreButton
        {
            get { return MoreButtonElement; }
        }

        /// <summary>Gets the Reload action for page-level layout diagnostics.</summary>
        internal Button ReloadActionElement { get { return ReloadButton; } }

        /// <summary>Gets the Stop action for page-level layout diagnostics.</summary>
        internal Button StopActionElement { get { return StopButton; } }

        /// <summary>Gets the compact-mode divider for page-level layout diagnostics.</summary>
        internal Border AddressDividerElement { get { return AddressDivider; } }

        /// <summary>Keeps the editor fill during the compact morph, then removes it at completion.</summary>
        /// <param name="visible">Whether the compact morph still needs its filled background.</param>
        internal void SetTransitionBackground(bool visible)
        {
            AddressSurface.Opacity = visible ? 1 : 0;
        }

        /// <summary>Sets one opacity for every compact control so refresh, dots and splitter fade together.</summary>
        /// <param name="value">Target opacity for the compact cluster.</param>
        internal void SetCompactControlsOpacity(double value)
        {
            ReloadButton.Opacity = value;
            StopButton.Opacity = value;
            OverflowButton.Opacity = value;
            AddressDivider.Opacity = value;
            AddressTapTarget.Opacity = value;
        }

        /// <summary>Gets every compact control animated as one group during edit exit.</summary>
        internal FrameworkElement[] CompactControlElements
        {
            get
            {
                return new FrameworkElement[]
                {
                    ReloadButton,
                    StopButton,
                    OverflowButton,
                    AddressDivider,
                    AddressTapTarget
                };
            }
        }

#if HOSTHTML_SMOKE
        /// <summary>Records actual input/template colors and vertical alignment without address text.</summary>
        /// <returns>Color and theme metadata for candidate interaction diagnostics.</returns>
        internal string GetColorDiagnostic()
        {
            TextBox input = AddressBoxElement;
            ScrollViewer content = FindInputContent(input);
            Border surface = AddressSurface;
            return ";app-theme=" + Application.Current.RequestedTheme
                + ";input-theme=" + input.RequestedTheme + ";input-foreground=" + BrushColor(input.Foreground)
                + ";native-theme=" + (content == null ? "missing" : content.RequestedTheme.ToString())
                + ";native-foreground=" + (content == null ? "missing" : BrushColor(content.Foreground))
                + ";native-v-align=" + (content == null ? "missing" : content.VerticalContentAlignment.ToString())
                + ";native-height=" + (content == null ? "missing" : content.ActualHeight.ToString())
                + ";input-height=" + input.ActualHeight + ";chrome-height=" + BrowserChromeElement.ActualHeight
                + ";padding=" + input.Padding + ";field=" + BrushColor(surface.Background)
                + ";field-opacity=" + surface.Opacity;
        }

        /// <summary>Finds the native TextBox content host after template application.</summary>
        /// <param name="root">The input or one of its template children.</param>
        /// <returns>The named native content ScrollViewer, or null before template application.</returns>
        private static ScrollViewer FindInputContent(DependencyObject root)
        {
            ScrollViewer content = root as ScrollViewer;
            if (content != null && content.Name == "ContentElement")
                return content;
            for (int index = 0; index < VisualTreeHelper.GetChildrenCount(root); ++index)
            {
                content = FindInputContent(VisualTreeHelper.GetChild(root, index));
                if (content != null)
                    return content;
            }
            return null;
        }

        /// <summary>Formats a solid color brush for value-only diagnostics.</summary>
        /// <param name="brush">The effective color brush.</param>
        /// <returns>ARGB text or the non-solid marker.</returns>
        private static string BrushColor(Brush brush)
        {
            SolidColorBrush solid = brush as SolidColorBrush;
            return solid == null ? "non-solid" : solid.Color.ToString();
        }
#endif

        /// <summary>Applies the initial visual state after the control is loaded.</summary>
        /// <param name="sender">The loaded control.</param>
        /// <param name="args">The routed loaded event.</param>
        private void OnLoaded(object sender, RoutedEventArgs args)
        {
            inputTransition.Reset();
            surfaceTransition.Reset();
            ApplyAddressEditingState();
        }

        /// <summary>Updates the visual state when the dependency property changes.</summary>
        /// <param name="dependencyObject">The address control whose state changed.</param>
        /// <param name="args">The dependency-property change details.</param>
        private static void OnIsAddressEditingChanged(DependencyObject dependencyObject,
            DependencyPropertyChangedEventArgs args)
        {
            AddressBarControl control = (AddressBarControl)dependencyObject;
            if (control.inputTransition != null)
                control.inputTransition.PrepareTransition();
            if (control.surfaceTransition != null)
                control.surfaceTransition.PrepareTransition();
            control.ApplyAddressEditingState();
        }

        /// <summary>Installs final geometry; render-only transitions interpolate after natural layout.</summary>
        private void ApplyAddressEditingState()
        {
            VisualStateManager.GoToState(this,
                IsAddressEditing ? EditingAddressStateName : CompactAddressStateName, false);
            AddressSurface.Opacity = IsAddressEditing ? 1 : 0;
        }

        /// <summary>Gets the generated address chrome element.</summary>
        private Grid BrowserChromeElement
        {
            get { return ChromeGrid; }
        }

        /// <summary>Gets the generated address input element.</summary>
        private TextBox AddressBoxElement
        {
            get { return AddressInput; }
        }

        /// <summary>Gets the generated overflow button element.</summary>
        private Button MoreButtonElement
        {
            get { return OverflowButton; }
        }
    }
}
