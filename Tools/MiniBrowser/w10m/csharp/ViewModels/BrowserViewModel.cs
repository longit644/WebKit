using GalaSoft.MvvmLight;
using GalaSoft.MvvmLight.Command;
using System;
using WebKitMiniBrowser.Models;
using WebKitMiniBrowser.Services.Interfaces;
using Windows.UI.Xaml;

namespace WebKitMiniBrowser.ViewModels
{
    /// <summary>Browser-shell state and commands implemented with MVVM Light.</summary>
    public sealed class BrowserViewModel : ViewModelBase, IDisposable
    {
        /// <summary>Service exposing copied WebKit state and policy-mediated navigation.</summary>
        private readonly IBrowserService browserService;

        /// <summary>Service for showing system toast notifications.</summary>
        private readonly IToastNotificationService toastNotificationService;

        /// <summary>Command for traversing backward in browser history.</summary>
        private readonly RelayCommand backCommand;

        /// <summary>Command for traversing forward in browser history.</summary>
        private readonly RelayCommand forwardCommand;

        /// <summary>Command for reloading an existing idle document.</summary>
        private readonly RelayCommand reloadCommand;

        /// <summary>Command for validating and submitting the address.</summary>
        private readonly RelayCommand navigateCommand;

        /// <summary>Command for displaying the packaged demo page.</summary>
        private readonly RelayCommand loadDemoCommand;

        /// <summary>Command for stopping an active navigation.</summary>
        private readonly RelayCommand stopCommand;

        /// <summary>Command for consuming a completed certificate warning.</summary>
        private readonly RelayCommand proceedCommand;

        /// <summary>Command for toggling the secondary app-bar menu.</summary>
        private readonly RelayCommand toggleMenuCommand;

        /// <summary>Command for closing the secondary app-bar menu.</summary>
        private readonly RelayCommand closeMenuCommand;

        /// <summary>Command for entering full-width address editing.</summary>
        private readonly RelayCommand beginAddressEditingCommand;

        /// <summary>Command for cancelling an unsubmitted address draft.</summary>
        private readonly RelayCommand cancelAddressEditingCommand;

        /// <summary>Command for displaying the Hello system-toast demonstration.</summary>
        private readonly RelayCommand helloCommand;

        /// <summary>Current or draft address-bar text.</summary>
        private string address;

        /// <summary>Transient message from app-owned navigation policy.</summary>
        private string status;

        /// <summary>Whether an HTTPS warning page completed and awaits consent.</summary>
        private bool canProceedWithWarning;

        /// <summary>Whether the secondary app-bar menu is open.</summary>
        private bool isMenuOpen;

        /// <summary>Whether the address editor is in expanded editing mode.</summary>
        private bool isAddressEditing;

        /// <summary>Latest committed address to restore if the current draft is cancelled.</summary>
        private string addressBeforeEditing;

        /// <summary>Whether the Hello sample badge remains unacknowledged.</summary>
        private bool hasHelloNotification;

        /// <summary>Latest immutable state snapshot published by the browser service.</summary>
        private BrowserNavigationState browserState;

        /// <summary>Whether service subscriptions have been revoked.</summary>
        private bool isDisposed;

