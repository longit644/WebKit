using Windows.UI.Xaml.Controls;

namespace WebKitTlsSmoke
{
    /// <summary>
    /// TlsSmoke probe page. Displays the launch verdict and the native report.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        /// <summary>Report version matching the staged package version.</summary>
        private const string ReportVersion = "1.0.0.9";

        /// <summary>Initializes the page from compiled XAML.</summary>
        public MainPage()
        {
            InitializeComponent();
        }

        /// <summary>Shows the completed native probe report.</summary>
        /// <param name="report">Report text read back from LocalState.</param>
        internal void ShowResult(string report)
        {
            StatusText.Text = "TLS SMOKE DONE " + ReportVersion;
            ReportText.Text = report;
        }
    }
}
