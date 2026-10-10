using System;
using System.ComponentModel;
using System.Windows.Input;
using Windows.UI.Xaml;
using WebKit;

namespace WebKitMiniBrowser
{
    /// <summary>
    /// Owns MiniBrowser toolbar, address, title, history, loading, and status
    /// state while delegating navigation and authoritative browser state to
    /// the public <see cref="WebKitWebView"/> contract.
    /// </summary>
    public sealed class BrowserViewModel : INotifyPropertyChanged
    {
        /// <summary>The public browser control supplying authoritative navigation state.</summary>
        private readonly WebKitWebView browser;
        /// <summary>App-owned policy mediating every browser command and certificate warning.</summary>
        private readonly BrowserPolicy policy;
        /// <summary>The engine-history back command.</summary>
        private readonly ICommand backCommand;
        /// <summary>The engine-history forward command.</summary>
        private readonly ICommand forwardCommand;
        /// <summary>The current-document reload command.</summary>
        private readonly ICommand reloadCommand;
        /// <summary>The validated address navigation command.</summary>
        private readonly ICommand navigateCommand;
        /// <summary>The packaged app-owned demo command.</summary>
        private readonly ICommand loadDemoCommand;
        /// <summary>The stop command, which invalidates pending consent.</summary>
        private readonly ICommand stopCommand;
        /// <summary>The explicit one-shot certificate-consent command.</summary>
        private readonly ICommand proceedCommand;
        /// <summary>The command that expands or collapses the bottom app-bar menu.</summary>
        private readonly ICommand toggleMenuCommand;
        /// <summary>The command used by light dismissal and the phone's system Back button.</summary>
        private readonly ICommand closeMenuCommand;
        /// <summary>The command that begins a full-width address edit.</summary>
        private readonly ICommand beginAddressEditingCommand;
        /// <summary>The command that dismisses editing and restores the displayed address.</summary>
        private readonly ICommand cancelAddressEditingCommand;
        /// <summary>The sample app-bar command that acknowledges its notification badge.</summary>
        private readonly ICommand helloCommand;
        /// <summary>The current address-bar text.</summary>
        private string address;
        /// <summary>The current toolbar status message.</summary>
        private string status;
        /// <summary>Whether a matching warning HostHtml document completed and awaits consent.</summary>
        private bool canProceedWithWarning;
        /// <summary>Whether the app-bar action panel is expanded.</summary>
        private bool isMenuOpen;
        /// <summary>Whether the address editor owns the bottom bar and document scrim.</summary>
        private bool isAddressEditing;
        /// <summary>The displayed or subsequently committed address restored on cancellation.</summary>
        private string addressBeforeEditing;
        /// <summary>Whether the Hello sample action has an unread demo notification.</summary>
        private bool hasHelloNotification;

        /// <summary>
        /// Initializes the view model, subscribes to public browser state/events,
        /// and leaves the control's initial null Source unchanged.
        /// </summary>
        /// <param name="browser">The XAML-created public browser control.</param>
        /// <param name="policy">The app-owned navigation and warning policy.</param>
        /// <exception cref="ArgumentNullException">The browser control is null.</exception>
        public BrowserViewModel(WebKitWebView browser, BrowserPolicy policy)
        {
            if (browser == null)
            {
                throw new ArgumentNullException("browser");
            }

            this.browser = browser;
            this.policy = policy ?? throw new ArgumentNullException("policy");
            address = browser.Source == null ? string.Empty : browser.Source.AbsoluteUri;
            status = string.Empty;
            hasHelloNotification = true;
            backCommand = new DelegateCommand(GoBack, () => CanGoBack);
            forwardCommand = new DelegateCommand(GoForward, () => CanGoForward);
            reloadCommand = new DelegateCommand(Reload, () => HasSource);
            navigateCommand = new DelegateCommand(Navigate, () => !string.IsNullOrWhiteSpace(Address));
            loadDemoCommand = new DelegateCommand(LoadDemo, () => true);
            stopCommand = new DelegateCommand(Stop, () => IsLoading);
            proceedCommand = new DelegateCommand(Proceed, () => CanProceedWithWarning);
            toggleMenuCommand = new DelegateCommand(ToggleMenu, () => true);
            closeMenuCommand = new DelegateCommand(CloseMenu, () => true);
            beginAddressEditingCommand = new DelegateCommand(BeginAddressEditing, () => !IsAddressEditing);
            cancelAddressEditingCommand = new DelegateCommand(CancelAddressEditing, () => IsAddressEditing);
            helloCommand = new DelegateCommand(ShowHelloNotification, () => true);

            browser.PropertyChanged += OnBrowserPropertyChanged;
            browser.SourceChanged += OnSourceChanged;
            browser.HistoryChanged += OnHistoryChanged;
            browser.DocumentTitleChanged += OnDocumentTitleChanged;
            browser.NavigationCompleted += OnNavigationCompleted;
            policy.StatusChanged += OnPolicyStatusChanged;
            policy.WarningConsentReady += OnWarningConsentReady;
        }

