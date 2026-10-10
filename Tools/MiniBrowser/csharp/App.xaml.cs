using System;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WebKitMiniBrowser
{
    /// <summary>
    /// MiniBrowser application: shows the main shell page and waits for user
    /// input. No test navigation runs at startup; the hosted WebKitWebView
    /// keeps its null Source until the user navigates.
    /// </summary>
    sealed partial class App : Application
    {
        /// <summary>
        /// Initializes the application object and subscribes suspension.
        /// </summary>
        public App()
        {
            InitializeComponent();
            Suspending += OnSuspending;
        }

        /// <summary>
        /// Invoked when the application is launched. Shows the main page.
        /// </summary>
        /// <param name="e">Launch details.</param>
        protected override void OnLaunched(LaunchActivatedEventArgs e)
        {
            // The shell applies VisibleBounds once. UseVisible would also inset
            // the Frame automatically and leave duplicate top/bottom gutters.
            ApplicationView.GetForCurrentView().SetDesiredBoundsMode(ApplicationViewBoundsMode.UseCoreWindow);
            Frame rootFrame = Window.Current.Content as Frame;
            if (rootFrame == null)
            {
                rootFrame = new Frame();
                rootFrame.HorizontalContentAlignment = HorizontalAlignment.Stretch;
                rootFrame.VerticalContentAlignment = VerticalAlignment.Stretch;
                Window.Current.Content = rootFrame;
            }

            if (rootFrame.Content == null)
            {
                rootFrame.Navigate(typeof(MainPage), e.Arguments);
            }

            Window.Current.Activate();
        }

        /// <summary>
        /// Invoked when the OS suspends the application. Suspension lifetime
        /// and control Close belong to task 8.8; this shell only releases the
        /// deferral so suspension is never blocked here.
        /// </summary>
        /// <param name="sender">The application instance.</param>
        /// <param name="e">Suspension details.</param>
        private void OnSuspending(object sender, SuspendingEventArgs e)
        {
            SuspendingDeferral deferral = e.SuspendingOperation.GetDeferral();
            deferral.Complete();
        }
    }
}