        /// <summary>Creates the browser view model from its page-scoped services.</summary>
        /// <param name="browserService">The browser state and navigation service.</param>
        /// <param name="toastNotificationService">The Windows toast service.</param>
        public BrowserViewModel(IBrowserService browserService, IToastNotificationService toastNotificationService)
        {
            this.browserService = browserService ?? throw new ArgumentNullException(nameof(browserService));
            this.toastNotificationService = toastNotificationService ?? throw new ArgumentNullException(nameof(toastNotificationService));
            browserState = browserService.CurrentState;
            address = browserState.Source == null ? string.Empty : browserState.Source.AbsoluteUri;
            status = string.Empty;
            hasHelloNotification = true;

            backCommand = new RelayCommand(GoBack, () => CanGoBack, true);
            forwardCommand = new RelayCommand(GoForward, () => CanGoForward, true);
            reloadCommand = new RelayCommand(Reload, () => HasSource && !IsLoading, true);
            navigateCommand = new RelayCommand(Navigate, () => !string.IsNullOrWhiteSpace(Address), true);
            loadDemoCommand = new RelayCommand(LoadDemo, true);
            stopCommand = new RelayCommand(Stop, () => IsLoading, true);
            proceedCommand = new RelayCommand(Proceed, () => CanProceedWithWarning, true);
            toggleMenuCommand = new RelayCommand(ToggleMenu, true);
            closeMenuCommand = new RelayCommand(CloseMenu, true);
            beginAddressEditingCommand = new RelayCommand(BeginAddressEditing, () => !IsAddressEditing, true);
            cancelAddressEditingCommand = new RelayCommand(CancelAddressEditing, () => IsAddressEditing, true);
            helloCommand = new RelayCommand(ShowHelloNotification, true);

            browserService.StateChanged += OnBrowserStateChanged;
            browserService.StatusChanged += OnStatusChanged;
            browserService.WarningConsentReady += OnWarningConsentReady;
        }

        /// <summary>Gets or sets the editable address-bar text.</summary>
        public string Address
        {
            get { return address; }
            set
            {
                if (Set(ref address, value ?? string.Empty, nameof(Address)))
                    navigateCommand.RaiseCanExecuteChanged();
            }
        }

        /// <summary>Gets the title copied from the current browser document.</summary>
        public string DocumentTitle { get { return browserState.DocumentTitle; } }

        /// <summary>Gets the policy-status visibility retained for non-footer consumers.</summary>
        public Visibility StatusVisibility
        {
            get { return string.IsNullOrWhiteSpace(Status) ? Visibility.Collapsed : Visibility.Visible; }
        }

        /// <summary>Gets the current app-owned navigation status.</summary>
        public string Status
        {
            get { return status; }
            private set
            {
                if (Set(ref status, value, nameof(Status)))
                    RaisePropertyChanged(() => StatusVisibility);
            }
        }

        /// <summary>Gets whether browser history contains a previous entry.</summary>
        public bool CanGoBack { get { return browserState.CanGoBack; } }

        /// <summary>Gets whether browser history contains a forward entry.</summary>
        public bool CanGoForward { get { return browserState.CanGoForward; } }

        /// <summary>Gets whether a main-document navigation is active.</summary>
        public bool IsLoading { get { return browserState.IsLoading; } }

        /// <summary>Gets visibility for the thin loading-progress line.</summary>
        public Visibility LoadingVisibility { get { return IsLoading ? Visibility.Visible : Visibility.Collapsed; } }

        /// <summary>Gets whether a committed source is available.</summary>
        public bool HasSource { get { return browserState.Source != null; } }

        /// <summary>Gets whether the matching certificate warning completed for consent.</summary>
        public bool CanProceedWithWarning
        {
            get { return canProceedWithWarning; }
            private set
            {
                if (Set(ref canProceedWithWarning, value, nameof(CanProceedWithWarning)))
                {
                    RaisePropertyChanged(() => WarningConsentVisibility);
                    proceedCommand.RaiseCanExecuteChanged();
                }
            }
        }

        /// <summary>Gets visibility for the explicit certificate-consent action.</summary>
        public Visibility WarningConsentVisibility
        {
            get { return CanProceedWithWarning ? Visibility.Visible : Visibility.Collapsed; }
        }

        /// <summary>Gets whether the secondary app-bar menu is expanded.</summary>
        public bool IsMenuOpen
        {
            get { return isMenuOpen; }
            private set
            {
                if (Set(ref isMenuOpen, value, nameof(IsMenuOpen)))
                    RaisePropertyChanged(() => MenuButtonLabel);
            }
        }

        /// <summary>Gets the accessible label of the menu toggle.</summary>
        public string MenuButtonLabel { get { return IsMenuOpen ? "Close actions" : "More actions"; } }

