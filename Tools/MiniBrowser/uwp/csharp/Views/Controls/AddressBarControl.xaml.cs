using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Reusable bottom address chrome with compact and editing layouts.</summary>
    public sealed partial class AddressBarControl : UserControl
    {
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

        /// <summary>Applies the initial visual state after the control is loaded.</summary>
        /// <param name="sender">The loaded control.</param>
        /// <param name="args">The routed loaded event.</param>
        private void OnLoaded(object sender, RoutedEventArgs args)
        {
            ApplyAddressEditingState();
        }

        /// <summary>Updates the visual state when the dependency property changes.</summary>
        /// <param name="dependencyObject">The address control whose state changed.</param>
        /// <param name="args">The dependency-property change details.</param>
        private static void OnIsAddressEditingChanged(DependencyObject dependencyObject,
            DependencyPropertyChangedEventArgs args)
        {
            ((AddressBarControl)dependencyObject).ApplyAddressEditingState();
        }

        /// <summary>Chooses the compact or editing geometry for the address chrome.</summary>
        private void ApplyAddressEditingState()
        {
            VisualStateManager.GoToState(this,
                IsAddressEditing ? "EditingAddress" : "CompactAddress", false);
        }

        /// <summary>Gets the generated address chrome element.</summary>
        private Grid BrowserChromeElement
        {
            get { return (Grid)FindName("ChromeGrid"); }
        }

        /// <summary>Gets the generated address input element.</summary>
        private TextBox AddressBoxElement
        {
            get { return (TextBox)FindName("AddressInput"); }
        }

        /// <summary>Gets the generated overflow button element.</summary>
        private Button MoreButtonElement
        {
            get { return (Button)FindName("OverflowButton"); }
        }
    }
}