        /// <summary>Raised whenever a bindable view-model property changes.</summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>Raised when the Hello sample command requests a Windows system toast.</summary>
        public event EventHandler HelloNotificationRequested;

        /// <summary>Gets or sets the address-bar text.</summary>
        public string Address
        {
            get { return address; }
            set
            {
                string valueOrEmpty = value ?? string.Empty;
                if (string.Equals(address, valueOrEmpty, StringComparison.Ordinal))
                {
                    return;
                }

                address = valueOrEmpty;
                OnPropertyChanged("Address");
                RaiseCommandCanExecuteChanged(navigateCommand);
            }
        }

        /// <summary>Gets the current document title copied by the browser control.</summary>
        public string DocumentTitle
        {
            get { return browser.DocumentTitle ?? string.Empty; }
        }

        /// <summary>Gets the current navigation/status message.</summary>
        public string Status
        {
            get { return status; }
            private set
            {
                if (string.Equals(status, value, StringComparison.Ordinal))
                {
                    return;
                }

                status = value;
                OnPropertyChanged("Status");
                OnPropertyChanged("StatusVisibility");
            }
        }

        /// <summary>Keeps the idle content edge clear of an empty or permanent status footer.</summary>
        public Visibility StatusVisibility
        {
            get { return string.IsNullOrWhiteSpace(Status) ? Visibility.Collapsed : Visibility.Visible; }
        }

        /// <summary>Gets whether the engine has a previous history entry.</summary>
        public bool CanGoBack
        {
            get { return browser.CanGoBack; }
        }

        /// <summary>Gets whether the engine has a next history entry.</summary>
        public bool CanGoForward
        {
            get { return browser.CanGoForward; }
        }

        /// <summary>Gets whether the browser has an outstanding main navigation.</summary>
        public bool IsLoading
        {
            get { return browser.IsLoading; }
        }