        /// <summary>Gets whether the address editor occupies the full bottom chrome.</summary>
        public bool IsAddressEditing
        {
            get { return isAddressEditing; }
            private set
            {
                if (Set(ref isAddressEditing, value, nameof(IsAddressEditing)))
                {
                    RaisePropertyChanged(() => AddressEditingVisibility);
                    RaisePropertyChanged(() => CompactChromeVisibility);
                    RaisePropertyChanged(() => ReloadButtonVisibility);
                    RaisePropertyChanged(() => StopButtonVisibility);
                    beginAddressEditingCommand.RaiseCanExecuteChanged();
                    cancelAddressEditingCommand.RaiseCanExecuteChanged();
                }
            }
        }

        /// <summary>Gets visibility for the document dim overlay during address editing.</summary>
        public Visibility AddressEditingVisibility { get { return IsAddressEditing ? Visibility.Visible : Visibility.Collapsed; } }

        /// <summary>Gets visibility for compact side controls hidden during address editing.</summary>
        public Visibility CompactChromeVisibility { get { return IsAddressEditing ? Visibility.Collapsed : Visibility.Visible; } }

        /// <summary>Gets visibility for Reload when a source exists and the shell is idle.</summary>
        public Visibility ReloadButtonVisibility { get { return HasSource && !IsLoading && !IsAddressEditing ? Visibility.Visible : Visibility.Collapsed; } }

        /// <summary>Gets visibility for Stop while loading and outside address-edit mode.</summary>
        public Visibility StopButtonVisibility { get { return IsLoading && !IsAddressEditing ? Visibility.Visible : Visibility.Collapsed; } }

        /// <summary>Gets whether the Hello app-bar sample notification remains unread.</summary>
        public bool HasHelloNotification
        {
            get { return hasHelloNotification; }
            private set
            {
                if (Set(ref hasHelloNotification, value, nameof(HasHelloNotification)))
                    RaisePropertyChanged(() => HelloNotificationVisibility);
            }
        }

        /// <summary>Gets visibility for the Hello notification badge.</summary>
        public Visibility HelloNotificationVisibility { get { return HasHelloNotification ? Visibility.Visible : Visibility.Collapsed; } }

        /// <summary>Gets the previous-history command.</summary>
        public RelayCommand BackCommand { get { return backCommand; } }

        /// <summary>Gets the forward-history command.</summary>
        public RelayCommand ForwardCommand { get { return forwardCommand; } }

        /// <summary>Gets the current-document reload command.</summary>
        public RelayCommand ReloadCommand { get { return reloadCommand; } }

        /// <summary>Gets the address-submit command.</summary>
        public RelayCommand NavigateCommand { get { return navigateCommand; } }

        /// <summary>Gets the local-demo command.</summary>
        public RelayCommand LoadDemoCommand { get { return loadDemoCommand; } }

        /// <summary>Gets the in-progress navigation stop command.</summary>
        public RelayCommand StopCommand { get { return stopCommand; } }

        /// <summary>Gets the explicit certificate-consent command.</summary>
        public RelayCommand ProceedCommand { get { return proceedCommand; } }

        /// <summary>Gets the menu toggle command.</summary>
        public RelayCommand ToggleMenuCommand { get { return toggleMenuCommand; } }

        /// <summary>Gets the menu-close command.</summary>
        public RelayCommand CloseMenuCommand { get { return closeMenuCommand; } }

        /// <summary>Gets the address-edit begin command.</summary>
        public RelayCommand BeginAddressEditingCommand { get { return beginAddressEditingCommand; } }

        /// <summary>Gets the address-edit cancellation command.</summary>
        public RelayCommand CancelAddressEditingCommand { get { return cancelAddressEditingCommand; } }

        /// <summary>Gets the Hello system-toast command.</summary>
        public RelayCommand HelloCommand { get { return helloCommand; } }

        /// <summary>Navigates backward through browser policy.</summary>
        private void GoBack() { CancelAddressEditing(); CloseMenu(); browserService.GoBack(); }

