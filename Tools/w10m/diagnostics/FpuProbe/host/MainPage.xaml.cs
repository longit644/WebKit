using Windows.UI.Xaml.Controls;

namespace WebKitFpuProbe
{
    /// <summary>
    /// Probe result page. Displays the launch verdict and the verified native
    /// report written by the App class.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        /// <summary>Initializes the page from compiled XAML.</summary>
        public MainPage()
        {
            InitializeComponent();
        }

        /// <summary>Shows the recorded probe verdict and detail lines.</summary>
        /// <param name="passed">Whether every probe expectation passed.</param>
        /// <param name="details">The verification summary lines.</param>
        internal void ShowReport(bool passed, string details)
        {
            StatusText.Text = passed ? "FPU PROBE PASS 1.0.0.12" : "FPU PROBE FAIL 1.0.0.12";
            ResultsText.Text = details;
        }
    }
}