        /// <summary>Reserves loading-indicator space only while a navigation is active.</summary>
        public Visibility LoadingVisibility
        {
            get { return IsLoading ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Shows Reload only when no main-document navigation is active.</summary>
        public Visibility ReloadButtonVisibility
        {
            get { return IsLoading || IsAddressEditing ? Visibility.Collapsed : Visibility.Visible; }
        }

        /// <summary>Shows Stop in the refresh control's slot during main-document loading.</summary>
        public Visibility StopButtonVisibility
        {
            get { return IsLoading && !IsAddressEditing ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Gets whether the browser currently has a source document.</summary>
        public bool HasSource
        {
            get { return browser.Source != null; }
        }

        /// <summary>Gets whether a completed app warning is awaiting explicit user consent.</summary>
        public bool CanProceedWithWarning
        {
            get { return canProceedWithWarning; }
            private set
            {
                if (canProceedWithWarning == value)
                {
                    return;
                }

                canProceedWithWarning = value;
                OnPropertyChanged("CanProceedWithWarning");
                OnPropertyChanged("WarningConsentVisibility");
                RaiseCommandCanExecuteChanged(proceedCommand);
            }
        }

        /// <summary>Gets the XAML visibility used for the explicit consent button.</summary>
        public Visibility WarningConsentVisibility
        {
            get { return CanProceedWithWarning ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Gets whether the bottom app-bar panel is expanded; the view owns its animation.</summary>
        public bool IsMenuOpen
        {
            get { return isMenuOpen; }
            private set
            {
                if (isMenuOpen == value)
                {
                    return;
                }

                isMenuOpen = value;
                OnPropertyChanged("IsMenuOpen");
                OnPropertyChanged("MenuButtonLabel");
            }
        }

        /// <summary>Gets the accessible label for the menu toggle in its current state.</summary>
        public string MenuButtonLabel
        {
            get { return IsMenuOpen ? "Close actions" : "More actions"; }
        }

        /// <summary>Gets whether the focused address editor occupies the full bottom bar.</summary>
        public bool IsAddressEditing
        {
            get { return isAddressEditing; }
            private set
            {
                if (isAddressEditing == value)
                {
                    return;
                }

                isAddressEditing = value;
                OnPropertyChanged("IsAddressEditing");
                OnPropertyChanged("AddressEditingVisibility");
                OnPropertyChanged("CompactChromeVisibility");
                OnPropertyChanged("ReloadButtonVisibility");
                OnPropertyChanged("StopButtonVisibility");
                RaiseCommandCanExecuteChanged(beginAddressEditingCommand);
                RaiseCommandCanExecuteChanged(cancelAddressEditingCommand);
            }
        }

        /// <summary>Gets the document-scrim visibility while editing the address.</summary>
        public Visibility AddressEditingVisibility
        {
            get { return IsAddressEditing ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Hides the idle side buttons and divider while the editor spans the bar.</summary>
        public Visibility CompactChromeVisibility
        {
            get { return IsAddressEditing ? Visibility.Collapsed : Visibility.Visible; }
        }

        /// <summary>Gets whether the Hello demonstration has an unread notification.</summary>
        public bool HasHelloNotification
        {
            get { return hasHelloNotification; }
            private set
            {
                if (hasHelloNotification == value)
                {
                    return;
                }

                hasHelloNotification = value;
                OnPropertyChanged("HasHelloNotification");
                OnPropertyChanged("HelloNotificationVisibility");
            }
        }

        /// <summary>Gets visibility for the small numeric badge on the Hello action.</summary>
        public Visibility HelloNotificationVisibility
        {
            get { return HasHelloNotification ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Gets the command that traverses the previous engine history entry.</summary>
        public ICommand BackCommand
        {
            get { return backCommand; }
        }

        /// <summary>Gets the command that traverses the next engine history entry.</summary>
        public ICommand ForwardCommand
        {
            get { return forwardCommand; }
        }

        /// <summary>Gets the command that reloads the current document.</summary>
        public ICommand ReloadCommand
        {
            get { return reloadCommand; }
        }

        /// <summary>Gets the command that validates and navigates to the address-bar value.</summary>
        public ICommand NavigateCommand
        {
            get { return navigateCommand; }
        }

        /// <summary>Gets the command that displays the packaged local demo.</summary>
        public ICommand LoadDemoCommand
        {
            get { return loadDemoCommand; }
        }

        /// <summary>Gets the command that stops navigation through app policy.</summary>
        public ICommand StopCommand
        {
            get { return stopCommand; }
        }

        /// <summary>Gets the command that consumes a ready warning token with explicit consent.</summary>
        public ICommand ProceedCommand
        {
            get { return proceedCommand; }
        }

        /// <summary>Gets the command invoked by the app-bar three-dot button.</summary>
        public ICommand ToggleMenuCommand
        {
            get { return toggleMenuCommand; }
        }

        /// <summary>Gets the command that dismisses an expanded panel without navigating.</summary>
        public ICommand CloseMenuCommand
        {
            get { return closeMenuCommand; }
        }

        /// <summary>Gets the command invoked when the address receives focus.</summary>
        public ICommand BeginAddressEditingCommand
        {
            get { return beginAddressEditingCommand; }
        }

        /// <summary>Gets the command used by outside tap, focus loss, Escape, and system Back.</summary>
        public ICommand CancelAddressEditingCommand
        {
            get { return cancelAddressEditingCommand; }
        }

        /// <summary>Gets the Hello demonstration command shown in the primary app-bar strip.</summary>
        public ICommand HelloCommand
        {
            get { return helloCommand; }
        }

        /// <summary>Acknowledges the sample notification and exposes a short app-owned status.</summary>
        private void ShowHelloNotification()
        {
            CloseMenu();
            HasHelloNotification = false;
            EventHandler handler = HelloNotificationRequested;
            if (handler != null)
            {
                handler(this, EventArgs.Empty);
            }
        }

        /// <summary>Closes the action panel and preserves the current address before editing.</summary>
        private void BeginAddressEditing()
        {
            CloseMenu();
            addressBeforeEditing = Address;
            IsAddressEditing = true;
        }

        /// <summary>Discards an unsubmitted draft without navigating or changing history.</summary>
        private void CancelAddressEditing()
        {
            if (!IsAddressEditing)
            {
                return;
            }

            Address = addressBeforeEditing;
            IsAddressEditing = false;
        }

        /// <summary>Changes the app-bar expansion state independently of browser history.</summary>
        private void ToggleMenu()
        {
            CancelAddressEditing();
            IsMenuOpen = !IsMenuOpen;
        }

        /// <summary>Dismisses the app-bar panel without modifying the current document.</summary>
        private void CloseMenu()
        {
            IsMenuOpen = false;
        }

        /// <summary>Dismisses the panel and loads the application-owned demo through policy.</summary>
        private void LoadDemo()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.NavigateToDemo();
        }

        /// <summary>Dismisses the panel and consumes the ready certificate decision once.</summary>
        private void Proceed()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.ProceedWithCurrentWarning();
        }

        /// <summary>
        /// Traverses backward using the public engine history operation.
        /// </summary>
        private void GoBack()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.GoBack();
        }

        /// <summary>
        /// Traverses forward using the public engine history operation.
        /// </summary>
        private void GoForward()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.GoForward();
        }

        /// <summary>
        /// Reloads using the public browser operation.
        /// </summary>
        private void Reload()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.Reload();
        }

        /// <summary>Stops the current document through the policy invalidation path.</summary>
        private void Stop()
        {
            CancelAddressEditing();
            CloseMenu();
            policy.Stop();
        }

        /// <summary>
        /// Navigates to the address only when it is an absolute HTTP(S) URI.
        /// </summary>
        private void Navigate()
        {
            CloseMenu();
            Uri uri;
            if (!Uri.TryCreate(Address, UriKind.Absolute, out uri) ||
                (!string.Equals(uri.Scheme, "http", StringComparison.OrdinalIgnoreCase) &&
                 !string.Equals(uri.Scheme, "https", StringComparison.OrdinalIgnoreCase)))
            {
                Status = "Invalid address. Type an absolute http(s) URI.";
                return;
            }

            IsAddressEditing = false;
            policy.Navigate(uri);
        }

        /// <summary>
        /// Synchronizes the view model when the control publishes copied state.
        /// </summary>
        /// <param name="sender">The browser control.</param>
        /// <param name="args">The changed-property notification.</param>
        private void OnBrowserPropertyChanged(object sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == "Source")
            {
                Uri source = browser.Source;
                SynchronizeAddress(source);
                OnPropertyChanged("HasSource");
                RaiseCommandCanExecuteChanged(reloadCommand);
            }
            else if (args.PropertyName == "CanGoBack")
            {
                OnPropertyChanged("CanGoBack");
                RaiseCommandCanExecuteChanged(backCommand);
            }
            else if (args.PropertyName == "CanGoForward")
            {
                OnPropertyChanged("CanGoForward");
                RaiseCommandCanExecuteChanged(forwardCommand);
            }
            else if (args.PropertyName == "IsLoading")
            {
                OnPropertyChanged("IsLoading");
                OnPropertyChanged("LoadingVisibility");
                OnPropertyChanged("ReloadButtonVisibility");
                OnPropertyChanged("StopButtonVisibility");
                RaiseCommandCanExecuteChanged(stopCommand);
            }
            else if (args.PropertyName == "DocumentTitle")
            {
                OnPropertyChanged("DocumentTitle");
            }
        }

        /// <summary>
        /// Follows the engine's actual committed URI in the address bar.
        /// </summary>
        /// <param name="sender">The browser control.</param>
        /// <param name="args">The copied committed-source notification.</param>
        private void OnSourceChanged(WebKitWebView sender, WebKitSourceChangedEventArgs args)
        {
            SynchronizeAddress(args.Uri);
        }

        /// <summary>Tracks committed source without replacing a draft while the user is typing.</summary>
        /// <param name="source">The latest source copied by the public control.</param>
        private void SynchronizeAddress(Uri source)
        {
            string committedAddress = source == null ? string.Empty : source.AbsoluteUri;
            if (IsAddressEditing)
            {
                addressBeforeEditing = committedAddress;
            }
            else
            {
                Address = committedAddress;
            }
        }

        /// <summary>
        /// Refreshes history command availability from the installed engine snapshot.
        /// </summary>
        /// <param name="sender">The browser control.</param>
        /// <param name="args">The copied history flags.</param>
        private void OnHistoryChanged(WebKitWebView sender, WebKitHistoryChangedEventArgs args)
        {
            OnPropertyChanged("CanGoBack");
            OnPropertyChanged("CanGoForward");
            RaiseCommandCanExecuteChanged(backCommand);
            RaiseCommandCanExecuteChanged(forwardCommand);
        }

        /// <summary>
        /// Refreshes the displayed title after the control installs its copied title.
        /// </summary>
        /// <param name="sender">The browser control.</param>
        /// <param name="args">The copied title.</param>
        private void OnDocumentTitleChanged(WebKitWebView sender, WebKitDocumentTitleChangedEventArgs args)
        {
            OnPropertyChanged("DocumentTitle");
        }

        /// <summary>
        /// Publishes the terminal result and current loading snapshot to bindings.
        /// </summary>
        /// <param name="sender">The browser control.</param>
        /// <param name="args">The recorded navigation terminal outcome.</param>
        private void OnNavigationCompleted(WebKitWebView sender, WebKitNavigationCompletedEventArgs args)
        {
            OnPropertyChanged("IsLoading");
            OnPropertyChanged("LoadingVisibility");
            OnPropertyChanged("ReloadButtonVisibility");
            OnPropertyChanged("StopButtonVisibility");
            RaiseCommandCanExecuteChanged(stopCommand);
        }

        /// <summary>Receives app-owned navigation and certificate-policy status messages.</summary>
        /// <param name="text">The status text selected by BrowserPolicy.</param>
        private void OnPolicyStatusChanged(string text)
        {
            Status = text;
        }

        /// <summary>Receives whether the warning page has committed for explicit consent.</summary>
        /// <param name="ready">True only for the matching completed warning HostHtml.</param>
        private void OnWarningConsentReady(bool ready)
        {
            CanProceedWithWarning = ready;
        }

        /// <summary>
        /// Raises the standard binding notification for one named property.
        /// </summary>
        /// <param name="propertyName">The changed property name.</param>
        private void OnPropertyChanged(string propertyName)
        {
            PropertyChangedEventHandler handler = PropertyChanged;
            if (handler != null)
            {
                handler(this, new PropertyChangedEventArgs(propertyName));
            }
        }

        /// <summary>
        /// Requests a command to reevaluate its availability when it supports it.
        /// </summary>
        /// <param name="command">The command whose availability may have changed.</param>
        private static void RaiseCommandCanExecuteChanged(ICommand command)
        {
            DelegateCommand delegateCommand = command as DelegateCommand;
            if (delegateCommand != null)
            {
                delegateCommand.RaiseCanExecuteChanged();
            }
        }

        /// <summary>
        /// Small command implementation whose availability is evaluated from live view-model state.
        /// </summary>
        private sealed class DelegateCommand : ICommand
        {
            /// <summary>The operation invoked by this command.</summary>
            private readonly Action execute;
            /// <summary>The live availability predicate for this command.</summary>
            private readonly Func<bool> canExecute;

            /// <summary>
            /// Creates a command with execution and availability delegates.
            /// </summary>
            /// <param name="execute">The action to invoke.</param>
            /// <param name="canExecute">The predicate controlling command availability.</param>
            internal DelegateCommand(Action execute, Func<bool> canExecute)
            {
                this.execute = execute;
                this.canExecute = canExecute;
            }

            /// <summary>Raised when the command's availability may have changed.</summary>
            public event EventHandler CanExecuteChanged;

            /// <summary>Determines whether this command can execute.</summary>
            /// <param name="parameter">Unused command parameter.</param>
            /// <returns>True when the availability predicate succeeds.</returns>
            public bool CanExecute(object parameter)
            {
                return canExecute();
            }

            /// <summary>Executes this command when it is currently available.</summary>
            /// <param name="parameter">Unused command parameter.</param>
            public void Execute(object parameter)
            {
                if (CanExecute(parameter))
                {
                    execute();
                }
            }

            /// <summary>Notifies command consumers to reevaluate availability.</summary>
            internal void RaiseCanExecuteChanged()
            {
                EventHandler handler = CanExecuteChanged;
                if (handler != null)
                {
                    handler(this, EventArgs.Empty);
                }
            }
        }
    }
}
