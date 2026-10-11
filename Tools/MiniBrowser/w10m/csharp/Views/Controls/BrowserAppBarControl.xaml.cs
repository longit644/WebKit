using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Shapes;

namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Reusable address-editing scrim and overflow app-bar surface.</summary>
    public sealed partial class BrowserAppBarControl : UserControl
    {
        /// <summary>Initializes the app-bar control and its compiled XAML content.</summary>
        public BrowserAppBarControl()
        {
            InitializeComponent();
        }

        /// <summary>Gets the menu overlay for page-level visibility and sizing control.</summary>
        internal Grid MenuOverlay { get { return (Grid)FindName("MenuOverlayElement"); } }

        /// <summary>Gets the address-editing scrim for page-level layout placement.</summary>
        internal Button AddressEditingOverlay { get { return (Button)FindName("AddressEditingOverlayElement"); } }

        /// <summary>Gets the menu sheet for page-level sizing and animation.</summary>
        internal Border MenuSheet { get { return (Border)FindName("MenuSheetElement"); } }

        /// <summary>Gets the secondary-action scroller for page-level sizing.</summary>
        internal ScrollViewer MenuScroll { get { return (ScrollViewer)FindName("MenuScrollElement"); } }

        /// <summary>Gets the sheet translation used by the page's menu animation.</summary>
        internal TranslateTransform MenuSheetTranslation
        {
            get { return (TranslateTransform)FindName("MenuSheetTranslationElement"); }
        }

        /// <summary>Gets the clipping geometry sized by the page.</summary>
        internal RectangleGeometry MenuOverlayClip
        {
            get { return (RectangleGeometry)FindName("MenuOverlayClipElement"); }
        }

        /// <summary>Gets the Forward menu action for focus and enabled-state checks.</summary>
        internal Button ForwardMenuButton { get { return (Button)FindName("ForwardMenuButtonElement"); } }

        /// <summary>Gets the local demo menu action for focus management.</summary>
        internal Button DemoMenuButton { get { return (Button)FindName("DemoMenuButtonElement"); } }

        /// <summary>Gets the native primary action host for responsive sizing.</summary>
        internal Grid HelloActionHost { get { return (Grid)FindName("HelloActionHostElement"); } }

        /// <summary>Gets the native Hello app-bar action for responsive sizing.</summary>
        internal AppBarButton HelloActionButton { get { return (AppBarButton)FindName("HelloActionButtonElement"); } }

        /// <summary>Gets the one-DIP divider above the secondary menu rows.</summary>
        internal Border MenuTopDivider { get { return (Border)FindName("MenuTopDividerElement"); } }
    }
}
