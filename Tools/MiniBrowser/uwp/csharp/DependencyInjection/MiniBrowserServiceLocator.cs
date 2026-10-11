using GalaSoft.MvvmLight.Ioc;
using System;
using WebKit;
using WebKitMiniBrowser.Services;
using WebKitMiniBrowser.Services.Interfaces;
using WebKitMiniBrowser.ViewModels;

namespace WebKitMiniBrowser.DependencyInjection
{
    /// <summary>Configures MVVM Light SimpleIoc for application and page-scoped services.</summary>
    public static class MiniBrowserServiceLocator
    {
        /// <summary>Registers application-wide services before the root Frame is created.</summary>
        public static void ConfigureApplicationServices()
        {
            if (!SimpleIoc.Default.IsRegistered<IToastNotificationService>())
                SimpleIoc.Default.Register<IToastNotificationService, ToastNotificationService>();
        }

        /// <summary>
        /// Registers the browser service and view model after compiled XAML creates the page's browser control.
        /// Replaces a prior page scope so the new view model cannot retain a disposed XAML control.
        /// </summary>
        /// <param name="browser">The page-owned WebKitWebView instance.</param>
        /// <param name="blockedTemplatePath">The packaged warning-page template path.</param>
        /// <param name="overridePath">The optional application-local warning template path.</param>
        /// <param name="demoTemplatePath">The packaged local demo template path.</param>
        public static void ConfigureBrowserPage(WebKitWebView browser, string blockedTemplatePath,
            string overridePath, string demoTemplatePath)
        {
            if (SimpleIoc.Default.IsRegistered<BrowserViewModel>())
            {
                SimpleIoc.Default.GetInstance<BrowserViewModel>().Dispose();
                SimpleIoc.Default.Unregister<BrowserViewModel>();
            }

            if (SimpleIoc.Default.IsRegistered<IBrowserService>())
            {
                SimpleIoc.Default.GetInstance<IBrowserService>().Dispose();
                SimpleIoc.Default.Unregister<IBrowserService>();
            }

            BrowserPolicy policy = new BrowserPolicy(browser, blockedTemplatePath, overridePath, demoTemplatePath);
            BrowserService browserService = new BrowserService(browser, policy);
            SimpleIoc.Default.Register<IBrowserService>(() => browserService);
            SimpleIoc.Default.Register<BrowserViewModel>();
        }

        /// <summary>Resolves the configured service or view model from SimpleIoc.</summary>
        /// <typeparam name="TService">The registered service type.</typeparam>
        /// <returns>The service-locator-managed instance.</returns>
        public static TService Get<TService>() where TService : class
        {
            return SimpleIoc.Default.GetInstance<TService>();
        }
    }
}