        /// <summary>Navigates forward through browser policy.</summary>
        private void GoForward() { CancelAddressEditing(); CloseMenu(); browserService.GoForward(); }

        /// <summary>Reloads the current document through browser policy.</summary>
        private void Reload() { CancelAddressEditing(); CloseMenu(); browserService.Reload(); }

        /// <summary>Validates the editable address and requests HTTP(S) navigation.</summary>
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
            browserService.Navigate(uri);
        }

        /// <summary>Loads the app-owned local demo.</summary>
        private void LoadDemo() { CancelAddressEditing(); CloseMenu(); browserService.LoadDemo(); }

        /// <summary>Stops the active main-document navigation.</summary>
        private void Stop() { CancelAddressEditing(); CloseMenu(); browserService.Stop(); }

        /// <summary>Consumes a completed warning page's exact consent token.</summary>
        private void Proceed() { CancelAddressEditing(); CloseMenu(); browserService.ProceedWithWarning(); }

        /// <summary>Toggles menu state independently of browser history.</summary>
        private void ToggleMenu() { CancelAddressEditing(); IsMenuOpen = !IsMenuOpen; }

        /// <summary>Closes the secondary menu.</summary>
        private void CloseMenu() { IsMenuOpen = false; }

        /// <summary>Stores the current source as the cancellation target and begins editing.</summary>
        private void BeginAddressEditing() { CloseMenu(); addressBeforeEditing = Address; IsAddressEditing = true; }

        /// <summary>Restores the latest committed address when editing is dismissed.</summary>
        private void CancelAddressEditing()
        {
            if (!IsAddressEditing) return;
            Address = addressBeforeEditing;
            IsAddressEditing = false;
        }

        /// <summary>Clears the badge and shows the sample system notification.</summary>
        private void ShowHelloNotification()
        {
            CloseMenu();
            HasHelloNotification = false;
            toastNotificationService.Show("Hello", "Hello from MiniBrowser.");
        }

        /// <summary>Updates bindings and commands from the browser service's copied state.</summary>
        /// <param name="sender">The browser service publishing the snapshot.</param>
        /// <param name="state">The immutable state snapshot.</param>
        private void OnBrowserStateChanged(object sender, BrowserNavigationState state)
        {
            browserState = state;
            string committedAddress = state.Source == null ? string.Empty : state.Source.AbsoluteUri;
            if (IsAddressEditing)
                addressBeforeEditing = committedAddress;
            else
                Address = committedAddress;

            RaisePropertyChanged(() => DocumentTitle);
            RaisePropertyChanged(() => CanGoBack);
            RaisePropertyChanged(() => CanGoForward);
            RaisePropertyChanged(() => IsLoading);
            RaisePropertyChanged(() => LoadingVisibility);
            RaisePropertyChanged(() => HasSource);
            RaisePropertyChanged(() => ReloadButtonVisibility);
            RaisePropertyChanged(() => StopButtonVisibility);
            backCommand.RaiseCanExecuteChanged();
            forwardCommand.RaiseCanExecuteChanged();
            reloadCommand.RaiseCanExecuteChanged();
            stopCommand.RaiseCanExecuteChanged();
        }

        /// <summary>Displays navigation-policy status to any status presentation bound by the host.</summary>
        /// <param name="text">The status message selected by policy.</param>
        private void OnStatusChanged(string text) { Status = text; }

        /// <summary>Updates consent availability after the matching warning page completes.</summary>
        /// <param name="ready">Whether explicit consent can proceed.</param>
        private void OnWarningConsentReady(bool ready) { CanProceedWithWarning = ready; }

        /// <summary>Detaches browser service events and releases MVVM Light messenger subscriptions.</summary>
        public void Dispose()
        {
            if (isDisposed)
                return;

            isDisposed = true;
            browserService.StateChanged -= OnBrowserStateChanged;
            browserService.StatusChanged -= OnStatusChanged;
            browserService.WarningConsentReady -= OnWarningConsentReady;
            Cleanup();
        }
    }
}
