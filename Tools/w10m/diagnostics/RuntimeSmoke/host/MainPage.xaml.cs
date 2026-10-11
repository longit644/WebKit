using Windows.UI.Xaml.Controls;

namespace WebKitWebView.RuntimeSmoke
{
    /// <summary>
    /// Smoke result page. Displays the launch verdict written by the App class.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        /// <summary>Initializes the page from compiled XAML.</summary>
        public MainPage()
        {
            InitializeComponent();
        }

        /// <summary>Shows the recorded smoke verdict.</summary>
        /// <param name="passed">Whether every host and payload stage passed.</param>
        internal void ShowResult(bool passed)
        {
            StatusText.Text = passed ? "RUNTIME SMOKE PASS 1.0.0.11" : "RUNTIME SMOKE FAIL 1.0.0.11";
        }
    }
}
