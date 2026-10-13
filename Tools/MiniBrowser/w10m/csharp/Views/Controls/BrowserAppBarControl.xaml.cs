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

        // x:Name generates a field in the same partial class, so internal elements
        // are referenced directly. FindName is only for another namescope.
        /// <summary>Gets the menu overlay for page-level visibility and sizing control.</summary>
        internal Grid MenuOverlay { get { return MenuOverlayElement; } }

        /// <summary>Gets the address-editing scrim for page-level layout placement.</summary>
        internal Button AddressEditingOverlay { get { return AddressEditingOverlayElement; } }

        /// <summary>Gets the menu sheet for page-level sizing and animation.</summary>
        internal Border MenuSheet { get { return MenuSheetElement; } }

        /// <summary>Gets the secondary-action scroller for page-level sizing.</summary>
        internal ScrollViewer MenuScroll { get { return MenuScrollElement; } }

        /// <summary>Gets the sheet translation used by the page's menu animation.</summary>
        internal TranslateTransform MenuSheetTranslation
        {
            get { return MenuSheetTranslationElement; }
        }

        /// <summary>Gets the clipping geometry sized by the page.</summary>
        internal RectangleGeometry MenuOverlayClip
        {
            get { return MenuOverlayClipElement; }
        }

        /// <summary>Gets the Forward menu action for focus and enabled-state checks.</summary>
        internal Button ForwardMenuButton { get { return ForwardMenuButtonElement; } }

        /// <summary>Gets the local demo menu action for focus management.</summary>
        internal Button DemoMenuButton { get { return DemoMenuButtonElement; } }

        /// <summary>Gets the native primary action host for responsive sizing.</summary>
        internal Grid HelloActionHost { get { return HelloActionHostElement; } }

        /// <summary>Gets the native Hello app-bar action for responsive sizing.</summary>
        internal AppBarButton HelloActionButton { get { return HelloActionButtonElement; } }

        /// <summary>Gets the one-DIP divider above the secondary menu rows.</summary>
        internal Border MenuTopDivider { get { return MenuTopDividerElement; } }
    }
}
